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

        public void Save(IEnumerable<SongRequest> requests)
        {
            lock (_ioLock)
            {
                var path = GetPath();
                try
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
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
                    var tempPath = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
                    File.WriteAllText(tempPath, json);
                    // Atomic swap: never delete the existing file before the
                    // replacement is in place, so a crash mid-save can't leave the
                    // only good queue file gone.
                    if (File.Exists(path))
                        File.Replace(tempPath, path, null);
                    else
                        File.Move(tempPath, path);
                }
                catch (Exception ex)
                {
                    _log.Warn($"Could not save request queue: {ex.Message}");
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
