using System;

namespace SimpleBSStreamChat.Chat.Requests
{
    public enum SongRequestStatus
    {
        FetchingInfo, // queued; BeatSaver metadata fetch in flight (Accept disabled)
        Ready,        // metadata present; Accept/Deny enabled
        NotFound,     // bad/deleted key
        Downloading,  // Accept pressed; zip download in progress
        Installing,   // extracted; awaiting SongCore refresh
        Failed,       // download/install failed; Accept = retry, Deny = remove
    }

    /// <summary>
    /// One entry in the song-request queue. The queue position is not stored —
    /// it is the entry's index in the snapshot the UI copies, so positions
    /// renumber automatically when an entry is removed.
    /// </summary>
    public class SongRequest
    {
        public string Id { get; }                 // stable handle for UI callbacks / row identity
        public string Key { get; }                // normalized BeatSaver key (lowercase hex)
        public string RequesterName { get; }
        public ChatPlatform RequesterPlatform { get; }
        public DateTime RequestedAtUtc { get; }

        // Filled by the metadata fetch (faked in P1: SongName = "Map <key>").
        public string SongName { get; internal set; } = "";
        public string SongAuthorName { get; internal set; } = "";
        public string LevelAuthorName { get; internal set; } = "";
        public string Hash { get; internal set; } = "";
        public string CoverUrl { get; internal set; } = "";
        public int DurationSeconds { get; internal set; }
        public SongRequestStatus Status { get; internal set; } = SongRequestStatus.FetchingInfo;
        public string StatusDetail { get; internal set; } = "";
        public float DownloadProgress { get; internal set; }

        public SongRequest(string key, string requesterName, ChatPlatform requesterPlatform)
            : this(key, requesterName, requesterPlatform, DateTime.UtcNow)
        {
        }

        internal SongRequest(string key, string requesterName, ChatPlatform requesterPlatform, DateTime requestedAtUtc)
            : this(Guid.NewGuid().ToString("N"), key, requesterName, requesterPlatform, requestedAtUtc)
        {
        }

        private SongRequest(string id, string key, string requesterName, ChatPlatform requesterPlatform, DateTime requestedAtUtc)
        {
            Id = id;
            Key = key;
            RequesterName = requesterName;
            RequesterPlatform = requesterPlatform;
            RequestedAtUtc = requestedAtUtc;
        }

        internal SongRequest Snapshot() =>
            new SongRequest(Id, Key, RequesterName, RequesterPlatform, RequestedAtUtc)
            {
                SongName = SongName,
                SongAuthorName = SongAuthorName,
                LevelAuthorName = LevelAuthorName,
                Hash = Hash,
                CoverUrl = CoverUrl,
                DurationSeconds = DurationSeconds,
                Status = Status,
                StatusDetail = StatusDetail,
                DownloadProgress = DownloadProgress,
            };
    }
}
