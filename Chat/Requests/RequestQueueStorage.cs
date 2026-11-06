using System;
using System.Collections.Generic;
using System.IO;
using IPA.Utilities;
using Newtonsoft.Json;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.Requests
{
    public class RequestQueueStorage
    {
        private const string FileName = "SimpleBSStreamChat.Requests.json";
        private readonly object _ioLock = new object();
        private readonly IPALogger _log;
        // Highest queue version written so far. Routine saves run on background
        // threads and can complete out of order; an older snapshot must never
        // overwrite a newer one.
        private long _lastSavedVersion = long.MinValue;

        public RequestQueueStorage(IPALogger log)
        {
            _log = log;
        }

        public IReadOnlyList<StoredRequest> Load()
        {
            lock (_ioLock)
            {
                var path = GetPath();
                if (!File.Exists(path))
                    return Array.Empty<StoredRequest>();

                try
                {
                    var json = File.ReadAllText(path);
                    var entries = JsonConvert.DeserializeObject<List<StoredRequest>>(json);
                    return entries != null ? entries : Array.Empty<StoredRequest>();
                }
                catch (Exception ex)
                {
                    _log.Warn($"Could not load saved request queue: {ex.Message}");
                }
                return Array.Empty<StoredRequest>();
            }
        }

        public void Save(IEnumerable<SongRequest> requests, long version)
        {
            lock (_ioLock)
            {
                // Drop a payload older than (or equal to) the last one written.
                if (version <= _lastSavedVersion)
                    return;

                var path = GetPath();
                var tempPath = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
                try
                {
                    var directory = Path.GetDirectoryName(path);
                    if (string.IsNullOrEmpty(directory))
                    {
                        _log.Warn("Could not save request queue: user data path has no directory.");
                        return;
                    }
                    Directory.CreateDirectory(directory);

                    var entries = new List<StoredRequest>();
                    foreach (var request in requests)
                    {
                        if (request.Status == SongRequestStatus.NotFound)
                            continue;
                        entries.Add(new StoredRequest
                        {
                            Key = request.Key,
                            RequesterName = request.RequesterName,
                            RequesterPlatform = request.RequesterPlatform,
                            RequestedAtUtc = request.RequestedAtUtc,
                        });
                    }

                    var json = JsonConvert.SerializeObject(entries, Formatting.Indented);
                    File.WriteAllText(tempPath, json);
                    // Atomic swap: never delete the existing file before the
                    // replacement is in place, so a crash mid-save can't leave the
                    // only good queue file gone.
                    if (File.Exists(path))
                        File.Replace(tempPath, path, null);
                    else
                        File.Move(tempPath, path);
                    _lastSavedVersion = version;
                }
                catch (Exception ex)
                {
                    _log.Warn($"Could not save request queue: {ex.Message}");
                }
                finally
                {
                    // Never leave the uniquely-named temp file behind on failure.
                    try
                    {
                        if (File.Exists(tempPath))
                            File.Delete(tempPath);
                    }
                    catch { /* best effort */ }
                }
            }
        }

        public void Clear()
        {
            lock (_ioLock)
            {
                var path = GetPath();
                try
                {
                    if (File.Exists(path))
                        File.Delete(path);
                }
                catch (Exception ex)
                {
                    _log.Warn($"Could not clear saved request queue: {ex.Message}");
                }
            }
        }

        private static string GetPath() => Path.Combine(UnityGame.UserDataPath, FileName);

        public class StoredRequest
        {
            public string Key { get; set; } = "";
            public string RequesterName { get; set; } = "";
            public ChatPlatform RequesterPlatform { get; set; }
            public DateTime RequestedAtUtc { get; set; }
        }
    }
}
