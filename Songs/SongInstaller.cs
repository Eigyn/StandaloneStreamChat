using System;
using System.Collections.Concurrent;
using System.IO;
using System.IO.Compression;
using System.Threading.Tasks;
using BeatSaberMarkupLanguage;
using SongCore;
using SimpleBSStreamChat.UI;
using UnityEngine.SceneManagement;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Songs
{
    /// <summary>Outcome of a SongCore refresh + resolve: the loaded level, or an
    /// error string when it could not be resolved.</summary>
    internal readonly struct LevelResolveResult
    {
        public readonly BeatmapLevel? Level;
        public readonly string Error;

        public LevelResolveResult(BeatmapLevel? level, string error)
        {
            Level = level;
            Error = error;
        }
    }

    internal sealed class SongInstaller
    {
        // Upper bound on how long to wait for SongCore's load event before giving
        // up, so a missed event can never leave a request stuck in Installing.
        private const float RefreshTimeoutSeconds = 30f;

        private readonly MainThreadDispatcher _dispatcher;
        private readonly IPALogger _log;

        public SongInstaller(MainThreadDispatcher dispatcher, IPALogger log)
        {
            _dispatcher = dispatcher;
            _log = log;
        }

        public bool IsInstalled(string hash)
        {
            if (string.IsNullOrEmpty(hash))
                return false;
            try
            {
                return Collections.songWithHashPresent(hash.ToUpperInvariant());
            }
            catch (Exception ex)
            {
                _log.Warn($"SongCore installed-map check failed for '{hash}': {ex.Message}");
                return false;
            }
        }

        public BeatmapLevel? ResolveLoadedLevel(string hash)
        {
            if (string.IsNullOrEmpty(hash))
                return null;
            try
            {
                return Loader.GetLevelByHash(hash);
            }
            catch (Exception ex)
            {
                _log.Warn($"SongCore level resolve failed for '{hash}': {ex.Message}");
                return null;
            }
        }

        public string ExtractZip(byte[] bytes, string key, string songName, string mapper)
        {
            if (bytes == null || bytes.Length == 0)
                throw new InvalidDataException("Downloaded zip was empty.");

            var customLevels = Path.GetFullPath(CustomLevelPathHelper.customLevelsDirectoryPath);
            Directory.CreateDirectory(customLevels);

            var folderName = MakeSafeFolderName($"{key} ({songName} - {mapper})");
            var targetFolder = Path.Combine(customLevels, folderName);
            // A per-extraction unique temp folder: two accepts of the same key
            // (duplicates enabled) or any overlap can never collide on the temp
            // path, corrupting each other's extraction.
            var tempFolder = targetFolder + "." + Guid.NewGuid().ToString("N") + ".tmp";

            DeleteDirectoryIfExists(tempFolder);
            Directory.CreateDirectory(tempFolder);

            try
            {
                using (var stream = new MemoryStream(bytes))
                using (var archive = new ZipArchive(stream, ZipArchiveMode.Read))
                {
                    ExtractArchive(archive, tempFolder);
                }

                // Only now replace any existing install: the old folder is not
                // deleted until the replacement is fully extracted, so a failed
                // download/extraction never destroys a good installed map.
                DeleteDirectoryIfExists(targetFolder);
                Directory.Move(tempFolder, targetFolder);
                return targetFolder;
            }
            catch
            {
                DeleteDirectoryIfExists(tempFolder);
                throw;
            }
        }

        /// <summary>
        /// Refreshes SongCore and resolves the new level by hash. Always
        /// unsubscribes from <see cref="Loader.SongsLoadedEvent"/> — including on a
        /// timeout if the event never fires — so a request can never wedge in the
        /// Installing state. Safe to call from any thread; the SongCore work runs
        /// on the main thread.
        /// </summary>
        public Task<LevelResolveResult> RefreshAndResolveAsync(string hash)
        {
            var tcs = new TaskCompletionSource<LevelResolveResult>();
            if (_dispatcher.IsMainThread)
                BeginRefresh(hash, tcs);
            else
                _dispatcher.Enqueue(() => BeginRefresh(hash, tcs));
            return tcs.Task;
        }

        // Main-thread only.
        private void BeginRefresh(string hash, TaskCompletionSource<LevelResolveResult> tcs)
        {
            if (Loader.Instance == null)
            {
                tcs.TrySetResult(new LevelResolveResult(null, "SongCore is not ready yet"));
                return;
            }
            if (Loader.AreSongsLoading)
            {
                tcs.TrySetResult(new LevelResolveResult(null, "SongCore is already loading songs"));
                return;
            }
            // Refuse to refresh SongCore where accepting isn't allowed (a song, the
            // results screen, or any non-solo flow) — navigation would be refused
            // there anyway and reloading the pack a live flow references is churn.
            if (!MenuState.CanAcceptNow())
            {
                tcs.TrySetResult(new LevelResolveResult(null, "return to menu and accept again"));
                return;
            }

            Action<Loader, ConcurrentDictionary<string, BeatmapLevel>> handler = null!;
            handler = (loader, levels) =>
            {
                Loader.SongsLoadedEvent -= handler;
                if (tcs.Task.IsCompleted)
                    return; // already timed out
                var level = ResolveLoadedLevel(hash);
                tcs.TrySetResult(new LevelResolveResult(level, level == null ? "Map failed to load" : ""));
            };

            try
            {
                Loader.SongsLoadedEvent += handler;
                Loader.Instance.RefreshSongs(false);
            }
            catch (Exception ex)
            {
                Loader.SongsLoadedEvent -= handler;
                tcs.TrySetResult(new LevelResolveResult(null, "SongCore refresh failed: " + ex.Message));
                return;
            }

            // Timeout guard, marshalled back to the main thread so the event
            // (un)subscription stays single-threaded. Both the load event and this
            // timeout race on TrySetResult; whichever fires first wins and the
            // other unsubscribe becomes a no-op.
            _dispatcher.EnqueueDelayed(RefreshTimeoutSeconds, () =>
            {
                if (tcs.Task.IsCompleted)
                    return;
                Loader.SongsLoadedEvent -= handler;
                tcs.TrySetResult(new LevelResolveResult(null, "SongCore did not finish loading in time"));
            });
        }

        private static void ExtractArchive(ZipArchive archive, string targetFolder)
        {
            var root = Path.GetFullPath(targetFolder);
            if (!root.EndsWith(Path.DirectorySeparatorChar.ToString(), StringComparison.Ordinal))
                root += Path.DirectorySeparatorChar;

            foreach (var entry in archive.Entries)
            {
                var relative = entry.FullName
                    .Replace('/', Path.DirectorySeparatorChar)
                    .Replace('\\', Path.DirectorySeparatorChar);
                var destination = Path.GetFullPath(Path.Combine(root, relative));
                if (!destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Zip entry escapes the target folder: " + entry.FullName);

                if (string.IsNullOrEmpty(entry.Name))
                {
                    Directory.CreateDirectory(destination);
                    continue;
                }

                var parent = Path.GetDirectoryName(destination);
                if (!string.IsNullOrEmpty(parent))
                    Directory.CreateDirectory(parent);

                using (var source = entry.Open())
                using (var dest = File.Create(destination))
                    source.CopyTo(dest);
            }
        }

        private static string MakeSafeFolderName(string value)
        {
            var invalid = Path.GetInvalidFileNameChars();
            var chars = value.ToCharArray();
            for (var i = 0; i < chars.Length; i++)
            {
                if (Array.IndexOf(invalid, chars[i]) >= 0 || char.IsControl(chars[i]))
                    chars[i] = '_';
            }

            var safe = new string(chars).Trim();
            if (string.IsNullOrEmpty(safe))
                safe = "Requested Map";
            return safe.Length <= 120 ? safe : safe.Substring(0, 120).TrimEnd();
        }

        private static void DeleteDirectoryIfExists(string path)
        {
            if (!Directory.Exists(path))
                return;

            foreach (var file in Directory.GetFiles(path, "*", SearchOption.AllDirectories))
                File.SetAttributes(file, FileAttributes.Normal);
            Directory.Delete(path, true);
        }
    }
}
