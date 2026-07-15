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
    /// this uses the same public Pusher chat stream the web client exposes.
    /// </summary>
    public class KickLiveChatClient : IChatService
    {
        private static readonly string[] PusherUrls =
        {
            "wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0-rc2&flash=false",
            "wss://ws-us2.pusher.com/app/eb1d5f283081a78b932c?protocol=7&client=js&version=8.4.0-rc2&flash=false",
        };
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
        private int _preferredPusherUrlIndex;

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

                    Exception? lastError = null;
                    var startIndex = _preferredPusherUrlIndex;
                    for (var offset = 0; offset < PusherUrls.Length; offset++)
                    {
                        var index = (startIndex + offset) % PusherUrls.Length;
                        var url = PusherUrls[index];
                        try
                        {
                            await ConnectAndReadAsync(info, url, ct).ConfigureAwait(false);
                            _preferredPusherUrlIndex = index;
                            lastError = null;
                            break;
                        }
                        catch (PusherAppUnavailableException ex)
                        {
                            lastError = ex;
                            _log.Debug("Kick: Pusher app unavailable, trying fallback key: " + ex.Message);
                        }
                    }

                    if (lastError != null)
                        throw lastError;

                    errorDelaySeconds = 2;
                    StatusChanged?.Invoke($"Kick: disconnected - reconnecting in {errorDelaySeconds}s");
                    await Task.Delay(TimeSpan.FromSeconds(errorDelaySeconds), ct).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
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

        private async Task ConnectAndReadAsync(KickChannelInfo info, string pusherUrl, CancellationToken ct)
        {
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
                    await socket.ConnectAsync(new Uri(pusherUrl), ct).ConfigureAwait(false);

                    var subscribed = false;
                    while (!ct.IsCancellationRequested && socket.State == WebSocketState.Open)
                    {
                        var text = await ReceiveTextAsync(socket, ct).ConfigureAwait(false);
                        if (text == null)
                            return;

                        PusherFrame? frame;
                        try
                        {
                            frame = ParsePusherFrame(text);
                        }
                        catch (JsonException ex)
                        {
                            _log.Debug("Kick: ignored malformed Pusher frame: " + ex.Message);
                            continue;
                        }
                        if (frame == null)
                            continue;

                        var evt = frame.Event;
                        if (evt == "pusher:connection_established")
                        {
                            await SubscribeAsync(socket, "chatrooms." + info.ChatroomId + ".v2", ct).ConfigureAwait(false);
                            await SubscribeAsync(socket, "chatrooms." + info.ChatroomId, ct).ConfigureAwait(false);
                        }
                        else if (evt == "pusher_internal:subscription_succeeded")
                        {
                            if (!subscribed)
                            {
                                StatusChanged?.Invoke($"Kick: connected to {info.DisplayName} chat");
                                subscribed = true;
                            }
                        }
                        else if (evt == "pusher:ping")
                        {
                            await SendTextAsync(socket, "{\"event\":\"pusher:pong\",\"data\":{}}", ct).ConfigureAwait(false);
                        }
                        else if (evt == "pusher:error")
                        {
                            var message = StringValue(frame.Data) ?? frame.Data?.ToString() ?? "unknown Pusher error";
                            if (message.IndexOf("app", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                message.IndexOf("key", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                message.IndexOf("cluster", StringComparison.OrdinalIgnoreCase) >= 0)
                                throw new PusherAppUnavailableException(message);
                            throw new InvalidOperationException("Pusher error: " + message);
                        }
                        else if (IsChatMessageEvent(evt))
                        {
                            ChatMessage? message;
                            try
                            {
                                message = ParseChatMessage(frame.Data);
                            }
                            catch (Exception ex)
                            {
                                _log.Debug("Kick: ignored malformed chat message: " + ex.Message);
                                continue;
                            }

                            if (message == null)
                                continue;

                            var count = Interlocked.Increment(ref _messageCount);
                            if (count <= 5 || count % 100 == 0)
                                _log.Info($"Kick: received message #{count} (from {message.Author})");

                            MessageReceived?.Invoke(message);
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

        private static PusherFrame? ParsePusherFrame(string text)
        {
            using (var reader = new JsonTextReader(new StringReader(text)))
            {
                if (!reader.Read() || reader.TokenType != JsonToken.StartObject)
                    return null;

                string? evt = null;
                JToken? data = null;
                while (reader.Read())
                {
                    if (reader.TokenType == JsonToken.EndObject)
                        break;
                    if (reader.TokenType != JsonToken.PropertyName)
                    {
                        reader.Skip();
                        continue;
                    }

                    var name = reader.Value as string;
                    if (!reader.Read())
                        break;

                    if (string.Equals(name, "event", StringComparison.Ordinal))
                    {
                        evt = reader.TokenType == JsonToken.String
                            ? reader.Value as string
                            : JToken.ReadFrom(reader).ToString(Formatting.None);
                    }
                    else if (string.Equals(name, "data", StringComparison.Ordinal))
                    {
                        data = reader.TokenType == JsonToken.String
                            ? new JValue(reader.Value as string)
                            : JToken.ReadFrom(reader);
                    }
                    else
                    {
                        reader.Skip();
                    }
                }

                return string.IsNullOrWhiteSpace(evt) ? null : new PusherFrame(evt!, data);
            }
        }

        private static async Task SubscribeAsync(ClientWebSocket socket, string channel, CancellationToken ct)
        {
            var frame = new JObject
            {
                ["event"] = "pusher:subscribe",
                ["data"] = new JObject
                {
                    ["auth"] = "",
                    ["channel"] = channel,
                },
            };
            await SendTextAsync(socket, frame.ToString(Formatting.None), ct).ConfigureAwait(false);
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

        private sealed class PusherAppUnavailableException : Exception
        {
            public PusherAppUnavailableException(string message) : base(message)
            {
            }
        }

        private sealed class PusherFrame
        {
            public PusherFrame(string evt, JToken? data)
            {
                Event = evt;
                Data = data;
            }

            public string Event { get; }
            public JToken? Data { get; }
        }
    }
}
