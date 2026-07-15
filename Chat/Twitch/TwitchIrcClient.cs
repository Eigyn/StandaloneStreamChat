using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Text;
using System.Threading;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.Twitch
{
    /// <summary>
    /// Anonymous read-only Twitch chat client over IRC/TLS using the documented
    /// "justinfan" anonymous login — no OAuth, no client id.
    /// Deliberately uses BLOCKING reads on a dedicated background thread:
    /// async SslStream reads stall on the game's Mono runtime after the initial
    /// burst (observed in-game: join succeeded, then no lines ever arrived).
    /// </summary>
    public class TwitchIrcClient : IChatService
    {
        private const string Host = "irc.chat.twitch.tv";
        private const int Port = 6697;

        public event Action<ChatMessage>? MessageReceived;
        public event Action<string>? StatusChanged;

        private readonly string _channel; // lowercase, without '#'
        private readonly IPALogger _log;
        private Thread? _thread;
        private volatile bool _stopping;
        private TcpClient? _tcp;
        private int _messageCount;

        public TwitchIrcClient(string channel, IPALogger log)
        {
            _channel = channel.Trim().TrimStart('#').ToLowerInvariant();
            _log = log;
        }

        public void Start()
        {
            if (_thread != null)
                return;
            _stopping = false;
            _thread = new Thread(RunLoop)
            {
                IsBackground = true,
                Name = "SimpleBSStreamChat.TwitchIrc",
            };
            _thread.Start();
        }

        public void Stop() => Stop(true);

        public void Stop(bool waitForExit)
        {
            if (_thread == null)
                return;
            _stopping = true;
            try { _tcp?.Close(); } catch { /* already closed */ }
            if (waitForExit)
                try { _thread.Join(2000); } catch { /* background thread dies with the process */ }
            _thread = null;
        }

        public void Dispose() => Stop();

        private void RunLoop()
        {
            var delaySeconds = 1;
            while (!_stopping)
            {
                try
                {
                    ConnectAndRead();
                    // Server closed the connection or asked us to reconnect.
                    delaySeconds = 1;
                }
                catch (Exception ex)
                {
                    if (_stopping)
                        break;
                    StatusChanged?.Invoke($"Twitch: connection lost ({ex.GetType().Name}), retrying in {delaySeconds}s");
                    _log.Debug(ex.ToString());
                }

                // Interruptible backoff sleep.
                for (var waited = 0; waited < delaySeconds * 1000 && !_stopping; waited += 250)
                    Thread.Sleep(250);
                if (_stopping)
                    break;
                delaySeconds = Math.Min(delaySeconds * 2, 60);
            }
        }

        private void ConnectAndRead()
        {
            StatusChanged?.Invoke($"Twitch: connecting to #{_channel}...");

            using (var tcp = new TcpClient())
            {
                _tcp = tcp;
                tcp.Connect(Host, Port);
                using (var ssl = new SslStream(tcp.GetStream(), false))
                {
                    ssl.AuthenticateAsClient(Host, null, SslProtocols.Tls12, false);
                    using (var reader = new StreamReader(ssl, Encoding.UTF8))
                    using (var writer = new StreamWriter(ssl, new UTF8Encoding(false)) { NewLine = "\r\n", AutoFlush = true })
                    {
                        var nick = "justinfan" + new Random().Next(10000, 99999);
                        writer.WriteLine("CAP REQ :twitch.tv/tags twitch.tv/commands");
                        writer.WriteLine("NICK " + nick);

                        while (!_stopping)
                        {
                            var line = reader.ReadLine();
                            if (line == null)
                                return; // server closed; outer loop reconnects

                            var message = IrcMessage.Parse(line);
                            if (message == null)
                                continue;

                            switch (message.Command)
                            {
                                case "PING":
                                    writer.WriteLine("PONG :" + message.TrailingOrEmpty);
                                    break;
                                case "001": // welcome — safe to join now
                                    writer.WriteLine("JOIN #" + _channel);
                                    break;
                                case "366": // end of names — join complete
                                    StatusChanged?.Invoke($"Twitch: joined #{_channel}");
                                    break;
                                case "PRIVMSG":
                                    HandlePrivmsg(message);
                                    break;
                                case "NOTICE":
                                    StatusChanged?.Invoke("Twitch: " + message.TrailingOrEmpty);
                                    break;
                                case "RECONNECT":
                                    StatusChanged?.Invoke("Twitch: server requested reconnect");
                                    return;
                            }
                        }
                    }
                }
            }
        }

        private void HandlePrivmsg(IrcMessage message)
        {
            if (message.Params.Count < 2)
                return;

            var text = message.TrailingOrEmpty;
            var isAction = false;
            if (text.StartsWith("\u0001ACTION ", StringComparison.Ordinal))
            {
                isAction = true;
                text = text.Substring(8).TrimEnd('\u0001');
            }

            message.Tags.TryGetValue("display-name", out var author);
            if (string.IsNullOrEmpty(author))
                author = message.PrefixNick;

            message.Tags.TryGetValue("color", out var color);
            if (string.IsNullOrEmpty(color))
                color = NameColors.For(author!);

            message.Tags.TryGetValue("badges", out var badges);
            badges = badges ?? "";
            message.Tags.TryGetValue("emotes", out var emoteTag);
            var segments = BuildEmoteSegments(text, emoteTag);

            var chatMessage = new ChatMessage(
                ChatPlatform.Twitch,
                author!,
                color!,
                isBroadcaster: badges.Contains("broadcaster/"),
                isModerator: badges.Contains("moderator/"),
                text,
                isAction,
                segments);

            var count = Interlocked.Increment(ref _messageCount);
            if (count <= 5 || count % 100 == 0)
                _log.Info($"Twitch: received message #{count} (from {author})");

            MessageReceived?.Invoke(chatMessage);
        }

        private static IReadOnlyList<ChatSegment>? BuildEmoteSegments(string text, string? emoteTag)
        {
            if (string.IsNullOrWhiteSpace(emoteTag) || string.IsNullOrEmpty(text))
                return null;

            var spans = new List<EmoteSpan>();
            foreach (var emotePart in emoteTag!.Split('/'))
            {
                var colon = emotePart.IndexOf(':');
                if (colon <= 0 || colon + 1 >= emotePart.Length)
                    continue;
                var id = emotePart.Substring(0, colon);
                foreach (var range in emotePart.Substring(colon + 1).Split(','))
                {
                    var dash = range.IndexOf('-');
                    if (dash <= 0 || dash + 1 >= range.Length)
                        continue;
                    if (!int.TryParse(range.Substring(0, dash), out var startCodepoint) ||
                        !int.TryParse(range.Substring(dash + 1), out var endCodepoint) ||
                        endCodepoint < startCodepoint)
                        continue;

                    var start = CodepointIndexToCharIndex(text, startCodepoint);
                    var endExclusive = CodepointIndexToCharIndex(text, endCodepoint + 1);
                    if (start < 0 || endExclusive <= start || endExclusive > text.Length)
                        continue;
                    var name = text.Substring(start, endExclusive - start);
                    spans.Add(new EmoteSpan(start, endExclusive, new EmoteRef(
                        "tw:v2:" + id,
                        "https://static-cdn.jtvnw.net/emoticons/v2/" + id + "/default/dark/2.0",
                        name,
                        EmoteFormat.Unknown)));
                }
            }

            if (spans.Count == 0)
                return null;

            spans = spans.OrderBy(s => s.Start).ToList();
            var segments = new List<ChatSegment>();
            var pos = 0;
            foreach (var span in spans)
            {
                if (span.Start < pos || span.End > text.Length)
                    continue;
                if (span.Start > pos)
                    segments.Add(new TextSegment(text.Substring(pos, span.Start - pos)));
                segments.Add(new EmoteSegment(span.Emote));
                pos = span.End;
            }
            if (pos < text.Length)
                segments.Add(new TextSegment(text.Substring(pos)));
            return segments.Count == 0 ? null : segments;
        }

        private static int CodepointIndexToCharIndex(string text, int codepointIndex)
        {
            if (codepointIndex <= 0)
                return 0;
            var codepoints = 0;
            for (var i = 0; i < text.Length; i++)
            {
                if (codepoints == codepointIndex)
                    return i;
                if (char.IsHighSurrogate(text[i]) && i + 1 < text.Length && char.IsLowSurrogate(text[i + 1]))
                    i++;
                codepoints++;
            }
            return codepoints == codepointIndex ? text.Length : -1;
        }

        private sealed class EmoteSpan
        {
            public EmoteSpan(int start, int end, EmoteRef emote)
            {
                Start = start;
                End = end;
                Emote = emote;
            }

            public int Start { get; }
            public int End { get; }
            public EmoteRef Emote { get; }
        }
    }
}
