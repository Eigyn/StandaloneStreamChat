#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace StandaloneStreamChat {
    /// A setting the chat and request threads also read. The settings screen
    /// writes it on the main thread, so loads and stores are atomic.
    template <typename T>
    class Shared {
       public:
        Shared(T value = {}) : _value(value) {}
        Shared(Shared const& other) : _value(other.Get()) {}
        Shared& operator=(Shared const& other) {
            Set(other.Get());
            return *this;
        }
        Shared& operator=(T value) {
            Set(value);
            return *this;
        }
        operator T() const { return Get(); }
        T Get() const { return _value.load(std::memory_order_relaxed); }
        void Set(T value) { _value.store(value, std::memory_order_relaxed); }

       private:
        std::atomic<T> _value;
    };

    // Where "Recenter Window" puts the panel.
    inline constexpr float DefaultPositionX = 0.0f;
    inline constexpr float DefaultPositionY = 4.4f;
    inline constexpr float DefaultPositionZ = 4.0f;
    inline constexpr float DefaultRotationX = -25.0f;
    inline constexpr float DefaultRotationY = 0.0f;

    // The mod's settings, saved as JSON in the Configs folder of the game's
    // external files directory.
    struct Config {
        // Each platform connects once it has a channel and is switched on.
        std::string twitchChannel;  // without '#'
        bool enableTwitch = false;
        std::string youTubeChannel;  // @handle, UC channel id, or 11-char video id
        bool enableYouTube = false;
        std::string kickChannel;  // slug, or numeric chatroom id
        bool enableKick = false;
        bool showPanel = true;
        Shared<bool> showSystemMessages = true;
        Shared<bool> showViewerCounts = true;
        // Size of the viewer-count numbers and their platform icons.
        float viewerCountSize = 5.0f;
        // Platform and broadcaster/mod icons in front of messages; off = just name and message.
        bool showPlatformIcons = true;
        // Stream emotes as inline images; off = their names. (Unicode emoji
        // always draw through the Unicode core mod's font.)
        bool emojiImages = true;
        // Optional on Quest: bounded GIF playback; static images remain the default.
        bool animatedEmotes = false;
        // A dark band behind each message, this opaque.
        bool highlightMessages = true;
        float highlightOpacity = 0.5f;
        float fontSize = 4.0f;
        float panelWidth = 100.0f;
        float panelHeight = 140.0f;
        // 0 = flat; higher = stronger curve (radius = 1200 / amount).
        float curveAmount = 0.0f;
        // Window corner radius in panel units; 0 = square. Quest only. A value
        // below 0 in the file makes the panel take the rounding BSML's backdrop had.
        float cornerRadius = 1.5f;
        // Window backdrop tint as "#RRGGBB"; alpha comes from backgroundOpacity.
        std::string backgroundColor = "#000000";
        float backgroundOpacity = 0.7f;
        // Unlocked = the whole panel is a grab handle; locked = it can't be moved.
        bool panelUnlocked = true;
        // While dragging: 0 = free, 1 = X only, 2 = Y only, 3 = Z only.
        int panelMoveMode = 0;
        float positionX = DefaultPositionX;
        float positionY = DefaultPositionY;
        float positionZ = DefaultPositionZ;
        float rotationX = DefaultRotationX;
        float rotationY = DefaultRotationY;
        // During songs the panel can sit somewhere else; off = the pose above everywhere.
        bool separatePlayingPosition = false;
        float playingPositionX = DefaultPositionX;
        float playingPositionY = DefaultPositionY;
        float playingPositionZ = DefaultPositionZ;
        float playingRotationX = DefaultRotationX;
        float playingRotationY = DefaultRotationY;

        // Song requests: viewers type "!bsr <key>".
        Shared<bool> enableSongRequests = true;
        // 0 = everyone, 1 = moderators + broadcaster, 2 = broadcaster only
        // (Requests::WhoCanRequest); anything else is read as 2.
        Shared<int> whoCanRequest = 0;
        // Between one viewer's requests; the broadcaster and mods are exempt.
        Shared<int> perUserCooldownSeconds = 30;
        Shared<bool> allowDuplicates = false;
        // Requests one viewer may have queued at once; 0 = unlimited.
        Shared<int> maxRequestsPerUser = 2;
        // The queue stops taking viewers' requests at this size (at most 100).
        Shared<int> maxQueueSize = 100;
        // Map filters for viewers' requests, checked once BeatSaver answers; 0 = off.
        Shared<int> minRatingPercent = 0;
        Shared<int> maxLengthMinutes = 0;
        Shared<float> maxNps = 0.0f;  // a map passes if any difficulty is at or below it
        // Keep the queue across game restarts.
        Shared<bool> persistRequestQueue = true;

        // Filters: names and words or phrases, lowercase. Each list is a
        // Blocklist (hide what's on it) or an Allowlist (show only what's on
        // it). The chat threads read the copy in Chat::SetFilters, not these.
        std::vector<std::string> filterUsers;
        bool filterUsersAllow = false;
        std::vector<std::string> filterWords;
        bool filterWordsAllow = false;
        bool hideCommands = false;  // keep "!" messages out of the feed
    };

    Config& GetConfig();
    void LoadConfig();
    void SaveConfig();
    /// Hands the config's filters to the chat threads (Chat::SetFilters).
    void ApplyFilterConfig();

    /// The Configs folder holding the settings and the saved request queue.
    std::filesystem::path ConfigDirectory();
    /// Replaces `path` with `contents` by writing a temporary file and renaming
    /// it over, so a crash or full storage mid-write keeps the previous copy.
    /// False (with `path` untouched) on failure.
    bool WriteFileReplacing(std::filesystem::path const& path, std::string_view contents);
}
