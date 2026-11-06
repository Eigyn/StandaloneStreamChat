using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using SimpleBSStreamChat.Songs;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.Requests
{
    public enum EnqueueResult
    {
        Ok,
        NotAllowed,
        OnCooldown,
        QueueFull,
        TooManyPerUser,
        Duplicate,
    }

    /// <summary>
    /// Thread-safe, versioned song-request queue. Mirrors <see cref="ChatManager"/>'s
    /// concurrency idiom: a lock-guarded list plus a <see cref="Version"/> counter the
    /// UI polls on the main thread, copying a snapshot via <see cref="CopyRequests"/>
    /// when it changes. Chat commands arrive on background threads through
    /// <see cref="OnChatMessage"/>; the UI calls <see cref="Accept"/>/<see cref="Deny"/>
    /// on the main thread.
    ///
    /// P1: metadata is faked (SongName = "Map &lt;key&gt;", status Ready immediately) and
    /// Accept simply logs and advances the queue. BeatSaver metadata, download,
    /// install and navigation land in later phases.
    /// </summary>
    public class SongRequestManager
    {
        private const double NotFoundAutoPurgeSeconds = 30d;
        private const double NoticeRateLimitSeconds = 5d;
        // A fixed abuse/growth guard. Queue presentation now scrolls, so this
        // deliberately is not a user-visible layout setting.
        private const int QueueCapacity = 100;

        private readonly object _lock = new object();
        private readonly List<SongRequest> _queue = new List<SongRequest>();
        private readonly Dictionary<string, DateTime> _lastRequestUtc = new Dictionary<string, DateTime>();
        private readonly Dictionary<string, DateTime> _notFoundSinceUtc = new Dictionary<string, DateTime>();
        private readonly Dictionary<string, DateTime> _lastNoticeUtc = new Dictionary<string, DateTime>();
        private readonly PluginConfig _config;
        private readonly IPALogger _log;
        private readonly BeatSaverService? _beatSaver;
        private readonly RequestQueueStorage? _storage;
        private SongRequestPipeline? _pipeline;
        private int _version;
        // Throttles the opportunistic prune of the cooldown/notice tracking maps.
        private DateTime _nextPruneUtc = DateTime.MinValue;
        // Cancels in-flight metadata fetches/retries on shutdown.
        private readonly CancellationTokenSource _lifetime = new CancellationTokenSource();
        // Caps concurrent BeatSaver metadata lookups so restoring a full queue
        // doesn't fire dozens of requests at once.
        private readonly SemaphoreSlim _metadataGate = new SemaphoreSlim(3, 3);
        // Thread-safe source of retry-backoff jitter (Random is not thread-safe and
        // metadata fetches run on concurrent thread-pool continuations).
        private static readonly Random _jitterRng = new Random();

        /// <summary>Raised (off the queue lock) with feedback for the in-game chat panel.</summary>
        public event Action<string>? Notice;
        /// <summary>Raised off-lock after a new live request enters the queue.</summary>
        public event Action? Added;
        public event Action? Accepted;

        public SongRequestManager(PluginConfig config, IPALogger log, BeatSaverService? beatSaver = null,
            RequestQueueStorage? storage = null)
        {
            _config = config;
            _log = log;
            _beatSaver = beatSaver;
            _storage = storage;
        }

        internal void SetPipeline(SongRequestPipeline pipeline)
        {
            _pipeline = pipeline;
        }

        public int Version { get { lock (_lock) return _version; } }

        /// <summary>True when the queue currently contains at least one request.</summary>
        public bool HasRequests { get { lock (_lock) return _queue.Count > 0; } }

        /// <summary>Current queue size for compact UI labels.</summary>
        public int Count { get { lock (_lock) return _queue.Count; } }

        public void RestorePersistedQueue()
        {
            if (_storage == null)
                return;
            if (!_config.PersistRequestQueue)
            {
                _storage.Clear();
                return;
            }

            var entries = _storage.Load();
            if (entries.Count == 0)
                return;

            var restored = new List<SongRequest>();
            lock (_lock)
            {
                foreach (var entry in entries)
                {
                    if (_queue.Count >= QueueCapacity)
                        break;
                    if (entry.RequesterPlatform == ChatPlatform.System)
                        continue;
                    var key = (entry.Key ?? "").Trim().ToLowerInvariant().TrimStart('0');
                    if (!RequestCommandParser.IsValidKey(key))
                        continue;
                    var requester = (entry.RequesterName ?? "").Trim();
                    if (requester.Length == 0)
                        continue;
                    if (!_config.AllowDuplicates &&
                        _queue.Exists(r => r.Key.Equals(key, StringComparison.OrdinalIgnoreCase)))
                        continue;

                    var requestedAt = NormalizeUtc(entry.RequestedAtUtc);
                    var request = new SongRequest(key, requester, entry.RequesterPlatform, requestedAt);
                    _queue.Add(request);
                    restored.Add(request);
                }

                if (restored.Count > 0)
                    _version++;
            }

            if (restored.Count == 0)
            {
                PersistQueue();
                return;
            }

            _log.Info($"Restored {restored.Count} saved song request(s).");
            PersistQueue();
            foreach (var request in restored)
                StartMetadataFetch(request);
        }

        public void Tick()
        {
            List<SongRequest>? removed = null;
            var now = DateTime.UtcNow;
            lock (_lock)
            {
                for (var i = _queue.Count - 1; i >= 0; i--)
                {
                    var request = _queue[i];
                    if (request.Status != SongRequestStatus.NotFound)
                        continue;

                    if (!_notFoundSinceUtc.TryGetValue(request.Id, out var sinceUtc))
                    {
                        _notFoundSinceUtc[request.Id] = now;
                        continue;
                    }

                    if ((now - sinceUtc).TotalSeconds < NotFoundAutoPurgeSeconds)
                        continue;

                    removed ??= new List<SongRequest>();
                    removed.Add(request);
                    _queue.RemoveAt(i);
                    _notFoundSinceUtc.Remove(request.Id);
                }

                if (removed != null)
                    _version++;

                // Opportunistically prune long-elapsed cooldown/notice bookkeeping so
                // these maps don't grow unbounded over a multi-hour stream.
                if (now >= _nextPruneUtc)
                {
                    _nextPruneUtc = now.AddSeconds(60d);
                    PruneStaleTrackingLocked(now);
                }
            }

            if (removed == null)
                return;

            foreach (var request in removed)
                _beatSaver?.Forget(request.Id);
            PersistQueue();
        }

        // Called under _lock. Drops cooldown/notice entries whose window has safely
        // elapsed so they no longer affect any decision — bounding map growth.
        private void PruneStaleTrackingLocked(DateTime now)
        {
            var cooldown = Math.Max(0, _config.PerUserCooldownSeconds);
            // A 60s floor keeps a 0/short cooldown from letting the map grow forever.
            RemoveExpired(_lastRequestUtc, now, Math.Max(cooldown, 60));
            RemoveExpired(_lastNoticeUtc, now, Math.Max(NoticeRateLimitSeconds, 60d));
        }

        private static void RemoveExpired(Dictionary<string, DateTime> map, DateTime now, double ttlSeconds)
        {
            List<string>? expired = null;
            foreach (var pair in map)
            {
                if ((now - pair.Value).TotalSeconds >= ttlSeconds)
                    (expired ??= new List<string>()).Add(pair.Key);
            }
            if (expired == null)
                return;
            foreach (var key in expired)
                map.Remove(key);
        }

        /// <summary>Copies the queue into <paramref name="target"/> (cleared first).</summary>
        public void CopyRequests(List<SongRequest> target)
        {
            lock (_lock)
            {
                target.Clear();
                foreach (var request in _queue)
                    target.Add(request.Snapshot());
            }
        }

        /// <summary>Command entry point — called from a chat service's background thread.</summary>
        public void OnChatMessage(ChatMessage message)
        {
            if (message == null || message.Platform == ChatPlatform.System)
                return;
            if (string.IsNullOrEmpty(message.Author))
                return;
            if (!_config.EnableSongRequests)
                return;

            var parsed = RequestCommandParser.Parse(message.Text, _config.RequestCommand);
            if (!parsed.IsCommand)
                return;
            if (parsed.Key == null)
            {
                RaiseNotice($"@{message.Author} that isn't a valid map key.",
                    NoticeKey("invalid", message));
                return;
            }

            var result = TryEnqueue(message, parsed.Key);
            if (result != EnqueueResult.Ok)
                _log.Info($"Song request '{parsed.Key}' from {message.Author} ({message.Platform}) rejected: {result}.");
            switch (result)
            {
                case EnqueueResult.NotAllowed:
                    RaiseNotice($"@{message.Author} song requests aren't open to you right now.",
                        NoticeKey("not-allowed", message));
                    break;
                case EnqueueResult.OnCooldown:
                    RaiseNotice($"@{message.Author} please wait before requesting again.",
                        NoticeKey("cooldown", message));
                    break;
                case EnqueueResult.QueueFull:
                    RaiseNotice("The request queue is full.", "queue-full");
                    break;
                case EnqueueResult.TooManyPerUser:
                    RaiseNotice($"@{message.Author} you already have the max requests queued.",
                        NoticeKey("too-many", message));
                    break;
                case EnqueueResult.Duplicate:
                    RaiseNotice($"@{message.Author} that map is already in the queue.",
                        NoticeKey("duplicate", message) + ":" + parsed.Key);
                    break;
                // Ok is announced inside TryEnqueue once the position is known.
            }
        }

        public EnqueueResult TryEnqueue(ChatMessage message, string key)
        {
            if (!IsAllowed(message))
                return EnqueueResult.NotAllowed;

            var userKey = UserKey(message);
            // The broadcaster and mods manage the queue; never throttle or cap
            // them (also keeps solo testing from tripping the spam limits).
            var trusted = message.IsBroadcaster || message.IsModerator;
            int position;
            SongRequest request;
            lock (_lock)
            {
                var cooldown = Math.Max(0, _config.PerUserCooldownSeconds);
                if (!trusted && cooldown > 0 && _lastRequestUtc.TryGetValue(userKey, out var last) &&
                    (DateTime.UtcNow - last).TotalSeconds < cooldown)
                    return EnqueueResult.OnCooldown;

                if (_queue.Count >= QueueCapacity)
                    return EnqueueResult.QueueFull;

                // A not-found (about to auto-purge) or failed-download entry should
                // not block someone re-requesting the same key.
                if (!_config.AllowDuplicates &&
                    _queue.Exists(r => r.Key.Equals(key, StringComparison.OrdinalIgnoreCase) &&
                                       r.Status != SongRequestStatus.NotFound &&
                                       r.Status != SongRequestStatus.Failed))
                    return EnqueueResult.Duplicate;

                if (!trusted && _config.MaxRequestsPerUser > 0)
                {
                    var mine = _queue.FindAll(r => UserKey(r) == userKey).Count;
                    if (mine >= _config.MaxRequestsPerUser)
                        return EnqueueResult.TooManyPerUser;
                }

                // Enters the queue as FetchingInfo; metadata arrives asynchronously.
                request = new SongRequest(key, message.Author, message.Platform);
                _queue.Add(request);
                _lastRequestUtc[userKey] = DateTime.UtcNow;
                position = _queue.Count;
                _version++;
            }

            _log.Info($"Song request queued (#{position}): {key} by {message.Author} ({message.Platform}).");
            RaiseAdded();
            PersistQueue();
            RaiseNotice($"Request added (#{position}): {key}");
            StartMetadataFetch(request);
            return EnqueueResult.Ok;
        }

        // Fire-and-forget BeatSaver lookup; fills the entry in and bumps the
        // version (which the UI polls) on completion. A genuine 404 purges the
        // request; a transient network/server failure is retried with backoff so
        // a BeatSaver outage never mistakes a valid request for a missing map.
        private async void StartMetadataFetch(SongRequest request)
        {
            if (_beatSaver == null)
            {
                // No BeatSaver reference (e.g. unit tests): keep the P1 stub.
                CompleteMetadata(request.Id, new MapInfo("Map " + request.Key, "", "", "", "", 0));
                return;
            }

            var token = _lifetime.Token;
            var attempt = 0;
            try
            {
                while (!token.IsCancellationRequested)
                {
                    // Stop if the request was denied/removed (including during a
                    // prior backoff delay) before spending another BeatSaver call.
                    if (!IsInQueue(request.Id))
                        return;

                    MapFetchResult result;
                    await _metadataGate.WaitAsync(token).ConfigureAwait(false);
                    try
                    {
                        result = await _beatSaver.FetchByKeyAsync(request.Id, request.Key, token)
                            .ConfigureAwait(false);
                    }
                    catch (Exception ex) when (!(ex is OperationCanceledException))
                    {
                        _log.Warn($"Metadata fetch for '{request.Key}' threw: {ex.Message}");
                        result = MapFetchResult.Error;
                    }
                    finally
                    {
                        _metadataGate.Release();
                    }

                    switch (result.Outcome)
                    {
                        case MapFetchOutcome.Found:
                            CompleteMetadata(request.Id, result.Info);
                            return;
                        case MapFetchOutcome.NotFound:
                            MarkNotFound(request);
                            return;
                        case MapFetchOutcome.Cancelled:
                            return;
                    }

                    // Transient error: keep the request and retry with capped
                    // backoff (3,6,12,24,48,48… seconds) plus a little jitter so a
                    // restored full queue doesn't retry in lockstep. Retrying is
                    // deliberately unbounded — it self-heals whenever BeatSaver
                    // recovers, and the streamer can Deny a stuck row meanwhile.
                    attempt++;
                    var baseDelay = Math.Min(48, 3 * (1 << Math.Min(attempt - 1, 4)));
                    var delaySeconds = baseDelay + NextJitter() * baseDelay * 0.25;
                    UpdateStatus(request.Id, SongRequestStatus.FetchingInfo, "BeatSaver unavailable — retrying");
                    await Task.Delay(TimeSpan.FromSeconds(delaySeconds), token).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException)
            {
                // Shutdown or cancellation: leave the request untouched (preserved).
            }
            catch (Exception ex)
            {
                // async void: any other escape would be an unobserved exception on a
                // thread-pool continuation (a hard crash). Log and drop it instead.
                _log.Warn($"Metadata fetch loop for '{request.Key}' failed: {ex.Message}");
            }
        }

        private bool IsInQueue(string requestId)
        {
            lock (_lock)
                return _queue.Exists(r => r.Id == requestId);
        }

        private void CompleteMetadata(string requestId, MapInfo info)
        {
            lock (_lock)
            {
                var request = _queue.Find(r => r.Id == requestId);
                if (request == null)
                {
                    _beatSaver?.Forget(requestId); // denied while fetching
                    return;
                }
                // Only a request still awaiting metadata may be completed. This makes
                // "a late/stray metadata callback clobbers a Downloading/Installing
                // row back to Ready" structurally impossible if a re-fetch path is
                // ever added (today the fetch runs exactly once and is terminal).
                if (request.Status != SongRequestStatus.FetchingInfo)
                    return;
                request.SongName = info.SongName;
                request.SongAuthorName = info.SongAuthorName;
                request.LevelAuthorName = info.LevelAuthorName;
                request.Hash = info.Hash;
                request.CoverUrl = info.CoverUrl;
                request.DurationSeconds = info.DurationSeconds;
                request.Status = SongRequestStatus.Ready;
                _notFoundSinceUtc.Remove(request.Id);
                _version++;
            }
        }

        private void MarkNotFound(SongRequest request)
        {
            bool present;
            lock (_lock)
            {
                var inQueue = _queue.Find(r => r.Id == request.Id);
                present = inQueue != null;
                if (present)
                {
                    inQueue!.Status = SongRequestStatus.NotFound;
                    inQueue.StatusDetail = "not found";
                    _notFoundSinceUtc[inQueue.Id] = DateTime.UtcNow;
                    _version++;
                }
            }
            _beatSaver?.Forget(request.Id);
            if (present)
                PersistQueue();
            if (present)
                RaiseNotice($"@{request.RequesterName} couldn't find map {request.Key} on BeatSaver.",
                    NoticeKey("not-found", request) + ":" + request.Key);
        }

        /// <summary>Main-thread: accept a request. Starts the P3 download/install
        /// pipeline; success removes the row after SongCore resolves the level.</summary>
        public void Accept(string requestId)
        {
            SongRequest? request;
            lock (_lock)
            {
                request = _queue.Find(r => r.Id == requestId);
                if (request == null)
                    return;
                if (request.Status != SongRequestStatus.Ready &&
                    request.Status != SongRequestStatus.Failed)
                    return;

                request.Status = SongRequestStatus.Downloading;
                request.StatusDetail = "0%";
                request.DownloadProgress = 0f;
                _version++;
            }

            _log.Info($"Song request accepted: {request.Key} by {request.RequesterName}.");
            var pipeline = _pipeline;
            if (pipeline == null)
            {
                MarkFailed(request.Id, "installer unavailable");
                return;
            }
            pipeline.Accept(request);
        }

        /// <summary>Main-thread: deny a request — remove without downloading.</summary>
        public void Deny(string requestId)
        {
            lock (_lock)
            {
                var pending = _queue.Find(r => r.Id == requestId);
                if (pending != null &&
                    (pending.Status == SongRequestStatus.Downloading ||
                     pending.Status == SongRequestStatus.Installing))
                    return;
            }

            var request = Remove(requestId);
            if (request == null)
                return;
            _log.Info($"Song request denied: {request.Key} by {request.RequesterName}.");
        }

        internal void CompleteAccepted(string requestId)
        {
            var request = Remove(requestId);
            if (request == null)
                return;
            RaiseNotice($"Map ready: {request.SongName} (requested by {request.RequesterName})");
            RaiseAccepted();
        }

        /// <summary>Removes the entry by id and bumps the version. Returns it, or null.</summary>
        public SongRequest? Remove(string requestId)
        {
            SongRequest? removed = null;
            lock (_lock)
            {
                var idx = _queue.FindIndex(r => r.Id == requestId);
                if (idx >= 0)
                {
                    removed = _queue[idx];
                    _queue.RemoveAt(idx);
                    _notFoundSinceUtc.Remove(requestId);
                    _version++;
                }
            }
            if (removed != null)
                _beatSaver?.Forget(requestId);
            if (removed != null)
                PersistQueue();
            return removed;
        }

        /// <summary>Any thread: update an entry's status; bumps the version.</summary>
        public void UpdateStatus(string requestId, SongRequestStatus status, string detail = "")
        {
            lock (_lock)
            {
                var request = _queue.Find(r => r.Id == requestId);
                if (request == null)
                    return;
                request.Status = status;
                request.StatusDetail = detail;
                if (status != SongRequestStatus.Downloading)
                    request.DownloadProgress = 0f;
                if (status == SongRequestStatus.NotFound)
                    _notFoundSinceUtc[request.Id] = DateTime.UtcNow;
                else
                    _notFoundSinceUtc.Remove(request.Id);
                _version++;
            }
        }

        /// <summary>Any thread: update the active download's progress; bumps the version.</summary>
        internal void UpdateProgress(string requestId, double progress, string detail)
        {
            lock (_lock)
            {
                var request = _queue.Find(r => r.Id == requestId);
                if (request == null)
                    return;

                var clamped = (float)Math.Max(0d, Math.Min(1d, progress));
                var changedEnough = Math.Abs(request.DownloadProgress - clamped) >= 0.01f;
                if (request.Status != SongRequestStatus.Downloading)
                {
                    request.Status = SongRequestStatus.Downloading;
                    changedEnough = true;
                }
                if (!string.Equals(request.StatusDetail, detail, StringComparison.Ordinal))
                    changedEnough = true;

                request.DownloadProgress = clamped;
                request.StatusDetail = detail;
                if (changedEnough)
                    _version++;
            }
        }

        internal void MarkFailed(string requestId, string detail)
        {
            UpdateStatus(requestId, SongRequestStatus.Failed, string.IsNullOrEmpty(detail) ? "failed" : detail);
        }

        internal void MarkReady(string requestId, string detail)
        {
            UpdateStatus(requestId, SongRequestStatus.Ready, detail);
        }

        private bool IsAllowed(ChatMessage message)
        {
            switch (_config.WhoCanRequest)
            {
                case 2: return message.IsBroadcaster;                       // Broadcaster only
                case 1: return message.IsBroadcaster || message.IsModerator; // Mods + broadcaster
                default: return true;                                       // Everyone
            }
        }

        private static string UserKey(ChatMessage message) =>
            message.Platform + ":" + message.Author.ToLowerInvariant();

        private static string UserKey(SongRequest request) =>
            request.RequesterPlatform + ":" + request.RequesterName.ToLowerInvariant();

        private static double NextJitter()
        {
            lock (_jitterRng)
                return _jitterRng.NextDouble();
        }

        private static DateTime NormalizeUtc(DateTime value)
        {
            if (value == default(DateTime))
                return DateTime.UtcNow;
            return value.Kind == DateTimeKind.Utc
                ? value
                : DateTime.SpecifyKind(value, value.Kind == DateTimeKind.Unspecified
                    ? DateTimeKind.Utc
                    : value.Kind).ToUniversalTime();
        }

        private static string NoticeKey(string kind, ChatMessage message) =>
            kind + ":" + UserKey(message);

        private static string NoticeKey(string kind, SongRequest request) =>
            kind + ":" + UserKey(request);

        public void SaveQueue() => PersistQueue(blocking: true);

        /// <summary>Cancels in-flight metadata fetches/retries on plugin exit and
        /// releases their synchronization primitives.</summary>
        public void Shutdown()
        {
            try { _lifetime.Cancel(); }
            catch (ObjectDisposedException) { /* already shut down */ }
            // Cancel first, then dispose. Any fetch still touching these unwinds via
            // OperationCanceledException (or the async-void catch-all); Dispose on
            // both types is idempotent.
            _lifetime.Dispose();
            _metadataGate.Dispose();
        }

        private void PersistQueue(bool blocking = false)
        {
            var storage = _storage;
            if (storage == null)
                return;
            if (!_config.PersistRequestQueue)
            {
                storage.Clear();
                return;
            }

            List<SongRequest> snapshot;
            int version;
            lock (_lock)
            {
                snapshot = new List<SongRequest>(_queue.Count);
                foreach (var request in _queue)
                    snapshot.Add(request.Snapshot());
                version = _version;
            }

            // Shutdown must finish writing before the process exits; routine saves
            // move the file I/O off the Unity main thread. Either way the version
            // stamp makes Save reject any snapshot older than the last one written,
            // so an out-of-order background completion can't persist a stale queue.
            if (blocking)
                storage.Save(snapshot, version);
            else
                Task.Run(() => storage.Save(snapshot, version));
        }

        private void RaiseNotice(string text, string? rateLimitKey = null)
        {
            if (rateLimitKey != null && ShouldSuppressNotice(rateLimitKey))
                return;
            try { Notice?.Invoke(text); }
            catch (Exception ex) { _log.Warn($"Song-request notice failed: {ex.Message}"); }
        }

        private bool ShouldSuppressNotice(string rateLimitKey)
        {
            var now = DateTime.UtcNow;
            lock (_lock)
            {
                if (_lastNoticeUtc.TryGetValue(rateLimitKey, out var last) &&
                    (now - last).TotalSeconds < NoticeRateLimitSeconds)
                    return true;
                _lastNoticeUtc[rateLimitKey] = now;
                return false;
            }
        }

        private void RaiseAccepted()
        {
            try { Accepted?.Invoke(); }
            catch (Exception ex) { _log.Warn($"Song-request accepted callback failed: {ex.Message}"); }
        }

        private void RaiseAdded()
        {
            try { Added?.Invoke(); }
            catch (Exception ex) { _log.Warn($"Song-request added callback failed: {ex.Message}"); }
        }
    }
}
