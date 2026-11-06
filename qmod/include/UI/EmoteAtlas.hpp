#pragma once

#include "Chat/ChatMessage.hpp"
#include "UI/EmoteReferences.hpp"
#include "UI/GifDecoder.hpp"

#include "TMPro/TMP_SpriteAsset.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "Compat.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace SimpleBSStreamChat::UI {
    class EmoteDownloader;

    /// Emote images, plus the chat-line platform and badge icons, packed into
    /// one 1024x1024 texture and exposed to TMP as a sprite asset, so chat text
    /// shows them inline with <sprite name="..."> tags (as the PC build does).
    ///
    /// Emotes download on a background thread the first time they're asked
    /// for; until then (or if they fail) callers show the emote's name. The
    /// least recently shown emotes make room when the texture is full; the
    /// icons are never evicted. Main thread only.
    class EmoteAtlas {
       public:
        enum class Status { Ready, Loading, Unavailable };

        static EmoteAtlas& Instance();

        TMPro::TMP_SpriteAsset* SpriteAsset();
        /// The sprite tag for a built-in icon ("twitch", "youtube", "kick",
        /// "broadcaster", "moderator"), or empty if it failed to load.
        std::string IconTag(std::string_view icon);
        /// Appends the emote's sprite tag when its image is ready. Otherwise
        /// queues the download (once) and says whether it's still coming.
        Status Append(std::string& out, Chat::ChatSegment const& emote);
        /// Turns finished downloads into sprites, a few per call. True if any
        /// were added, so text waiting on them can be rebuilt.
        bool Pump();
        void BeginRow(uint64_t row) { _references.Begin(row); }
        void EndRow() { _references.End(); }
        void RetainRows(std::span<uint64_t const> rows);
        void ConfigureAnimations(bool enabled);

       private:
        struct Entry {
            Status status = Status::Loading;
            std::string tag;  // <sprite name="key">
            int cell = -1;    // first grid cell, and how many cells wide
            int cells = 0;
            uint32_t glyphIndex = 0;
            uint64_t lastUsed = 0;
            bool pinned = false;
            int contentWidth = 0, contentHeight = 0;
            GifAnimation animation;
            size_t frame = 0;
            std::chrono::steady_clock::time_point nextFrame;
            std::chrono::steady_clock::time_point failedAt;
        };

        EmoteAtlas();
        bool AddPng(std::string const& key, Entry& entry, std::span<unsigned char const> png);
        bool Insert(std::string const& key, Entry& entry, uint8_t const* rgba, int width, int height);
        int Allocate(int cells) const;
        int EvictAndAllocate(int cells);
        void Remove(std::string const& key);
        void Upload();
        void Paint(Entry const& entry, uint8_t const* rgba, int width, int height);

        SafeUnityPtr<UnityEngine::Texture2D*> _texture;
        SafeUnityPtr<UnityEngine::Texture2D*> _decodeTexture;  // scratch target for LoadImage
        SafeUnityPtr<UnityEngine::Material*> _material;
        SafeUnityPtr<TMPro::TMP_SpriteAsset*> _asset;
        std::vector<uint8_t> _pixels;           // CPU copy of the texture, RGBA, bottom row first
        std::vector<std::string> _cellOwners;   // emote key per grid cell, empty = free
        std::unordered_map<std::string, Entry> _entries;
        std::unique_ptr<EmoteDownloader> _downloader;
        uint32_t _nextGlyphIndex = 0;
        uint64_t _useCounter = 0;
        bool _tablesDirty = false;
        bool _defaultLinked = false;  // TMP's default sprite asset added as our fallback
        EmoteReferences _references;
        bool _animationsEnabled = false;
        uint64_t _animationGeneration = 0;
        bool _pixelsDirty = false;
        std::chrono::steady_clock::time_point _nextAnimationTick{};
    };
}
