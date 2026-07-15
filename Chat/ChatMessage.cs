using System.Collections.Generic;
using System.Text;

namespace SimpleBSStreamChat.Chat
{
    public enum ChatPlatform
    {
        System,
        Twitch,
        YouTube,
        Kick,
    }

    public class ChatMessage
    {
        public ChatPlatform Platform { get; }
        public string Author { get; }
        public string AuthorColorHex { get; } // "#RRGGBB"
        public bool IsBroadcaster { get; }
        public bool IsModerator { get; }
        public string Text { get; }
        public IReadOnlyList<ChatSegment>? Segments { get; }
        public bool IsAction { get; } // Twitch "/me" style message

        public ChatMessage(ChatPlatform platform, string author, string authorColorHex,
            bool isBroadcaster, bool isModerator, string text, bool isAction,
            IReadOnlyList<ChatSegment>? segments = null)
        {
            Platform = platform;
            Author = author;
            AuthorColorHex = authorColorHex;
            IsBroadcaster = isBroadcaster;
            IsModerator = isModerator;
            Text = text;
            IsAction = isAction;
            Segments = segments;
        }

        public static ChatMessage System(string text) =>
            new ChatMessage(ChatPlatform.System, "", "#9E9E9E", false, false, text, false);

        public static string TextFromSegments(IReadOnlyList<ChatSegment> segments)
        {
            var builder = new StringBuilder();
            foreach (var segment in segments)
            {
                if (segment is TextSegment text)
                    builder.Append(text.Text);
                else if (segment is EmoteSegment emote)
                    builder.Append(emote.Emote.Name);
            }
            return builder.ToString();
        }
    }

    public abstract class ChatSegment
    {
    }

    public sealed class TextSegment : ChatSegment
    {
        public TextSegment(string text)
        {
            Text = text ?? "";
        }

        public string Text { get; }
    }

    public sealed class EmoteSegment : ChatSegment
    {
        public EmoteSegment(EmoteRef emote)
        {
            Emote = emote;
        }

        public EmoteRef Emote { get; }
    }

    public enum EmoteFormat
    {
        Unknown,
        Png,
        Jpeg,
        Gif,
        Apng,
        WebP,
    }

    public sealed class EmoteRef
    {
        public EmoteRef(string key, string imageUrl, string name,
            EmoteFormat format = EmoteFormat.Unknown, bool animated = false)
        {
            Key = key ?? "";
            ImageUrl = imageUrl ?? "";
            Name = name ?? "";
            Format = format;
            Animated = animated;
        }

        public string Key { get; }
        public string ImageUrl { get; }
        public string Name { get; }
        public EmoteFormat Format { get; }
        public bool Animated { get; }
    }
}
