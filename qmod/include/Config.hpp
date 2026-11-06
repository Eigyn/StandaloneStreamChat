#pragma once

#include <string>

namespace SimpleBSStreamChat {
    // Where "Recenter Window" puts the panel (same as the PC build).
    inline constexpr float DefaultPositionX = 0.0f;
    inline constexpr float DefaultPositionY = 4.4f;
    inline constexpr float DefaultPositionZ = 4.0f;
    inline constexpr float DefaultRotationX = -25.0f;
    inline constexpr float DefaultRotationY = 0.0f;

    // Mirrors the PC PluginConfig fields the Quest build supports so far,
    // under the same JSON keys. Stored under the mod's ModData Configs folder.
    struct Config {
        std::string twitchChannel;  // without '#'
        bool enableTwitch = true;
        std::string youTubeChannel;  // @handle, UC channel id, or 11-char video id
        bool enableYouTube = true;
        std::string kickChannel;  // slug, or numeric chatroom id
        // On by default (the PC default is off): nothing connects until a
        // channel is set, so the toggle would only be one more step.
        bool enableKick = true;
        bool showPanel = true;
        bool showSystemMessages = true;
        bool showViewerCounts = true;
        // Size of the viewer-count numbers and their platform icons.
        float viewerCountSize = 2.9f;
        // Platform and broadcaster/mod icons in front of messages; off = just name and message.
        bool showPlatformIcons = true;
        // Stream emotes as inline images; off = their names. (Unicode emoji
        // always draw through the Unicode core mod's font.)
        bool emojiImages = true;
        // Optional on Quest: bounded GIF playback; static images remain the default.
        bool animatedEmotes = false;
        // A dark band behind each message, this opaque.
        bool highlightMessages = true;
        float highlightOpacity = 0.55f;
        float fontSize = 3.4f;
        float panelWidth = 90.0f;
        float panelHeight = 65.0f;
        // 0 = flat; higher = stronger curve (radius = 1200 / amount).
        float curveAmount = 0.0f;
        // Window corner radius in panel units; 0 = square. Quest only. Below 0
        // means unset: the panel fills in the rounding BSML's backdrop had.
        float cornerRadius = -1.0f;
        // Window backdrop tint as "#RRGGBB"; alpha comes from backgroundOpacity.
        std::string backgroundColor = "#000000";
        float backgroundOpacity = 0.6f;
        // Unlocked = the whole panel is a grab handle; locked = it can't be moved.
        bool panelUnlocked = true;
        // While dragging: 0 = free, 1 = X only, 2 = Y only, 3 = Z only.
        int panelMoveMode = 0;
        float positionX = DefaultPositionX;
        float positionY = DefaultPositionY;
        float positionZ = DefaultPositionZ;
        float rotationX = DefaultRotationX;
        float rotationY = DefaultRotationY;

        // Song requests: viewers type "!bsr <key>" (the PC's !request isn't used).
        bool enableSongRequests = true;
        // 0 = everyone, 1 = moderators + broadcaster, 2 = broadcaster only.
        int whoCanRequest = 0;
        // Between one viewer's requests; the broadcaster and mods are exempt.
        int perUserCooldownSeconds = 30;
        bool allowDuplicates = false;
        // Requests one viewer may have queued at once; 0 = unlimited.
        int maxRequestsPerUser = 2;
        // Keep the queue across game restarts.
        bool persistRequestQueue = true;
    };

    Config& GetConfig();
    void LoadConfig();
    void SaveConfig();
}
