using System;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using SimpleBSStreamChat.Chat.Kick;
using SimpleBSStreamChat.Chat.YouTube;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat
{
    /// <summary>
    /// Polls live viewer counts. Twitch: the public web GQL endpoint (Twitch's
    /// own website client id, no user auth). YouTube: the InnerTube
    /// updated_metadata endpoint using the chat session's key. -1 = unknown/offline.
    ///
    /// Each poll classifies the outcome as Live, Offline, or Unknown. Live and
    /// Offline are settled states polled at the steady cadence; only a genuinely
    /// Unknown (transient/network/malformed) outcome triggers faster retries,
    /// which back off so a failing endpoint is not hammered. Crucially, a stream
    /// that is simply offline is Offline, not Unknown, so it no longer retries
    /// every ~10s forever.
    /// </summary>
    public class ViewerCountTracker : IDisposable
    {
        private const int SteadyPollMs = 30000;
        private const int InitialUnknownPollMs = 10000;
        private const int MaxUnknownPollMs = 60000;

        private enum ViewerStatus { Live, Offline, Unknown }

        private readonly struct ViewerResult
        {
            public readonly int Count;
            public readonly ViewerStatus Status;

            private ViewerResult(int count, ViewerStatus status)
            {
                Count = count;
                Status = status;
            }

            // Offline and Unknown both hide the number (count -1); they differ
            // only in whether the poll cadence should back off (Unknown) or
            // settle (Offline).
            public static ViewerResult Live(int count) => new ViewerResult(count, ViewerStatus.Live);
            public static readonly ViewerResult Offline = new ViewerResult(-1, ViewerStatus.Offline);
            public static readonly ViewerResult Unknown = new ViewerResult(-1, ViewerStatus.Unknown);
        }

        // The id Twitch's own website uses; scraped from twitch.tv at runtime
        // because it rotates. Fallback = value observed 2026-07.
        private const string FallbackTwitchClientId = "kimne78kx3ncx6brgo4mv6wki5h1ko";
        private string? _twitchClientId;

        private readonly PluginConfig _config;
        private readonly Func<InnerTubeChatSession?> _youTubeSession;
        private readonly IPALogger _log;
        private readonly HttpClient _http;
        private Thread? _thread;
        private volatile bool _stopping;
        private volatile int _twitchViewers = -1;
        private volatile int _youTubeViewers = -1;
        private volatile int _kickViewers = -1;

        public int TwitchViewers => _twitchViewers;
        public int YouTubeViewers => _youTubeViewers;
        public int KickViewers => _kickViewers;

        public ViewerCountTracker(PluginConfig config, Func<InnerTubeChatSession?> youTubeSession, IPALogger log)
        {
            _config = config;
            _youTubeSession = youTubeSession;
            _log = log;
            _http = InnerTubeChatSession.CreateHttpClient();
        }

        public void Start()
        {
            if (_thread != null)
                return;
            _stopping = false;
            _thread = new Thread(RunLoop)
            {
                IsBackground = true,
                Name = "SimpleBSStreamChat.ViewerCounts",
            };
            _thread.Start();
        }

        public void Dispose()
        {
            _stopping = true;
            try { _thread?.Join(1000); } catch { /* background thread dies with the process */ }
            _thread = null;
            _http.Dispose();
        }

        private void RunLoop()
        {
            var unknownBackoffMs = InitialUnknownPollMs;
            while (!_stopping)
            {
                var anyUnknown = false;

                if (_config.ShowViewerCounts)
                {
                    if (_config.EnableTwitch && !string.IsNullOrWhiteSpace(_config.TwitchChannel))
                    {
                        var result = SafeFetch(
                            () => FetchTwitchViewers(_config.TwitchChannel.Trim().TrimStart('#').ToLowerInvariant()),
                            "Twitch");
                        _twitchViewers = result.Count;
                        anyUnknown |= result.Status == ViewerStatus.Unknown;
                    }
                    else
                    {
                        _twitchViewers = -1;
                    }

                    var session = _config.EnableYouTube ? _youTubeSession() : null;
                    if (session != null)
                    {
                        var result = SafeFetch(() => FetchYouTubeViewers(session), "YouTube");
                        _youTubeViewers = result.Count;
                        anyUnknown |= result.Status == ViewerStatus.Unknown;
                    }
                    else
                    {
                        _youTubeViewers = -1;
                    }

                    if (_config.EnableKick && !string.IsNullOrWhiteSpace(_config.KickChannel))
                    {
                        var result = SafeFetch(() => FetchKickViewers(_config.KickChannel), "Kick");
                        _kickViewers = result.Count;
                        anyUnknown |= result.Status == ViewerStatus.Unknown;
                    }
                    else
                    {
                        _kickViewers = -1;
                    }
                }

                // Live/offline are settled states polled at the steady cadence.
                // Only a transient Unknown (network/malformed/startup) polls
                // faster, and it backs off so a failing endpoint is not hammered.
                int sleepMs;
                if (anyUnknown)
                {
                    sleepMs = unknownBackoffMs;
                    unknownBackoffMs = Math.Min(unknownBackoffMs * 2, MaxUnknownPollMs);
                }
                else
                {
                    sleepMs = SteadyPollMs;
                    unknownBackoffMs = InitialUnknownPollMs;
                }
                for (var waited = 0; waited < sleepMs && !_stopping; waited += 250)
                    Thread.Sleep(250);
            }
        }

        private ViewerResult SafeFetch(Func<ViewerResult> fetch, string provider)
        {
            try
            {
                return fetch();
            }
            catch (Exception ex)
            {
                _log.Debug($"ViewerCounts/{provider}: {ex.Message}");
                return ViewerResult.Unknown;
            }
        }

        private ViewerResult FetchTwitchViewers(string channel)
        {
            if (_twitchClientId == null)
                _twitchClientId = FetchTwitchClientId() ?? FallbackTwitchClientId;

            var body = new JObject
            {
                ["query"] = "query($login: String!) { user(login: $login) { stream { viewersCount } } }",
                ["variables"] = new JObject { ["login"] = channel },
            };
            using (var request = new HttpRequestMessage(HttpMethod.Post, "https://gql.twitch.tv/gql"))
            {
                request.Headers.TryAddWithoutValidation("Client-Id", _twitchClientId);
                request.Content = new StringContent(body.ToString(Formatting.None), Encoding.UTF8, "application/json");
                using (var response = _http.SendAsync(request).GetAwaiter().GetResult())
                {
                    if ((int)response.StatusCode == 400 || (int)response.StatusCode == 401)
                    {
                        _twitchClientId = null; // rotated — re-scrape next cycle
                        _log.Debug("ViewerCounts/Twitch: client id rejected, will re-scrape");
                        return ViewerResult.Unknown;
                    }
                    if (!response.IsSuccessStatusCode)
                        return ViewerResult.Unknown;

                    var json = JObject.Parse(response.Content.ReadAsStringAsync().GetAwaiter().GetResult());
                    // When the channel doesn't exist `user` is JSON null; when the
                    // channel is offline `stream` is JSON null. Newtonsoft returns a
                    // JValue(Null) for those — indexing into it with a string throws,
                    // which is what previously spammed the log ~200×/session and kept
                    // the poll pinned at the fast cadence. Treat both as a settled
                    // Offline state instead.
                    var data = json["data"];
                    if (IsMissing(data))
                        return ViewerResult.Unknown; // unexpected envelope: retry
                    var user = data!["user"];
                    if (IsMissing(user))
                        return ViewerResult.Offline; // no such channel
                    var stream = user!["stream"];
                    if (IsMissing(stream))
                        return ViewerResult.Offline; // channel offline
                    var count = stream!["viewersCount"]?.ToObject<int?>();
                    return count.HasValue ? ViewerResult.Live(count.Value) : ViewerResult.Offline;
                }
            }
        }

        private static bool IsMissing(JToken? token) =>
            token == null || token.Type == JTokenType.Null;

        // The web client id is embedded in every twitch.tv page.
        private string? FetchTwitchClientId()
        {
            try
            {
                using (var response = _http.GetAsync("https://www.twitch.tv/").GetAwaiter().GetResult())
                {
                    if (!response.IsSuccessStatusCode)
                        return null;
                    var html = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                    var match = Regex.Match(html, "clientId[\"\\s:=]+[\"']([A-Za-z0-9]{20,40})[\"']");
                    if (match.Success)
                    {
                        _log.Debug("ViewerCounts/Twitch: scraped client id");
                        return match.Groups[1].Value;
                    }
                    return null;
                }
            }
            catch (Exception ex)
            {
                _log.Debug("ViewerCounts/Twitch: client id scrape failed: " + ex.Message);
                return null;
            }
        }

        private ViewerResult FetchYouTubeViewers(InnerTubeChatSession session)
        {
            var apiKey = session.ApiKey;
            var clientVersion = session.ClientVersion;
            var videoId = session.VideoId;
            if (apiKey == null || clientVersion == null || videoId == null)
                return ViewerResult.Unknown; // session still bootstrapping

            var body = new JObject
            {
                ["context"] = new JObject
                {
                    ["client"] = new JObject
                    {
                        ["clientName"] = "WEB",
                        ["clientVersion"] = clientVersion,
                        ["hl"] = "en",
                        ["gl"] = "US",
                    },
                },
                ["videoId"] = videoId,
            };
            var url = "https://www.youtube.com/youtubei/v1/updated_metadata?key=" + apiKey + "&prettyPrint=false";
            using (var request = new HttpRequestMessage(HttpMethod.Post, url))
            {
                request.Content = new StringContent(body.ToString(Formatting.None), Encoding.UTF8, "application/json");
                using (var response = _http.SendAsync(request).GetAwaiter().GetResult())
                {
                    if (!response.IsSuccessStatusCode)
                        return ViewerResult.Unknown;
                    var json = JObject.Parse(response.Content.ReadAsStringAsync().GetAwaiter().GetResult());
                    if (!(json["actions"] is JArray actions))
                        return ViewerResult.Offline; // parsed OK but no metadata: stream ended
                    foreach (var action in actions)
                    {
                        var renderer = action["updateViewershipAction"]?["viewCount"]?["videoViewCountRenderer"];
                        if (renderer == null)
                            continue;
                        var viewCount = renderer["viewCount"];
                        var text = viewCount?["simpleText"]?.ToString();
                        if (text == null && viewCount?["runs"] is JArray runs)
                        {
                            var sb = new StringBuilder();
                            foreach (var run in runs)
                                sb.Append(run["text"]?.ToString());
                            text = sb.ToString();
                        }
                        if (text == null)
                            continue;
                        var digits = new StringBuilder();
                        foreach (var c in text)
                            if (c >= '0' && c <= '9')
                                digits.Append(c);
                        if (digits.Length > 0 && int.TryParse(digits.ToString(), out var value))
                            return ViewerResult.Live(value);
                    }
                    return ViewerResult.Offline;
                }
            }
        }

        private ViewerResult FetchKickViewers(string channel)
        {
            var info = KickChannelResolver.ResolveAsync(channel, _http, _log, CancellationToken.None)
                .GetAwaiter().GetResult();
            if (info == null)
                return ViewerResult.Unknown; // resolve failed (network/rate-limit)
            // A numeric chatroom id or an offline channel resolves with no count
            // (-1): a settled Offline state, not something to fast-retry.
            return info.ViewerCount >= 0 ? ViewerResult.Live(info.ViewerCount) : ViewerResult.Offline;
        }
    }
}
