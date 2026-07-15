using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace SimpleBSStreamChat.Chat.YouTube
{
    /// <summary>
    /// Minimal client for YouTube's public InnerTube live-chat endpoint — the same
    /// API the browser's popout chat uses. No API key from the user, no login, no
    /// quota for public streams. Deliberately free of IPA/Unity dependencies so it
    /// can be exercised from a plain console harness and unit tests.
    /// </summary>
    public class InnerTubeChatSession
    {
        private const string UserAgent =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
        private const int MaxSeenMessageIds = 2048;

        private readonly HttpClient _http;
        private readonly HashSet<string> _seenMessageIds;
        private readonly Queue<string> _seenMessageIdOrder;
        private string? _apiKey;
        private string? _clientVersion;
        private string? _continuation;

        public string? VideoId { get; private set; }
        public string? ApiKey => _apiKey;
        public string? ClientVersion => _clientVersion;

        /// <summary>Diagnostic hook; wired to the debug log in-game and to stdout in the harness.</summary>
        public event Action<string>? Trace;

        public InnerTubeChatSession(HttpClient http)
            : this(http, new HashSet<string>(), new Queue<string>())
        {
        }

        // Sharing the seen-id set across sessions lets dedup survive reconnects, so
        // a re-bootstrapped session doesn't replay the recent messages YouTube puts
        // into a fresh continuation's first poll.
        public InnerTubeChatSession(HttpClient http, HashSet<string> seenMessageIds,
            Queue<string> seenMessageIdOrder)
        {
            _http = http;
            _seenMessageIds = seenMessageIds;
            _seenMessageIdOrder = seenMessageIdOrder;
        }

        public static HttpClient CreateHttpClient()
        {
            var handler = new HttpClientHandler { UseCookies = false };
            try { handler.AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate; }
            catch { /* not supported on some runtimes; plain responses still work */ }

            var http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(15) };
            http.DefaultRequestHeaders.TryAddWithoutValidation("User-Agent", UserAgent);
            http.DefaultRequestHeaders.TryAddWithoutValidation("Accept-Language", "en-US,en;q=0.9");
            // Pre-accepted consent cookies so EU users don't get the consent interstitial.
            http.DefaultRequestHeaders.TryAddWithoutValidation("Cookie", "CONSENT=YES+cb; SOCS=CAI");
            return http;
        }

        public static bool LooksLikeVideoId(string value) =>
            Regex.IsMatch(value, "^[0-9A-Za-z_-]{11}$");

        public static string BuildLiveUrl(string channel)
        {
            var c = channel.Trim();
            if (c.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ||
                c.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
                return c;
            if (Regex.IsMatch(c, "^UC[0-9A-Za-z_-]{22}$"))
                return "https://www.youtube.com/channel/" + c + "/live";
            if (!c.StartsWith("@"))
                c = "@" + c;
            return "https://www.youtube.com/" + c + "/live";
        }

        /// <summary>Returns the video id of the channel's current livestream, or null if not live.</summary>
        public async Task<string?> ResolveLiveVideoIdAsync(string channel, CancellationToken ct)
        {
            var url = BuildLiveUrl(channel);
            Trace?.Invoke("GET " + url);
            var html = await GetStringAsync(url, ct).ConfigureAwait(false);

            var match = Regex.Match(html,
                "<link rel=\"canonical\" href=\"https://www\\.youtube\\.com/watch\\?v=([0-9A-Za-z_-]{11})\"");
            return match.Success ? match.Groups[1].Value : null;
        }

        /// <summary>Loads the popout chat page and extracts everything needed to poll.</summary>
        public async Task<bool> StartAsync(string videoId, CancellationToken ct)
        {
            VideoId = videoId;
            _apiKey = null;
            _clientVersion = null;
            _continuation = null;
            // Do NOT clear the seen-id collections here: they are deliberately
            // shared across (re)connected sessions so a re-bootstrap does not
            // replay the recent backlog YouTube returns in a fresh continuation's
            // first poll. Clearing them on every bootstrap is what defeated
            // reconnect dedup and re-ran !request commands. They are bounded to
            // MaxSeenMessageIds, so keeping them costs nothing.

            var url = "https://www.youtube.com/live_chat?is_popout=1&v=" + videoId;
            Trace?.Invoke("GET " + url);
            var html = await GetStringAsync(url, ct).ConfigureAwait(false);

            _apiKey = ExtractBetween(html, "\"INNERTUBE_API_KEY\":\"", "\"");
            _clientVersion = ExtractBetween(html, "\"INNERTUBE_CONTEXT_CLIENT_VERSION\":\"", "\"")
                             ?? ExtractBetween(html, "\"clientVersion\":\"", "\"");

            var initialDataJson = ExtractYtInitialData(html);
            if (_apiKey == null || _clientVersion == null || initialDataJson == null)
            {
                Trace?.Invoke($"bootstrap failed: apiKey={_apiKey != null} clientVersion={_clientVersion != null} initialData={initialDataJson != null}");
                return false;
            }

            _continuation = FindInitialContinuation(JObject.Parse(initialDataJson));
            Trace?.Invoke("bootstrap: continuation " + (_continuation != null ? "ok" : "missing"));
            return _continuation != null;
        }

        /// <summary>One get_live_chat round. Null result means the session was never started.</summary>
        public async Task<InnerTubePollResult?> PollAsync(CancellationToken ct)
        {
            if (_apiKey == null || _clientVersion == null || _continuation == null)
                return null;

            var url = "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?key=" + _apiKey + "&prettyPrint=false";
            var body = new JObject
            {
                ["context"] = new JObject
                {
                    ["client"] = new JObject
                    {
                        ["clientName"] = "WEB",
                        ["clientVersion"] = _clientVersion,
                        ["hl"] = "en",
                        ["gl"] = "US",
                    },
                },
                ["continuation"] = _continuation,
            };

            using (var request = new HttpRequestMessage(HttpMethod.Post, url))
            {
                request.Content = new StringContent(body.ToString(Formatting.None), Encoding.UTF8, "application/json");
                using (var response = await _http.SendAsync(request, ct).ConfigureAwait(false))
                {
                    if (!response.IsSuccessStatusCode)
                    {
                        Trace?.Invoke($"poll HTTP {(int)response.StatusCode}");
                        return new InnerTubePollResult { ContinuationValid = false };
                    }
                    var json = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                    return ParsePollResponse(JObject.Parse(json));
                }
            }
        }

        internal InnerTubePollResult ParsePollResponse(JObject root)
        {
            var result = new InnerTubePollResult();
            if (!(root["continuationContents"]?["liveChatContinuation"] is JObject liveChat))
            {
                result.ContinuationValid = false;
                return result;
            }
            result.ContinuationValid = true;

            var continuations = liveChat["continuations"] as JArray;
            var head = continuations != null && continuations.Count > 0 ? continuations[0] : null;
            string? nextContinuation = null;
            if (head != null)
            {
                foreach (var key in new[] { "invalidationContinuationData", "timedContinuationData", "reloadContinuationData" })
                {
                    var data = head[key];
                    if (data == null)
                        continue;
                    nextContinuation = data["continuation"]?.ToString();
                    var timeout = data["timeoutMs"]?.ToObject<int?>();
                    if (timeout.HasValue)
                        result.TimeoutMs = timeout.Value;
                    break;
                }
            }
            if (nextContinuation != null)
                _continuation = nextContinuation;
            else
                result.ContinuationValid = false; // chat ended or format changed → caller re-bootstraps

            if (liveChat["actions"] is JArray actions)
            {
                foreach (var action in actions)
                {
                    var item = action["addChatItemAction"]?["item"];
                    if (item == null)
                        continue;
                    var message = ParseItem(item, out var messageId);
                    if (message != null)
                    {
                        if (!string.IsNullOrEmpty(messageId) && !RememberMessageId(messageId!))
                            continue;
                        result.Messages.Add(message);
                    }
                }
            }
            return result;
        }

        internal static string? FindInitialContinuation(JObject root)
        {
            var liveChatRenderer = root["contents"]?["liveChatRenderer"];
            if (liveChatRenderer == null)
                return null;

            // Prefer the "Live chat" (unfiltered) view over the default "Top chat".
            // If the titles are localized, YouTube still orders the submenu as
            // Top chat then Live chat, so use the last submenu continuation
            // before falling back to the renderer default continuation.
            if (liveChatRenderer["header"]?["liveChatHeaderRenderer"]?["viewSelector"]?["sortFilterSubMenuRenderer"]?["subMenuItems"] is JArray subMenuItems)
            {
                var submenuContinuations = new List<string>();
                foreach (var entry in subMenuItems)
                {
                    var continuation = entry["continuation"]?["reloadContinuationData"]?["continuation"]?.ToString();
                    if (continuation != null)
                        submenuContinuations.Add(continuation);

                    var title = entry["title"]?.ToString() ?? "";
                    if (title.IndexOf("live", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        if (continuation != null)
                            return continuation;
                    }
                }
                if (submenuContinuations.Count > 1)
                    return submenuContinuations[submenuContinuations.Count - 1];
                if (submenuContinuations.Count == 1)
                    return submenuContinuations[0];
            }

            if (liveChatRenderer["continuations"] is JArray continuations && continuations.Count > 0)
            {
                var head = continuations[0];
                foreach (var key in new[] { "invalidationContinuationData", "timedContinuationData", "reloadContinuationData" })
                {
                    var continuation = head[key]?["continuation"]?.ToString();
                    if (continuation != null)
                        return continuation;
                }
            }
            return null;
        }

        private bool RememberMessageId(string messageId)
        {
            if (!_seenMessageIds.Add(messageId))
                return false;

            _seenMessageIdOrder.Enqueue(messageId);
            while (_seenMessageIdOrder.Count > MaxSeenMessageIds)
                _seenMessageIds.Remove(_seenMessageIdOrder.Dequeue());
            return true;
        }

        private static ChatMessage? ParseItem(JToken item, out string? messageId)
        {
            messageId = null;
            var renderer = item["liveChatTextMessageRenderer"];
            var isPaid = false;
            var paidAmount = "";
            if (renderer == null)
            {
                renderer = item["liveChatPaidMessageRenderer"];
                if (renderer == null)
                    return null; // memberships, mode changes, deletions — ignored for now
                isPaid = true;
                paidAmount = renderer["purchaseAmountText"]?["simpleText"]?.ToString() ?? "";
            }
            messageId = renderer["id"]?.ToString();

            var author = renderer["authorName"]?["simpleText"]?.ToString() ?? "unknown";
            var segments = RunsToSegments(renderer["message"]?["runs"] as JArray);
            var text = ChatMessage.TextFromSegments(segments);
            if (isPaid)
                text = ("★ " + paidAmount + " " + text).TrimEnd();

            if (isPaid)
            {
                var prefix = ("* " + paidAmount + " ").TrimEnd() + " ";
                segments.Insert(0, new TextSegment(prefix));
                text = ChatMessage.TextFromSegments(segments).TrimEnd();
            }
            if (string.IsNullOrEmpty(messageId))
            {
                var timestamp = renderer["timestampUsec"]?.ToString();
                if (!string.IsNullOrEmpty(timestamp))
                    messageId = "fallback:" + timestamp + ":" + author + ":" + text;
            }

            var isOwner = false;
            var isModerator = false;
            if (renderer["authorBadges"] is JArray badges)
            {
                foreach (var badge in badges)
                {
                    var iconType = badge["liveChatAuthorBadgeRenderer"]?["icon"]?["iconType"]?.ToString();
                    if (iconType == "OWNER") isOwner = true;
                    else if (iconType == "MODERATOR") isModerator = true;
                }
            }

            var color = isOwner ? "#FFD600" : isModerator ? "#5E84F1" : NameColors.For(author);
            return new ChatMessage(ChatPlatform.YouTube, author, color, isOwner, isModerator, text, false, segments);
        }

        private static List<ChatSegment> RunsToSegments(JArray? runs)
        {
            var segments = new List<ChatSegment>();
            if (runs == null)
                return segments;
            foreach (var run in runs)
            {
                var text = run["text"]?.ToString();
                if (text != null)
                {
                    segments.Add(new TextSegment(text));
                    continue;
                }
                var emoji = run["emoji"];
                if (emoji == null)
                    continue;
                if (emoji["isCustomEmoji"]?.ToObject<bool>() == true)
                {
                    // Custom channel emotes have no unicode form — show their :shortcut:.
                    var shortcuts = emoji["shortcuts"] as JArray;
                    var name = shortcuts != null && shortcuts.Count > 0 ? shortcuts[0].ToString() : ":emote:";
                    var imageUrl = BestThumbnailUrl(emoji["image"]?["thumbnails"] as JArray);
                    var id = emoji["emojiId"]?.ToString();
                    if (!string.IsNullOrEmpty(imageUrl))
                    {
                        segments.Add(new EmoteSegment(new EmoteRef(
                            "yt:" + StableKey(id ?? imageUrl!),
                            imageUrl!,
                            name,
                            EmoteFormat.Unknown)));
                    }
                    else
                    {
                        segments.Add(new TextSegment(name));
                    }
                }
                else
                {
                    var emojiId = emoji["emojiId"]?.ToString();
                    if (!string.IsNullOrEmpty(emojiId))
                    {
                        segments.Add(new TextSegment(emojiId!));
                        continue;
                    }

                    var imageUrl = BestThumbnailUrl(emoji["image"]?["thumbnails"] as JArray);
                    var shortcuts = emoji["shortcuts"] as JArray;
                    var name = shortcuts != null && shortcuts.Count > 0 ? shortcuts[0].ToString() : ":emoji:";
                    if (!string.IsNullOrEmpty(imageUrl))
                    {
                        segments.Add(new EmoteSegment(new EmoteRef(
                            "ytstd:" + StableKey(imageUrl!),
                            imageUrl!,
                            name,
                            EmoteFormat.Unknown)));
                    }
                    else
                    {
                        segments.Add(new TextSegment(name));
                    }
                }
            }
            return segments;
        }

        private static string? BestThumbnailUrl(JArray? thumbnails)
        {
            if (thumbnails == null || thumbnails.Count == 0)
                return null;
            var best = thumbnails[thumbnails.Count - 1]?["url"]?.ToString();
            return string.IsNullOrWhiteSpace(best) ? null : best;
        }

        private static string StableKey(string value)
        {
            var builder = new StringBuilder(value.Length);
            foreach (var c in value)
            {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-')
                    builder.Append(c);
                else
                    builder.Append(((int)c).ToString("x"));
            }
            return builder.Length == 0 ? "unknown" : builder.ToString();
        }

        internal static string? ExtractYtInitialData(string html)
        {
            foreach (var marker in new[] { "window[\"ytInitialData\"] = ", "var ytInitialData = " })
            {
                var start = html.IndexOf(marker, StringComparison.Ordinal);
                if (start < 0)
                    continue;
                start += marker.Length;
                var end = html.IndexOf(";</script>", start, StringComparison.Ordinal);
                if (end < 0)
                    continue;
                return html.Substring(start, end - start);
            }
            return null;
        }

        internal static string? ExtractBetween(string text, string prefix, string suffix)
        {
            var start = text.IndexOf(prefix, StringComparison.Ordinal);
            if (start < 0)
                return null;
            start += prefix.Length;
            var end = text.IndexOf(suffix, start, StringComparison.Ordinal);
            return end < 0 ? null : text.Substring(start, end - start);
        }

        private async Task<string> GetStringAsync(string url, CancellationToken ct)
        {
            using (var response = await _http.GetAsync(url, ct).ConfigureAwait(false))
            {
                response.EnsureSuccessStatusCode();
                return await response.Content.ReadAsStringAsync().ConfigureAwait(false);
            }
        }
    }

    public class InnerTubePollResult
    {
        public List<ChatMessage> Messages { get; } = new List<ChatMessage>();
        public int TimeoutMs { get; set; } = 2000;
        public bool ContinuationValid { get; set; }
    }
}
