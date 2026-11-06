using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.WebSockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using SimpleBSStreamChat.Chat.YouTube;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.Kick
{
    /// <summary>
    /// Read-only Kick chat client. Kick's official API is app/OAuth oriented;
    /// this follows the web client instead: an anonymous guest token, then the
    /// chatroom's stream on Kick's Centrifugo WebSocket. (Kick moved chat off
    /// Pusher; the old public Pusher app keys no longer connect.)
    /// </summary>
    public class KickLiveChatClient : IChatService
    {
        private const string RealtimeApi = "https://web.kick.com/api/v1/realtime";
        // Used when the realtime lookup fails; it returned this server for every channel tried.
        private const string DefaultRealtimeUrl = "wss://realtime.us-west-2.platform.kick.com/connection/websocket";
        private static readonly Regex EmoteRegex =
            new Regex(@"\[emote:\d+:([^\]]+)\]", RegexOptions.CultureInvariant);
        private static readonly Regex EmoteTokenRegex =
            new Regex(@"\[emote:(\d+):([^\]]+)\]", RegexOptions.CultureInvariant);
        private static readonly Regex HexColorRegex =
            new Regex("^#[0-9A-Fa-f]{6}$", RegexOptions.CultureInvariant);
        private static readonly string[] IdFields = { "id", "message_id" };
        private static readonly string[] MessageTextFields = { "content", "message", "text" };
        private static readonly string[] SenderNameFields = { "username", "name", "slug", "display_name" };
        private static readonly string[] DataAuthorFields = { "username", "sender_username" };
        private static readonly string[] ColorFields = { "color" };
        private static readonly string[] RoleFields = { "role" };
        private static readonly string[] CreatedAtFields = { "created_at" };
        private static readonly string[] BadgeFields = { "type", "slug", "name", "text" };
        private static readonly string[] BroadcasterRoles = { "broadcaster", "owner" };
        private static readonly string[] ModeratorRoles = { "moderator", "mod" };

        public event Action<ChatMessage>? MessageReceived;
        public event Action<string>? StatusChanged;

        private readonly string _channel;
        private readonly IPALogger _log;
        private readonly HttpClient _http;
        private readonly HashSet<string> _seenMessageIds = new HashSet<string>();
        private readonly Queue<string> _seenMessageOrder = new Queue<string>();
        private readonly byte[] _receiveBuffer = new byte[8192];
        private CancellationTokenSource? _cts;
        private Task? _runTask;
        private volatile ClientWebSocket? _socket;
        private int _messageCount;

        public KickLiveChatClient(string channel, IPALogger log)
        {
            _channel = channel.Trim();
            _log = log;
            try { ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12; }
            catch { /* HTTPS already works on supported runtimes. */ }
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

            cts.Cancel();
            try { _socket?.Abort(); } catch { /* already closing */ }

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
                    StatusChanged?.Invoke($"Kick: resolving {_channel}...");
                    var info = await KickChannelResolver.ResolveAsync(_channel, _http, _log, ct).ConfigureAwait(false);
                    if (info == null)
                    {
                        StatusChanged?.Invoke($"Kick: could not resolve {_channel} - retrying in 10s");
                        await Task.Delay(TimeSpan.FromSeconds(10), ct).ConfigureAwait(false);
                        continue;
                    }

                    await ConnectAndReadAsync(info, ct).ConfigureAwait(false);

                    errorDelaySeconds = 2;
                    StatusChanged?.Invoke($"Kick: disconnected - reconnecting in {errorDelaySeconds}s");
                    await Task.Delay(TimeSpan.FromSeconds(errorDelaySeconds), ct).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception ex)
                {
                    // Includes HttpClient timeouts, which surface as TaskCanceledException.
                    if (ct.IsCancellationRequested)
                        break;
                    StatusChanged?.Invoke($"Kick: error ({ex.GetType().Name}), retrying in {errorDelaySeconds}s");
                    _log.Debug(ex.ToString());
                    try { await Task.Delay(TimeSpan.FromSeconds(errorDelaySeconds), ct).ConfigureAwait(false); }
                    catch (OperationCanceledException) { break; }
                    errorDelaySeconds = Math.Min(errorDelaySeconds * 2, 120);
                }
            }
        }

        private async Task ConnectAndReadAsync(KickChannelInfo info, CancellationToken ct)
        {
            var clientId = Guid.NewGuid().ToString();
            var url = await GetRealtimeUrlAsync(info, clientId, ct).ConfigureAwait(false);
            var token = await GetGuestTokenAsync(clientId, ct).ConfigureAwait(false);

            using (var socket = new ClientWebSocket())
            {
                _socket = socket;
                try
                {
                    TrySetHeader(socket, "Origin", "https://kick.com");
                    TrySetHeader(socket, "User-Agent",
                        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
                    socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(30);

                    StatusChanged?.Invoke($"Kick: connecting to {info.DisplayName}...");
                    await socket.ConnectAsync(new Uri(url), ct).ConfigureAwait(false);

                    var nextId = 1;
                    await SendCommandAsync(socket, nextId++, "connect",
                        new JObject { ["token"] = token, ["name"] = "js" }, ct).ConfigureAwait(false);
                    var subscribeId = nextId++;
                    await SendCommandAsync(socket, subscribeId, "subscribe",
                        new JObject { ["channel"] = "chatrooms." + info.ChatroomId + ".v2" }, ct).ConfigureAwait(false);

                    // Guest tokens expire. The server pings about every 25s, so
                    // checking after each receive is often enough.
                    DateTime? refreshAtUtc = null;
                    while (!ct.IsCancellationRequested && socket.State == WebSocketState.Open)
                    {
                        var text = await ReceiveTextAsync(socket, ct).ConfigureAwait(false);
                        if (text == null)
                            return;

                        if (refreshAtUtc.HasValue && DateTime.UtcNow >= refreshAtUtc.Value)
                        {
                            refreshAtUtc = null;
                            var freshToken = await GetGuestTokenAsync(clientId, ct).ConfigureAwait(false);
                            await SendCommandAsync(socket, nextId++, "refresh",
                                new JObject { ["token"] = freshToken }, ct).ConfigureAwait(false);
                        }

                        // One frame can carry several newline-separated replies.
                        foreach (var line in text.Split('\n'))
                        {
                            if (line.Length == 0)
                                continue;
                            if (line == "{}")
                            {
                                await SendTextAsync(socket, "{}", ct).ConfigureAwait(false); // ping -> pong
                                continue;
                            }

                            JObject reply;
                            try
                            {
                                reply = JObject.Parse(line);
                            }
                            catch (JsonException ex)
                            {
                                _log.Debug("Kick: ignored malformed realtime frame: " + ex.Message);
                                continue;
                            }

                            if (reply["error"] is JObject error)
                                throw new InvalidOperationException("Kick realtime error: " +
                                    (StringValue(error["message"]) ?? error.ToString(Formatting.None)));

                            if (reply["connect"] is JObject connect)
                            {
                                refreshAtUtc = RefreshTime(connect);
                            }
                            else if (reply["refresh"] is JObject refresh)
                            {
                                refreshAtUtc = RefreshTime(refresh);
                            }
                            else if (reply["subscribe"] != null && TryInt(reply["id"]) == subscribeId)
                            {
                                StatusChanged?.Invoke($"Kick: connected to {info.DisplayName} chat");
                            }
                            else if (reply["push"] is JObject push)
                            {
                                if (push["disconnect"] != null)
                                    return;
                                var data = (push["pub"] as JObject)?["data"] as JObject;
                                var evt = StringValue(data?["event"]);
                                if (evt != null && IsChatMessageEvent(evt))
                                    HandleChatMessage(data!["data"]);
                            }
                        }
                    }
                }
                finally
                {
                    if (ReferenceEquals(_socket, socket))
                        _socket = null;
                }
            }
        }

        private void HandleChatMessage(JToken? data)
        {
            ChatMessage? message;
            try
            {
                message = ParseChatMessage(data);
            }
            catch (Exception ex)
            {
                _log.Debug("Kick: ignored malformed chat message: " + ex.Message);
                return;
            }

            if (message == null)
                return;

            var count = Interlocked.Increment(ref _messageCount);
            if (count <= 5 || count % 100 == 0)
                _log.Info($"Kick: received message #{count} (from {message.Author})");

            MessageReceived?.Invoke(message);
        }

        private async Task<string> GetRealtimeUrlAsync(KickChannelInfo info, string clientId, CancellationToken ct)
        {
            var body = new JObject
            {
                ["client"] = new JObject { ["id"] = clientId, ["type"] = "web" },
                ["capabilities"] = new JObject
                {
                    ["accepted_providers"] = new JArray(new JObject { ["provider"] = "centrifugo" }),
                },
            };
            var response = await PostRealtimeAsync("/channels/" + info.ChannelId + "/chat/connection", body, ct)
                .ConfigureAwait(false);
            if ((response?["data"] as JObject)?["connections"] is JArray connections)
            {
                foreach (var entry in connections)
                {
                    if (!(entry is JObject connection))
                        continue;
                    var url = StringValue((connection["credentials"] as JObject)?["url"]);
                    if (StringValue(connection["provider"]) == "centrifugo" && url != null &&
                        url.StartsWith("wss://", StringComparison.OrdinalIgnoreCase))
                        return url;
                }
            }

            _log.Debug("Kick: realtime lookup gave no server, using the default");
            return DefaultRealtimeUrl;
        }

        private async Task<string> GetGuestTokenAsync(string clientId, CancellationToken ct)
        {
            var response = await PostRealtimeAsync("/auth/connection", new JObject { ["client_id"] = clientId }, ct)
                .ConfigureAwait(false);
            return StringValue((response?["data"] as JObject)?["token"])
                ?? throw new InvalidOperationException("Kick: no guest token");
        }

        // POST to Kick's realtime API, falling back to curl.exe like the channel
        // lookup when Cloudflare turns the game's HttpClient away.
        private async Task<JObject?> PostRealtimeAsync(string path, JObject body, CancellationToken ct)
        {
            var url = RealtimeApi + path;
            var json = body.ToString(Formatting.None);
            var allowCurlFallback = true;
            try
            {
                using (var request = new HttpRequestMessage(HttpMethod.Post, url))
                {
                    request.Content = new StringContent(json, Encoding.UTF8, "application/json");
                    request.Headers.TryAddWithoutValidation("Accept", "application/json");
                    request.Headers.TryAddWithoutValidation("Origin", "https://kick.com");
                    request.Headers.TryAddWithoutValidation("Referer", "https://kick.com/");
                    using (var response = await _http.SendAsync(request, ct).ConfigureAwait(false))
                    {
                        var text = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                        if (response.IsSuccessStatusCode && KickChannelResolver.LooksLikeJson(text))
                            return ParseObject(text);

                        _log.Debug($"Kick: realtime {path} HTTP {(int)response.StatusCode}");
                        allowCurlFallback = KickChannelResolver.ShouldTryCurlForStatus((int)response.StatusCode) ||
                            !KickChannelResolver.LooksLikeJson(text);
                    }
                }
            }
            catch (HttpRequestException ex)
            {
                _log.Debug($"Kick: realtime {path} failed: " + ex.Message);
            }

            if (!allowCurlFallback)
                return null;
            var curlText = await KickChannelResolver.CurlJsonAsync(url, json, "https://kick.com/", "realtime " + path,
                _log, ct).ConfigureAwait(false);
            return curlText == null ? null : ParseObject(curlText);
        }

        private static JObject? ParseObject(string text)
        {
            try
            {
                return JToken.Parse(text) as JObject;
            }
            catch (JsonException)
            {
                return null;
            }
        }

        // Refresh two minutes early (but no sooner than 30s) when the server
        // says the token expires.
        private static DateTime? RefreshTime(JObject reply)
        {
            if (!IsTrue(reply["expires"]))
                return null;
            var ttl = TryInt(reply["ttl"]);
            return ttl.HasValue ? DateTime.UtcNow.AddSeconds(Math.Max(ttl.Value - 120, 30)) : (DateTime?)null;
        }

        private static int? TryInt(JToken? token)
        {
            if (token == null || token.Type == JTokenType.Null)
                return null;
            if (token.Type == JTokenType.Integer)
                return token.ToObject<int>();
            return int.TryParse(token.ToString(), out var value) ? value : (int?)null;
        }

        private static Task SendCommandAsync(ClientWebSocket socket, int id, string command, JObject args,
            CancellationToken ct)
        {
            var frame = new JObject { ["id"] = id, [command] = args };
            return SendTextAsync(socket, frame.ToString(Formatting.None), ct);
        }

        private async Task<string?> ReceiveTextAsync(ClientWebSocket socket, CancellationToken ct)
        {
            var segment = new ArraySegment<byte>(_receiveBuffer);
            var result = await socket.ReceiveAsync(segment, ct).ConfigureAwait(false);
            if (result.MessageType == WebSocketMessageType.Close)
                return null;
            if (result.EndOfMessage)
                return Encoding.UTF8.GetString(_receiveBuffer, 0, result.Count);

            using (var stream = new MemoryStream(_receiveBuffer.Length))
            {
                stream.Write(_receiveBuffer, 0, result.Count);
                do
                {
                    result = await socket.ReceiveAsync(segment, ct).ConfigureAwait(false);
                    if (result.MessageType == WebSocketMessageType.Close)
                        return null;
                    stream.Write(_receiveBuffer, 0, result.Count);
                }
                while (!result.EndOfMessage);

                if (stream.TryGetBuffer(out var bytes) && bytes.Array != null)
                    return Encoding.UTF8.GetString(bytes.Array, bytes.Offset, bytes.Count);
                return Encoding.UTF8.GetString(stream.ToArray());
            }
        }

        private static Task SendTextAsync(ClientWebSocket socket, string text, CancellationToken ct)
        {
            var bytes = Encoding.UTF8.GetBytes(text);
            return socket.SendAsync(new ArraySegment<byte>(bytes), WebSocketMessageType.Text, true, ct);
        }

        private static void TrySetHeader(ClientWebSocket socket, string name, string value)
        {
            try { socket.Options.SetRequestHeader(name, value); }
            catch { /* Some runtimes reserve headers like User-Agent. */ }
        }

        private ChatMessage? ParseChatMessage(JToken? dataToken)
        {
            var data = ParseDataObject(dataToken);
            if (data == null)
                return null;

            var id = MessageIdentity(data);
            if (id != null && !RememberMessage(id))
                return null;

            var messageObject = AsObject(data["message"]);
            var sender = AsObject(data["sender"])
                ?? AsObject(data["user"])
                ?? AsObject(messageObject?["sender"])
                ?? AsObject(messageObject?["user"]);
            var text = FirstString(messageObject, MessageTextFields)
                ?? StringValue(data["message"])
                ?? FirstString(data, MessageTextFields);
            var author = FirstString(sender, SenderNameFields)
                ?? FirstString(data, DataAuthorFields)
                ?? "Kick";

            if (string.IsNullOrWhiteSpace(text))
                return null;

            var identity = AsObject(sender?["identity"]) ?? AsObject(data["identity"]);
            var color = FirstString(identity, ColorFields)
                ?? FirstString(sender, ColorFields)
                ?? NameColors.For(author);
            if (!HexColorRegex.IsMatch(color))
                color = NameColors.For(author);

            var isBroadcaster = IsTrue(sender?["is_broadcaster"]) ||
                HasBadge(identity?["badges"], "broadcaster") ||
                HasBadge(sender?["badges"], "broadcaster") ||
                StringEqualsAny(FirstString(sender, RoleFields), BroadcasterRoles);

            var isModerator = IsTrue(sender?["is_moderator"]) ||
                HasBadge(identity?["badges"], "moderator") ||
                HasBadge(sender?["badges"], "moderator") ||
                StringEqualsAny(FirstString(sender, RoleFields), ModeratorRoles);

            var segments = BuildKickSegments(text!);
            var displayText = segments != null ? ChatMessage.TextFromSegments(segments) : CleanContent(text!);
            return new ChatMessage(ChatPlatform.Kick, author, color, isBroadcaster, isModerator,
                displayText, false, segments);
        }

        private bool RememberMessage(string id)
        {
            if (!_seenMessageIds.Add(id))
                return false;

            _seenMessageOrder.Enqueue(id);
            while (_seenMessageOrder.Count > 300)
                _seenMessageIds.Remove(_seenMessageOrder.Dequeue());
            return true;
        }

        private static string? MessageIdentity(JObject data)
        {
            var id = FirstString(data, IdFields);
            if (id != null)
                return id;

            var message = AsObject(data["message"]);
            id = FirstString(message, IdFields);
            if (id != null)
                return id;

            var sender = AsObject(data["sender"]) ?? AsObject(data["user"]);
            var text = FirstString(data, MessageTextFields) ?? FirstString(message, MessageTextFields);
            var createdAt = FirstString(data, CreatedAtFields) ?? FirstString(message, CreatedAtFields);
            var author = FirstString(sender, SenderNameFields);
            return text == null ? null : (author ?? "") + "|" + createdAt + "|" + text;
        }

        private static JObject? ParseDataObject(JToken? token)
        {
            if (token == null || token.Type == JTokenType.Null)
                return null;
            if (token.Type == JTokenType.Object)
                return (JObject)token;

            var raw = token.ToString();
            if (string.IsNullOrWhiteSpace(raw))
                return null;

            try
            {
                var parsed = JToken.Parse(raw);
                return parsed as JObject;
            }
            catch (JsonException)
            {
                return null;
            }
        }

        private static bool IsChatMessageEvent(string evt) =>
            evt.IndexOf("ChatMessageEvent", StringComparison.OrdinalIgnoreCase) >= 0 ||
            evt.IndexOf("ChatMessageSentEvent", StringComparison.OrdinalIgnoreCase) >= 0;

        private static string CleanContent(string text)
        {
            var decoded = WebUtility.HtmlDecode(text) ?? text;
            if (decoded.IndexOf("[emote:", StringComparison.Ordinal) >= 0)
                decoded = EmoteRegex.Replace(decoded, "$1");
            decoded = decoded.Replace('\r', ' ').Replace('\n', ' ');
            return decoded.Trim();
        }

        private static IReadOnlyList<ChatSegment>? BuildKickSegments(string text)
        {
            if (string.IsNullOrEmpty(text))
                return null;

            var decoded = WebUtility.HtmlDecode(text) ?? text;
            if (decoded.IndexOf("[emote:", StringComparison.Ordinal) < 0)
                return null;

            var segments = new List<ChatSegment>();
            var pos = 0;
            foreach (Match match in EmoteTokenRegex.Matches(decoded))
            {
                if (!match.Success)
                    continue;
                if (match.Index > pos)
                    AddCleanTextSegment(segments, decoded.Substring(pos, match.Index - pos));
                var id = match.Groups[1].Value;
                var name = match.Groups[2].Value;
                segments.Add(new EmoteSegment(new EmoteRef(
                    "kick:" + id,
                    "https://files.kick.com/emotes/" + id + "/fullsize",
                    name,
                    EmoteFormat.Unknown)));
                pos = match.Index + match.Length;
            }

            if (pos < decoded.Length)
                AddCleanTextSegment(segments, decoded.Substring(pos));
            return segments.Count == 0 ? null : segments;
        }

        private static void AddCleanTextSegment(List<ChatSegment> segments, string text)
        {
            if (string.IsNullOrEmpty(text))
                return;
            text = text.Replace('\r', ' ').Replace('\n', ' ');
            if (text.Length > 0)
                segments.Add(new TextSegment(text));
        }

        private static string? StringValue(JToken? token)
        {
            if (token == null || token.Type == JTokenType.Null ||
                token.Type == JTokenType.Object || token.Type == JTokenType.Array)
                return null;
            var value = token.ToString();
            return string.IsNullOrWhiteSpace(value) ? null : value;
        }

        private static string? FirstString(JToken? token, string[] names)
        {
            var obj = AsObject(token);
            if (obj == null)
                return null;
            foreach (var name in names)
            {
                var value = StringValue(obj[name]);
                if (!string.IsNullOrWhiteSpace(value))
                    return value;
            }
            return null;
        }

        private static JObject? AsObject(JToken? token) => token as JObject;

        private static bool IsTrue(JToken? token)
        {
            if (token == null || token.Type == JTokenType.Null)
                return false;
            if (token.Type == JTokenType.Boolean)
                return token.ToObject<bool>();
            var value = token.ToString();
            return value == "1" || value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
                value.Equals("yes", StringComparison.OrdinalIgnoreCase);
        }

        private static bool HasBadge(JToken? badges, string expectedType)
        {
            if (!(badges is JArray array))
                return false;

            foreach (var badge in array)
            {
                var value = badge.Type == JTokenType.String
                    ? badge.ToString()
                    : FirstString(badge, BadgeFields);
                if (value != null && value.Equals(expectedType, StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            return false;
        }

        private static bool StringEqualsAny(string? value, string[] expected)
        {
            if (string.IsNullOrWhiteSpace(value))
                return false;
            foreach (var item in expected)
                if (value!.Equals(item, StringComparison.OrdinalIgnoreCase))
                    return true;
            return false;
        }
    }
}
