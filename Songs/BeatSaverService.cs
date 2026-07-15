using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using BeatSaverSharp;
using BeatSaverSharp.Models;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Songs
{
    /// <summary>Plain metadata fields the request UI needs (decoupled from BeatSaverSharp types).</summary>
    public readonly struct MapInfo
    {
        public readonly string SongName;
        public readonly string SongAuthorName;
        public readonly string LevelAuthorName;
        public readonly string Hash;
        public readonly string CoverUrl;
        public readonly int DurationSeconds;

        public MapInfo(string songName, string songAuthorName, string levelAuthorName,
            string hash, string coverUrl, int durationSeconds)
        {
            SongName = songName;
            SongAuthorName = songAuthorName;
            LevelAuthorName = levelAuthorName;
            Hash = hash;
            CoverUrl = coverUrl;
            DurationSeconds = durationSeconds;
        }
    }

    public readonly struct HashMapInfo
    {
        public readonly string Key;
        public readonly string CoverUrl;

        public HashMapInfo(string key, string coverUrl)
        {
            Key = key;
            CoverUrl = coverUrl;
        }
    }

    /// <summary>Why a metadata fetch produced no map, so callers can tell a real
    /// 404 (purge) from a transient failure (keep/retry) or cancellation.</summary>
    public enum MapFetchOutcome
    {
        Found,
        NotFound,   // BeatSaver returned 404: the key/hash does not exist
        Error,      // network / rate-limit / server error: retryable, not a 404
        Cancelled,  // the fetch was cancelled (shutdown)
    }

    public readonly struct MapFetchResult
    {
        public readonly MapFetchOutcome Outcome;
        public readonly MapInfo Info;

        private MapFetchResult(MapFetchOutcome outcome, MapInfo info)
        {
            Outcome = outcome;
            Info = info;
        }

        public static MapFetchResult Found(MapInfo info) => new MapFetchResult(MapFetchOutcome.Found, info);
        public static readonly MapFetchResult NotFound = new MapFetchResult(MapFetchOutcome.NotFound, default);
        public static readonly MapFetchResult Error = new MapFetchResult(MapFetchOutcome.Error, default);
        public static readonly MapFetchResult Cancelled = new MapFetchResult(MapFetchOutcome.Cancelled, default);
    }

    /// <summary>
    /// Thin BeatSaverSharp wrapper. Fetches map metadata by key (async, off the
    /// main thread) and caches the <see cref="Beatmap"/> per request id so a later
    /// download (P3) reuses the same object. Returns null for "not found or error"
    /// — P2 treats both the same; a retry/error split is P5 hardening.
    /// </summary>
    public class BeatSaverService : IDisposable
    {
        private readonly BeatSaver _client;
        private readonly ConcurrentDictionary<string, Beatmap> _cache = new ConcurrentDictionary<string, Beatmap>();
        // hash (lower-case) -> BeatSaver key + cover URL, for the map overlay.
        private readonly ConcurrentDictionary<string, HashMapInfo> _infoByHash = new ConcurrentDictionary<string, HashMapInfo>();
        private readonly IPALogger _log;

        public BeatSaverService(IPALogger log)
        {
            _log = log;
            var version = typeof(Plugin).Assembly.GetName().Version ?? new Version(1, 0, 0);
            _client = new BeatSaver("SimpleBSStreamChat", version);
        }

        /// <summary>
        /// Fetches metadata for <paramref name="key"/>. Distinguishes a genuine
        /// 404 (<see cref="MapFetchOutcome.NotFound"/>) from a transient network/
        /// server failure (<see cref="MapFetchOutcome.Error"/>) and cancellation,
        /// so a BeatSaver outage is not mistaken for a missing map and purged.
        /// BeatSaverSharp returns null only for a 404 and throws for transport/
        /// server errors, which is the split relied on here.
        /// </summary>
        public async Task<MapFetchResult> FetchByKeyAsync(string requestId, string key, CancellationToken token)
        {
            try
            {
                var beatmap = await _client.Beatmap(key, token).ConfigureAwait(false);
                if (beatmap == null)
                    return MapFetchResult.NotFound; // 404: the key does not exist

                var version = beatmap.LatestVersion;
                if (version is null) // BeatmapVersion overloads ==; `is null` avoids it
                    return MapFetchResult.NotFound;

                _cache[requestId] = beatmap;
                var meta = beatmap.Metadata;
                return MapFetchResult.Found(new MapInfo(
                    meta?.SongName ?? beatmap.Name ?? "",
                    meta?.SongAuthorName ?? "",
                    meta?.LevelAuthorName ?? "",
                    version.Hash ?? "",
                    NormalizeCoverUrl(version.CoverURL),
                    meta?.Duration ?? 0));
            }
            catch (OperationCanceledException)
            {
                return MapFetchResult.Cancelled;
            }
            catch (Exception ex)
            {
                _log.Warn($"BeatSaver lookup for '{key}' failed: {ex.Message}");
                return MapFetchResult.Error; // retryable; do not treat as not-found
            }
        }

        /// <summary>
        /// Resolves a local song's BeatSaver key and cover URL from its content
        /// hash. Returns null for OST/unpublished maps or on error, and caches
        /// both hits and misses so repeated selections don't re-hit the API.
        /// </summary>
        public async Task<HashMapInfo?> FetchByHashAsync(string hash, CancellationToken token)
        {
            if (string.IsNullOrEmpty(hash))
                return null;
            var normalized = hash.ToLowerInvariant();
            if (_infoByHash.TryGetValue(normalized, out var cached))
                return HasHashInfo(cached) ? cached : (HashMapInfo?)null;

            try
            {
                var beatmap = await _client.BeatmapByHash(normalized, token).ConfigureAwait(false);
                var version = beatmap is null ? null : beatmap.LatestVersion;
                var info = new HashMapInfo(
                    beatmap?.ID ?? "",
                    NormalizeCoverUrl(version is null ? null : version.CoverURL));
                _infoByHash[normalized] = info;
                return HasHashInfo(info) ? info : (HashMapInfo?)null;
            }
            catch (OperationCanceledException)
            {
                return null;
            }
            catch (Exception ex)
            {
                _log.Warn($"BeatSaver hash lookup for '{normalized}' failed: {ex.Message}");
                return null;
            }
        }

        /// <summary>The cached <see cref="Beatmap"/> for a request (used by the P3 downloader).</summary>
        public Beatmap? GetCached(string requestId) =>
            _cache.TryGetValue(requestId, out var beatmap) ? beatmap : null;

        /// <summary>Drops a request's cached beatmap (on deny/remove).</summary>
        public void Forget(string requestId) => _cache.TryRemove(requestId, out _);

        // The API returns absolute cdn URLs, but guard the relative case just in case.
        private static string NormalizeCoverUrl(string? url)
        {
            if (string.IsNullOrEmpty(url))
                return "";
            if (url!.StartsWith("/"))
                return "https://cdn.beatsaver.com" + url;
            return url;
        }

        public void Dispose()
        {
            _cache.Clear();
            _infoByHash.Clear();
            try { _client.Dispose(); }
            catch (Exception ex) { _log.Warn($"BeatSaver client dispose failed: {ex.Message}"); }
        }

        private static bool HasHashInfo(HashMapInfo info) =>
            !string.IsNullOrEmpty(info.Key) || !string.IsNullOrEmpty(info.CoverUrl);
    }
}
