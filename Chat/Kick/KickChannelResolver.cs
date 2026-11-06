using System;
using System.Diagnostics;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.Kick
{
    internal sealed class KickChannelInfo
    {
        // The realtime connection lookup takes this; it also accepts a chatroom id.
        public int ChannelId { get; }
        public int ChatroomId { get; }
        public int ViewerCount { get; }
        public string DisplayName { get; }

        public KickChannelInfo(int channelId, int chatroomId, int viewerCount, string displayName)
        {
            ChannelId = channelId;
            ChatroomId = chatroomId;
            ViewerCount = viewerCount;
            DisplayName = displayName;
        }
    }

    internal static class KickChannelResolver
    {
        private const string BrowserUserAgent =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
            "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
        private static readonly object CacheLock = new object();
        private static readonly TimeSpan FreshCacheDuration = TimeSpan.FromSeconds(20);
        private static readonly TimeSpan StaleCacheFallbackDuration = TimeSpan.FromMinutes(30);
        private static string? _cachedChannel;
        private static KickChannelInfo? _cachedInfo;
        private static DateTime _cachedAtUtc;

        public static async Task<KickChannelInfo?> ResolveAsync(string channel, HttpClient http, IPALogger log,
            CancellationToken cancellationToken)
        {
            var normalized = NormalizeChannel(channel);
            if (string.IsNullOrWhiteSpace(normalized))
                return null;

            if (int.TryParse(normalized, out var chatroomId) && chatroomId > 0)
                return new KickChannelInfo(chatroomId, chatroomId, -1, normalized);

            if (TryGetCached(normalized, FreshCacheDuration, out var cached))
                return cached;

            var httpResult = await FetchWithHttpAsync(normalized, http, log, cancellationToken).ConfigureAwait(false);
            var json = httpResult.Json;
            if (json == null && httpResult.AllowCurlFallback)
                json = await FetchWithCurlAsync(normalized, log, cancellationToken).ConfigureAwait(false);
            if (json == null)
                return TryGetCached(normalized, StaleCacheFallbackDuration, out cached) ? cached : null;

            var info = ParseChannelInfo(json, normalized, log);
            if (info != null)
                Cache(normalized, info);
            return info ?? (TryGetCached(normalized, StaleCacheFallbackDuration, out cached) ? cached : null);
        }

        internal static bool IsNumericChatroomId(string channel)
        {
            var normalized = NormalizeChannel(channel);
            return int.TryParse(normalized, out var chatroomId) && chatroomId > 0;
        }

        private static string NormalizeChannel(string channel)
        {
            var value = (channel ?? "").Trim();
            if (value.Length == 0)
                return "";

            if (Uri.TryCreate(value, UriKind.Absolute, out var uri))
            {
                var parts = uri.AbsolutePath.Split(new[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
                value = parts.Length == 0 ? "" : parts[0];
            }

            value = value.Trim().TrimStart('@').TrimStart('#');
            if (value.StartsWith("chatroom:", StringComparison.OrdinalIgnoreCase))
                value = value.Substring("chatroom:".Length);
            return value;
        }

        private static async Task<HttpFetchResult> FetchWithHttpAsync(string slug, HttpClient http, IPALogger log,
            CancellationToken cancellationToken)
        {
            try
            {
                using (var request = new HttpRequestMessage(HttpMethod.Get,
                           "https://kick.com/api/v2/channels/" + Uri.EscapeDataString(slug)))
                {
                    request.Headers.TryAddWithoutValidation("User-Agent", BrowserUserAgent);
                    request.Headers.TryAddWithoutValidation("Accept", "application/json");
                    request.Headers.TryAddWithoutValidation("Accept-Language", "en-US,en;q=0.9");
                    request.Headers.TryAddWithoutValidation("Referer", "https://kick.com/" + slug);
                    using (var response = await http.SendAsync(request, cancellationToken).ConfigureAwait(false))
                    {
                        if (!response.IsSuccessStatusCode)
                        {
                            log.Debug($"Kick: channel lookup HTTP {(int)response.StatusCode}");
                            return HttpFetchResult.Empty(ShouldTryCurlForStatus((int)response.StatusCode));
                        }

                        var body = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                        if (string.IsNullOrWhiteSpace(body) || !LooksLikeJson(body))
                        {
                            log.Debug("Kick: channel lookup HTTP did not return JSON");
                            return HttpFetchResult.Empty(allowCurlFallback: true);
                        }

                        return HttpFetchResult.Success(body);
                    }
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return HttpFetchResult.Empty(allowCurlFallback: false);
            }
            catch (TaskCanceledException ex)
            {
                log.Debug("Kick: channel lookup timed out: " + ex.Message);
                return HttpFetchResult.Empty(allowCurlFallback: false);
            }
            catch (HttpRequestException ex)
            {
                log.Debug("Kick: channel lookup failed: " + ex.Message);
                return HttpFetchResult.Empty(allowCurlFallback: true);
            }
            catch (Exception ex)
            {
                log.Debug("Kick: channel lookup failed: " + ex.Message);
                return HttpFetchResult.Empty(allowCurlFallback: false);
            }
        }

        private static Task<string?> FetchWithCurlAsync(string slug, IPALogger log, CancellationToken cancellationToken) =>
            CurlJsonAsync("https://kick.com/api/v2/channels/" + Uri.EscapeDataString(slug), null,
                "https://kick.com/" + slug, "channel lookup", log, cancellationToken);

        /// <summary>One request through Windows' curl.exe, which gets past Kick's
        /// Cloudflare checks where the game's HttpClient is refused. POSTs
        /// <paramref name="postJson"/> when given. Returns the body only if it is JSON.</summary>
        internal static async Task<string?> CurlJsonAsync(string url, string? postJson, string referer, string what,
            IPALogger log, CancellationToken cancellationToken)
        {
            try
            {
                if (cancellationToken.IsCancellationRequested)
                    return null;

                var args = "--silent --show-error --location --compressed --max-time 15 " +
                    "-H " + Quote("Accept: application/json") + " " +
                    "-H " + Quote("Accept-Language: en-US,en;q=0.9") + " " +
                    "-H " + Quote("Referer: " + referer) + " " +
                    "-A " + Quote(BrowserUserAgent) + " ";
                if (postJson != null)
                {
                    // The body goes through stdin to keep JSON quoting off the command line.
                    args += "-H " + Quote("Content-Type: application/json") + " " +
                        "-H " + Quote("Origin: https://kick.com") + " --data-binary @- ";
                }
                args += Quote(url);

                using (var process = new Process())
                {
                    process.StartInfo = new ProcessStartInfo
                    {
                        FileName = "curl.exe",
                        Arguments = args,
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        RedirectStandardInput = postJson != null,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        StandardOutputEncoding = Encoding.UTF8,
                        StandardErrorEncoding = Encoding.UTF8,
                        ErrorDialog = false,
                    };

                    process.Start();
                    if (postJson != null)
                    {
                        // No BOM: curl sends stdin byte for byte.
                        var body = new UTF8Encoding(false).GetBytes(postJson);
                        process.StandardInput.BaseStream.Write(body, 0, body.Length);
                        process.StandardInput.Close();
                    }
                    var outputTask = process.StandardOutput.ReadToEndAsync();
                    var errorTask = process.StandardError.ReadToEndAsync();
                    var exited = await WaitForExitAsync(process, TimeSpan.FromSeconds(20), cancellationToken)
                        .ConfigureAwait(false);
                    if (!exited)
                    {
                        try { process.Kill(); } catch { /* process may have exited */ }
                        if (!cancellationToken.IsCancellationRequested)
                            log.Debug($"Kick: curl {what} timed out");
                        return null;
                    }

                    var output = await outputTask.ConfigureAwait(false);
                    var error = await errorTask.ConfigureAwait(false);
                    if (process.ExitCode != 0)
                    {
                        log.Debug($"Kick: curl {what} failed: " + error.Trim());
                        return null;
                    }

                    if (string.IsNullOrWhiteSpace(output) || !LooksLikeJson(output))
                    {
                        log.Debug($"Kick: curl {what} did not return JSON");
                        return null;
                    }

                    return output;
                }
            }
            catch (OperationCanceledException)
            {
                return null;
            }
            catch (Exception ex)
            {
                log.Debug($"Kick: curl {what} unavailable: " + ex.Message);
                return null;
            }
        }

        private static KickChannelInfo? ParseChannelInfo(string json, string slug, IPALogger log)
        {
            try
            {
                var root = JObject.Parse(json);
                var chatroom = AsObject(root["chatroom"]);
                var livestream = AsObject(root["livestream"]);
                var recentLivestream = AsObject(root["recent_livestream"]);

                var chatroomId = TryInt(chatroom?["id"])
                    ?? TryInt(root["chatroom_id"])
                    ?? TryInt(root["chatroomId"]);
                if (chatroomId == null || chatroomId <= 0)
                    return null;
                var channelId = TryInt(root["id"]) ?? chatroomId.Value;

                var viewerCount = TryInt(livestream?["viewer_count"])
                    ?? TryInt(livestream?["viewers"])
                    ?? TryInt(recentLivestream?["viewer_count"])
                    ?? TryInt(recentLivestream?["viewers"])
                    ?? -1;

                var displayName = FirstString(root["user"], "username", "name")
                    ?? FirstString(root, "user_username", "slug", "username")
                    ?? slug;

                return new KickChannelInfo(channelId, chatroomId.Value, viewerCount, displayName);
            }
            catch (Exception ex)
            {
                log.Debug("Kick: channel lookup JSON parse failed: " + ex.Message);
                return null;
            }
        }

        private static int? TryInt(JToken? token)
        {
            if (token == null || token.Type == JTokenType.Null)
                return null;
            if (token.Type == JTokenType.Integer)
                return token.ToObject<int>();
            return int.TryParse(token.ToString(), out var value) ? value : (int?)null;
        }

        private static string? FirstString(JToken? token, params string[] names)
        {
            var obj = AsObject(token);
            if (obj == null)
                return null;
            foreach (var name in names)
            {
                var value = obj[name]?.ToString();
                if (!string.IsNullOrWhiteSpace(value))
                    return value;
            }
            return null;
        }

        private static JObject? AsObject(JToken? token) => token as JObject;

        internal static bool LooksLikeJson(string value) =>
            Regex.IsMatch(value, @"^\s*[\{\[]", RegexOptions.CultureInvariant);

        internal static bool ShouldTryCurlForStatus(int statusCode) =>
            statusCode == 403 || statusCode == 429 || statusCode == 503 ||
            statusCode == 521 || statusCode == 522 || statusCode == 523 || statusCode == 524;

        private static string Quote(string value) => "\"" + value.Replace("\"", "\\\"") + "\"";

        private static async Task<bool> WaitForExitAsync(Process process, TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var completion = new TaskCompletionSource<bool>();
            EventHandler? handler = null;
            handler = (sender, args) => completion.TrySetResult(true);
            process.Exited += handler;

            try
            {
                process.EnableRaisingEvents = true;
                if (process.HasExited)
                    completion.TrySetResult(true);

                using (cancellationToken.Register(() =>
                {
                    try
                    {
                        if (!process.HasExited)
                            process.Kill();
                    }
                    catch
                    {
                        // The process can exit between HasExited and Kill.
                    }
                    completion.TrySetResult(false);
                }))
                {
                    var completed = await Task.WhenAny(completion.Task,
                            Task.Delay(timeout))
                        .ConfigureAwait(false);
                    if (completed != completion.Task)
                        return false;

                    return await completion.Task.ConfigureAwait(false);
                }
            }
            finally
            {
                process.Exited -= handler;
            }
        }

        private sealed class HttpFetchResult
        {
            private HttpFetchResult(string? json, bool allowCurlFallback)
            {
                Json = json;
                AllowCurlFallback = allowCurlFallback;
            }

            public string? Json { get; }
            public bool AllowCurlFallback { get; }

            public static HttpFetchResult Success(string json) =>
                new HttpFetchResult(json, allowCurlFallback: false);

            public static HttpFetchResult Empty(bool allowCurlFallback) =>
                new HttpFetchResult(null, allowCurlFallback);
        }

        private static void Cache(string channel, KickChannelInfo info)
        {
            lock (CacheLock)
            {
                _cachedChannel = channel;
                _cachedInfo = info;
                _cachedAtUtc = DateTime.UtcNow;
            }
        }

        private static bool TryGetCached(string channel, TimeSpan maxAge, out KickChannelInfo? info)
        {
            lock (CacheLock)
            {
                if (_cachedInfo != null &&
                    string.Equals(_cachedChannel, channel, StringComparison.OrdinalIgnoreCase) &&
                    DateTime.UtcNow - _cachedAtUtc <= maxAge)
                {
                    info = _cachedInfo;
                    return true;
                }
            }

            info = null;
            return false;
        }
    }
}
