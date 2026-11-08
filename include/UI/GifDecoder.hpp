#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace StandaloneStreamChat::UI {
    inline constexpr size_t MaxAnimationBytes = 512 * 1024;
    inline constexpr size_t MaxResidentAnimationBytes = 2 * 1024 * 1024;
    inline constexpr size_t MaxAnimatedEmotes = 4;
    inline constexpr size_t MaxAnimationFrames = 128;
    struct GifFrame { std::vector<uint8_t> rgba; uint32_t durationMs = 100; };
    struct GifAnimation {
        int width = 0, height = 0;
        std::vector<GifFrame> frames;
        size_t Bytes() const {
            size_t bytes = 0;
            for (auto const& frame : frames) bytes += frame.rgba.size();
            return bytes;
        }
    };
    bool LooksLikeGif(std::span<uint8_t const> data);
    // Composites disposal/transparency, shrinks to atlas size on the worker,
    // samples frames to the byte/count limits, and uses a still image when
    // the source dimension/work budget is exceeded.
    bool DecodeGifAnimation(std::span<uint8_t const> data, GifAnimation& animation);

    /// Decodes only the first frame of a GIF (Kick serves every emote as one,
    /// and Unity can't read them) onto a transparent canvas the size of the
    /// GIF's screen. `rgba` comes back with rows bottom-up, as Unity stores
    /// textures. False for anything malformed or over 1024 px. Plain C++, so
    /// it runs on the download thread.
    bool DecodeGifFirstFrame(std::span<uint8_t const> data, int& width, int& height, std::vector<uint8_t>& rgba);
}
