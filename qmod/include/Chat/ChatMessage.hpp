#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace SimpleBSStreamChat::Chat {
    enum class ChatPlatform { System, Twitch, YouTube, Kick };
    enum class ChatEvent { Message, RemoveMessage, RemoveUser, ClearChannel, ChannelIdentity };

    struct ChatBadge {
        std::string id;
        std::string version;
        std::string label;
        std::string imageUrl;
    };

    /// A run of message text, or an emote. An emote keeps its name in `text`,
    /// shown whenever its image isn't available (or images are turned off).
    struct ChatSegment {
        std::string text;
        std::string emoteKey;  // empty for plain text; unique per provider + emote
        std::string emoteUrl;
        std::string animatedUrl;  // optional GIF; static URL remains the fallback

        bool IsEmote() const { return !emoteKey.empty(); }
    };

    struct ChatMessage {
        ChatPlatform platform = ChatPlatform::System;
        std::string author;
        std::string color;  // "#RRGGBB"
        bool isBroadcaster = false;
        bool isModerator = false;
        std::string text;  // the whole message as plain text
        bool isAction = false;  // Twitch "/me"
        /// `text` split into text and emotes; empty when there are no emotes.
        std::vector<ChatSegment> segments;
        /// Increases with every message ChatManager stores, so the panel can
        /// tell new messages from ones it already shows.
        uint64_t sequence = 0;
        ChatEvent event = ChatEvent::Message;
        std::string messageId;
        std::string userId;
        std::string channelId;
        std::string channelName;
        std::string authorLogin;
        std::vector<ChatBadge> badges;
        bool isHighlighted = false;
        bool isMention = false;
        bool isNotice = false;
        uint64_t bits = 0;
        bool isDeleted = false;
        uint64_t revision = 0;
    };

    /// Stable per-name fallback colors (Twitch's default username palette).
    std::string NameColorFor(std::string_view name);

    /// `value` reduced to characters that are safe in a TMP sprite name;
    /// anything else becomes its hex code. Same scheme as the PC StableKey.
    std::string StableKey(std::string_view value);
}
