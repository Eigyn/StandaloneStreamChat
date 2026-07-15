using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.YouTube
{
    /// <summary>
    /// IChatService adapter around <see cref="InnerTubeChatSession"/>: owns the
    /// background loop, live-stream discovery, reconnects and status reporting.
    /// </summary>
    public class YouTubeLiveChatClient : IChatService
    {
        private const int MinPollDelayMs = 500;
        private const int MaxPollDelayMs = 10000;

        public event Action<ChatMessage>? MessageReceived;
        public event Action<string>? StatusChanged;

        private readonly string _channel;
        private readonly IPALogger _log;
        private readonly HttpClient _http;
        private CancellationTokenSource? _cts;
        private Task? _runTask;
        private int _messageCount;
        private volatile InnerTubeChatSession? _currentSession;
        // Shared across every (re)connected session so message-id dedup survives
        // reconnects — otherwise a fresh session replays recently-shown messages.
        // Owned by ChatManager and passed in, so dedup also survives a full
        // platform restart (toggle/channel change), not just an in-loop reconnect.
        private readonly HashSet<string> _seenMessageIds;
        private readonly Queue<string> _seenMessageIdOrder;

        /// <summary>The connected chat session (video id, API key), or null when not connected.</summary>
        public InnerTubeChatSession? CurrentSession => _currentSession;

        public YouTubeLiveChatClient(string channel, IPALogger log)
            : this(channel, log, new HashSet<string>(), new Queue<string>())
        {
        }

        public YouTubeLiveChatClient(string channel, IPALogger log,
            HashSet<string> seenMessageIds, Queue<string> seenMessageIdOrder)
        {
            _channel = channel.Trim();
            _log = log;
            _seenMessageIds = seenMessageIds;
            _seenMessageIdOrder = seenMessageIdOrder;
            try { ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12; }
            catch { /* runtime may not allow changing it; HTTPS worked already for Twitch */ }
            _http = InnerTubeChatSession.CreateHttpClient();
        }

        public void Start()
        {
            if (_runTask != null)
                return;
            _cts = new CancellationTokenSource();
            var token = _cts.Token;
            _runTask = Task.Run(() => RunAsync(token));
        }

        public void Stop() => StopInternal(waitForExit: true, disposeHttp: false);

        public void Stop(bool waitForExit)
        {
            StopInternal(waitForExit, disposeHttp: false);
        }

        public void Dispose()
        {
            StopInternal(waitForExit: true, disposeHttp: true);
        }

        public void Dispose(bool waitForExit)
        {
            StopInternal(waitForExit, disposeHttp: true);
        }

        private void StopInternal(bool waitForExit, bool disposeHttp)
        {
            if (_cts == null)
            {
                if (disposeHttp)
                    _http.Dispose();
                return;
            }

            var cts = _cts;
            var runTask = _runTask;
            _cts = null;
            _runTask = null;
            _currentSession = null;

            cts.Cancel();
            if (waitForExit)
            {
                try { runTask?.Wait(2000); } catch { /* cancellation surfaces here */ }
                cts.Dispose();
                if (disposeHttp)
                    _http.Dispose();
                return;
            }

            if (runTask != null)
            {
                runTask.ContinueWith(_ =>
                {
                    cts.Dispose();
                    if (disposeHttp)
                        _http.Dispose();
                }, TaskScheduler.Default);
            }
            else
            {
                cts.Dispose();
                if (disposeHttp)
                    _http.Dispose();
            }
        }

        private async Task RunAsync(CancellationToken ct)
        {
            var errorDelaySeconds = 2;
            while (!ct.IsCancellationRequested)
            {
                try
                {
                    var session = new InnerTubeChatSession(_http, _seenMessageIds, _seenMessageIdOrder);
                    session.Trace += t => _log.Debug("YouTube/InnerTube: " + t);

                    var videoId = InnerTubeChatSession.LooksLikeVideoId(_channel)
                        ? _channel
                        : await session.ResolveLiveVideoIdAsync(_channel, ct).ConfigureAwait(false);

                    if (videoId == null)
                    {
                        StatusChanged?.Invoke($"YouTube: {_channel} is not live - checking again in 2 minutes");
                        await Task.Delay(TimeSpan.FromSeconds(120), ct).ConfigureAwait(false);
                        continue;
                    }

                    if (!await session.StartAsync(videoId, ct).ConfigureAwait(false))
                    {
                        StatusChanged?.Invoke("YouTube: live chat unavailable - retrying in 60s");
                        await Task.Delay(TimeSpan.FromSeconds(60), ct).ConfigureAwait(false);
                        continue;
                    }

                    StatusChanged?.Invoke($"YouTube: connected to live chat ({videoId})");
                    errorDelaySeconds = 2;
                    _currentSession = session;

                    while (!ct.IsCancellationRequested)
                    {
                        var result = await session.PollAsync(ct).ConfigureAwait(false);
                        if (result == null || !result.ContinuationValid)
                        {
                            _currentSession = null;
                            StatusChanged?.Invoke("YouTube: chat ended or reset - reconnecting");
                            try { await Task.Delay(TimeSpan.FromSeconds(errorDelaySeconds), ct).ConfigureAwait(false); }
                            catch (OperationCanceledException) { break; }
                            errorDelaySeconds = Math.Min(errorDelaySeconds * 2, 120);
                            break;
                        }

                        foreach (var message in result.Messages)
                        {
                            var count = Interlocked.Increment(ref _messageCount);
                            if (count <= 5 || count % 100 == 0)
                                _log.Info($"YouTube: received message #{count} (from {message.Author})");
                            MessageReceived?.Invoke(message);
                        }

                        await Task.Delay(ClampPollDelay(result.TimeoutMs), ct).ConfigureAwait(false);
                    }
                }
                catch (Exception ex)
                {
                    _currentSession = null;
                    if (ct.IsCancellationRequested)
                        break;
                    StatusChanged?.Invoke($"YouTube: error ({ex.GetType().Name}), retrying in {errorDelaySeconds}s");
                    _log.Debug(ex.ToString());
                    try { await Task.Delay(TimeSpan.FromSeconds(errorDelaySeconds), ct).ConfigureAwait(false); }
                    catch (OperationCanceledException) { break; }
                    errorDelaySeconds = Math.Min(errorDelaySeconds * 2, 120);
                }
            }
        }

        private static int ClampPollDelay(int timeoutMs)
        {
            if (timeoutMs < MinPollDelayMs)
                return MinPollDelayMs;
            if (timeoutMs > MaxPollDelayMs)
                return MaxPollDelayMs;
            return timeoutMs;
        }
    }
}
