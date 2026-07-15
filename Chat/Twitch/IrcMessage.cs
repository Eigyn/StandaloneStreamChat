using System.Collections.Generic;
using System.Text;

namespace SimpleBSStreamChat.Chat.Twitch
{
    /// <summary>
    /// Minimal IRCv3 line parser: <c>@tags :prefix COMMAND params :trailing</c>.
    /// Pure logic, no I/O — unit-testable with raw strings.
    /// </summary>
    public class IrcMessage
    {
        public IReadOnlyDictionary<string, string> Tags { get; }
        public string Prefix { get; }
        public string Command { get; }
        /// <summary>Middle parameters; the trailing parameter (after " :") is the last entry.</summary>
        public IReadOnlyList<string> Params { get; }

        private IrcMessage(Dictionary<string, string> tags, string prefix, string command, List<string> parameters)
        {
            Tags = tags;
            Prefix = prefix;
            Command = command;
            Params = parameters;
        }

        /// <summary>Nick portion of the prefix ("nick!user@host" → "nick").</summary>
        public string PrefixNick
        {
            get
            {
                var bang = Prefix.IndexOf('!');
                return bang < 0 ? Prefix : Prefix.Substring(0, bang);
            }
        }

        public string TrailingOrEmpty => Params.Count > 0 ? Params[Params.Count - 1] : "";

        public static IrcMessage? Parse(string line)
        {
            if (string.IsNullOrEmpty(line))
                return null;

            var tags = new Dictionary<string, string>();
            var pos = 0;

            if (line[pos] == '@')
            {
                var end = line.IndexOf(' ', pos);
                if (end < 0)
                    return null;
                var tagsPart = line.Substring(1, end - 1);
                foreach (var pair in tagsPart.Split(';'))
                {
                    if (pair.Length == 0)
                        continue;
                    var eq = pair.IndexOf('=');
                    if (eq < 0)
                        tags[pair] = "";
                    else
                        tags[pair.Substring(0, eq)] = UnescapeTagValue(pair.Substring(eq + 1));
                }
                pos = end + 1;
            }

            while (pos < line.Length && line[pos] == ' ')
                pos++;

            var prefix = "";
            if (pos < line.Length && line[pos] == ':')
            {
                var end = line.IndexOf(' ', pos);
                if (end < 0)
                    return null;
                prefix = line.Substring(pos + 1, end - pos - 1);
                pos = end + 1;
                while (pos < line.Length && line[pos] == ' ')
                    pos++;
            }

            if (pos >= line.Length)
                return null;

            string command;
            var cmdEnd = line.IndexOf(' ', pos);
            if (cmdEnd < 0)
            {
                command = line.Substring(pos);
                pos = line.Length;
            }
            else
            {
                command = line.Substring(pos, cmdEnd - pos);
                pos = cmdEnd + 1;
            }

            var parameters = new List<string>();
            while (pos < line.Length)
            {
                while (pos < line.Length && line[pos] == ' ')
                    pos++;
                if (pos >= line.Length)
                    break;
                if (line[pos] == ':')
                {
                    parameters.Add(line.Substring(pos + 1));
                    break;
                }
                var end = line.IndexOf(' ', pos);
                if (end < 0)
                {
                    parameters.Add(line.Substring(pos));
                    break;
                }
                parameters.Add(line.Substring(pos, end - pos));
                pos = end + 1;
            }

            return new IrcMessage(tags, prefix, command, parameters);
        }

        // IRCv3 tag value escaping: \: → ';', \s → ' ', \\ → '\', \r, \n.
        private static string UnescapeTagValue(string value)
        {
            if (value.IndexOf('\\') < 0)
                return value;

            var sb = new StringBuilder(value.Length);
            for (var i = 0; i < value.Length; i++)
            {
                var c = value[i];
                if (c != '\\')
                {
                    sb.Append(c);
                    continue;
                }
                if (++i >= value.Length)
                    break; // lone trailing backslash is dropped per spec
                switch (value[i])
                {
                    case ':': sb.Append(';'); break;
                    case 's': sb.Append(' '); break;
                    case '\\': sb.Append('\\'); break;
                    case 'r': sb.Append('\r'); break;
                    case 'n': sb.Append('\n'); break;
                    default: sb.Append(value[i]); break;
                }
            }
            return sb.ToString();
        }
    }
}
