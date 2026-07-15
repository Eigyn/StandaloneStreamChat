using System;

namespace SimpleBSStreamChat.Chat.Requests
{
    /// <summary>
    /// Pure, Unity-free parser for the request command. Recognizes, after the
    /// configured command word: a bare key (<c>!request 4be4a</c>), a bsr token
    /// (<c>!request !bsr 4be4a</c> / <c>bsr 4be4a</c>), or a pasted BeatSaver URL
    /// (<c>!request https://beatsaver.com/maps/4be4a</c>).
    /// </summary>
    public static class RequestCommandParser
    {
        public readonly struct ParseResult
        {
            /// <summary>The message started with the command word (a request attempt).</summary>
            public readonly bool IsCommand;
            /// <summary>The extracted, validated key — null when the key was missing/invalid.</summary>
            public readonly string? Key;

            public ParseResult(bool isCommand, string? key)
            {
                IsCommand = isCommand;
                Key = key;
            }
        }

        private static readonly char[] Whitespace = { ' ', '\t', '\r', '\n' };
        private static readonly char[] KeyEnders = { '/', '?', '#', '&' };

        /// <param name="commandWord">One or more comma-separated aliases, e.g. "!request,!req".</param>
        public static ParseResult Parse(string? messageText, string? commandWord)
        {
            if (string.IsNullOrEmpty(messageText) || string.IsNullOrEmpty(commandWord))
                return new ParseResult(false, null);

            var text = messageText!.Trim();
            foreach (var alias in commandWord!.Split(','))
            {
                var word = alias.Trim();
                if (word.Length == 0)
                    continue;
                if (!text.StartsWith(word, StringComparison.OrdinalIgnoreCase))
                    continue;
                // Require a word boundary so "!requests" doesn't match "!request".
                if (text.Length > word.Length && !IsWhitespace(text[word.Length]))
                    continue;

                var remainder = text.Substring(word.Length).Trim();
                return new ParseResult(true, ExtractKey(remainder));
            }

            return new ParseResult(false, null);
        }

        private static string? ExtractKey(string remainder)
        {
            if (remainder.Length == 0)
                return null;

            var tokens = remainder.Split(Whitespace, StringSplitOptions.RemoveEmptyEntries);
            if (tokens.Length == 0)
                return null;

            // Skip a leading "!bsr" / "bsr" token.
            var raw = tokens[0];
            if ((raw.Equals("!bsr", StringComparison.OrdinalIgnoreCase) ||
                 raw.Equals("bsr", StringComparison.OrdinalIgnoreCase)) && tokens.Length > 1)
                raw = tokens[1];

            // Pasted BeatSaver URL → take the segment after "maps/".
            var mapsIdx = raw.IndexOf("maps/", StringComparison.OrdinalIgnoreCase);
            if (mapsIdx >= 0)
                raw = raw.Substring(mapsIdx + 5);

            raw = raw.Split(KeyEnders, 2)[0];   // drop any trailing path/query
            raw = raw.TrimStart('!');
            raw = raw.ToLowerInvariant().TrimStart('0'); // BeatSaver keys carry no leading zeros

            return IsValidKey(raw) ? raw : null;
        }

        public static bool IsValidKey(string? key)
        {
            if (string.IsNullOrEmpty(key) || key!.Length > 6)
                return false;
            foreach (var c in key)
            {
                var isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                if (!isHex)
                    return false;
            }
            return true;
        }

        private static bool IsWhitespace(char c) => c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }
}
