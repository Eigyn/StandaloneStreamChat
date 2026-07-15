namespace SimpleBSStreamChat.Chat
{
    /// <summary>Stable per-name fallback colors (Twitch's default username palette).</summary>
    public static class NameColors
    {
        private static readonly string[] Palette =
        {
            "#FF0000", "#0000FF", "#00FF00", "#B22222", "#FF7F50",
            "#9ACD32", "#FF4500", "#2E8B57", "#DAA520", "#D2691E",
            "#5F9EA0", "#1E90FF", "#FF69B4", "#8A2BE2", "#00FF7F",
        };

        public static string For(string name)
        {
            var hash = 0;
            foreach (var c in name)
                hash = (hash * 31 + c) & 0x7FFFFFFF;
            return Palette[hash % Palette.Length];
        }
    }
}
