using System;
using System.Threading;
using System.Threading.Tasks;
using SimpleBSStreamChat.Songs;
using SimpleBSStreamChat.UI;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.Requests
{
    internal sealed class SongRequestPipeline : IDisposable
    {
        private readonly SongRequestManager _manager;
        private readonly BeatSaverService _beatSaver;
        private readonly SongInstaller _installer;
        private readonly LevelNavigator _navigator;
        private readonly MainThreadDispatcher _dispatcher;
        private readonly IPALogger _log;
        private readonly CancellationTokenSource _shutdown = new CancellationTokenSource();
        // Serializes the whole accept→download→extract→refresh→navigate path so
        // only one install runs at a time: concurrent/rapid accepts cannot overlap
        // downloads, collide on temp/target folders, or trigger overlapping
        // SongCore refreshes or navigation.
        private readonly SemaphoreSlim _installGate = new SemaphoreSlim(1, 1);

        public SongRequestPipeline(SongRequestManager manager, BeatSaverService beatSaver,
            SongInstaller installer, LevelNavigator navigator, MainThreadDispatcher dispatcher, IPALogger log)
        {
            _manager = manager;
            _beatSaver = beatSaver;
            _installer = installer;
            _navigator = navigator;
            _dispatcher = dispatcher;
            _log = log;
        }

        public void Accept(SongRequest request)
        {
            _ = RunAcceptAsync(request, _shutdown.Token);
        }

        private async Task RunAcceptAsync(SongRequest request, CancellationToken token)
        {
            var id = request.Id;
            var key = request.Key;
            var hash = request.Hash;
            var songName = request.SongName;
            var mapper = request.LevelAuthorName;

            var acquired = false;
            try
            {
                // Show a brief "queued" state if another install currently holds
                // the gate, so a rapidly-accepted row doesn't look frozen at 0%.
                if (_installGate.CurrentCount == 0)
                    _manager.UpdateStatus(id, SongRequestStatus.Downloading, "queued");
                await _installGate.WaitAsync(token).ConfigureAwait(false);
                acquired = true;

                if (string.IsNullOrEmpty(hash))
                {
                    _manager.MarkFailed(id, "missing map hash");
                    return;
                }

                if (_installer.IsInstalled(hash))
                {
                    var loaded = _installer.ResolveLoadedLevel(hash);
                    if (loaded != null)
                    {
                        _log.Info($"Song request already installed: {key} ({hash}).");
                        await NavigateAsync(id, loaded).ConfigureAwait(false);
                    }
                    else
                    {
                        _manager.UpdateStatus(id, SongRequestStatus.Installing, "");
                        var resolved = await _installer.RefreshAndResolveAsync(hash).ConfigureAwait(false);
                        if (resolved.Level != null)
                            await NavigateAsync(id, resolved.Level).ConfigureAwait(false);
                        else
                            KeepReadyOrFail(id, resolved.Error);
                    }
                    return;
                }

                var beatmap = _beatSaver.GetCached(id);
                var version = beatmap?.LatestVersion;
                if (version is null)
                {
                    _manager.MarkFailed(id, "BeatSaver cache expired");
                    return;
                }

                _manager.UpdateProgress(id, 0d, "0%");
                var progress = new Progress<double>(p =>
                {
                    var clamped = Math.Max(0d, Math.Min(1d, p));
                    _manager.UpdateProgress(id, clamped, $"{clamped:P0}");
                });

                var bytes = await version.DownloadZIP(token, progress).ConfigureAwait(false);
                if (bytes == null || bytes.Length == 0)
                {
                    _manager.MarkFailed(id, "download returned no data");
                    return;
                }

                _manager.UpdateProgress(id, 1d, "100%");
                await Task.Run(() => _installer.ExtractZip(bytes, key, songName, mapper), token)
                    .ConfigureAwait(false);

                _manager.UpdateStatus(id, SongRequestStatus.Installing, "");
                var installed = await _installer.RefreshAndResolveAsync(hash).ConfigureAwait(false);
                if (installed.Level != null)
                {
                    _log.Info($"Song request installed: {key} ({hash}).");
                    await NavigateAsync(id, installed.Level).ConfigureAwait(false);
                }
                else
                {
                    KeepReadyOrFail(id, installed.Error);
                }
            }
            catch (OperationCanceledException)
            {
                if (!_shutdown.IsCancellationRequested)
                    _manager.MarkFailed(id, "cancelled");
            }
            catch (Exception ex)
            {
                _log.Warn($"Song request install failed for '{key}': {ex.Message}");
                _manager.MarkFailed(id, ex.Message);
            }
            finally
            {
                if (acquired)
                    _installGate.Release();
            }
        }

        // Runs navigation on the main thread and completes when it finishes, so
        // the install gate is held across navigation too (no overlap with the
        // next accept).
        private Task NavigateAsync(string requestId, BeatmapLevel level)
        {
            var tcs = new TaskCompletionSource<bool>();
            void Go()
            {
                try
                {
                    if (_navigator.TryGoToLevel(level, out var error))
                        _manager.CompleteAccepted(requestId);
                    else
                        KeepReadyOrFail(requestId, error);
                }
                finally
                {
                    tcs.TrySetResult(true);
                }
            }

            if (_dispatcher.IsMainThread)
                Go();
            else
                _dispatcher.Enqueue(Go);
            return tcs.Task;
        }

        private void KeepReadyOrFail(string requestId, string error)
        {
            if (string.Equals(error, "return to menu and accept again", StringComparison.Ordinal))
                _manager.MarkReady(requestId, error);
            else
                _manager.MarkFailed(requestId, string.IsNullOrEmpty(error) ? "Map failed to load" : error);
        }

        public void Dispose()
        {
            _shutdown.Cancel();
            _shutdown.Dispose();
            _installGate.Dispose();
        }
    }
}
