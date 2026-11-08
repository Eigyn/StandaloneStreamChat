#include "UI/GifDecoder.hpp"

#include <array>
#include <algorithm>
#include <cmath>

// GIF89a: https://www.w3.org/Graphics/GIF/spec-gif89a.txt
namespace StandaloneStreamChat::UI {
    namespace {
        constexpr int MaxSide = 1024;

        struct Reader {
            std::span<uint8_t const> data;
            size_t pos = 0;

            bool Has(size_t count) const { return pos + count <= data.size(); }
            uint8_t U8() { return data[pos++]; }
            int U16() {
                int value = data[pos] | (data[pos + 1] << 8);
                pos += 2;
                return value;
            }
        };

        // Data sub-blocks (each length-prefixed, ending at a zero length),
        // appended to `out` when given. False if the data ends first.
        bool ReadSubBlocks(Reader& reader, std::vector<uint8_t>* out) {
            while (reader.Has(1)) {
                size_t length = reader.U8();
                if (length == 0)
                    return true;
                if (!reader.Has(length))
                    return false;
                if (out)
                    out->insert(out->end(), reader.data.begin() + reader.pos, reader.data.begin() + reader.pos + length);
                reader.pos += length;
            }
            return false;
        }

        // Variable-width LZW (codes packed least significant bit first) into
        // palette indices. Stops after `count`; rejects incomplete pixel data.
        bool LzwDecode(std::vector<uint8_t> const& bytes, int minCodeSize, size_t count, std::vector<uint8_t>& indices) {
            if (minCodeSize < 2 || minCodeSize > 8)
                return false;
            constexpr int MaxCodes = 4096;
            constexpr uint16_t NoPrefix = 0xFFFF;
            const int clear = 1 << minCodeSize, end = clear + 1;

            std::array<uint16_t, MaxCodes> prefix;  // code -> code of its string minus the last index
            std::array<uint8_t, MaxCodes> suffix;   // code -> last index of its string
            std::array<uint8_t, MaxCodes> first;    // code -> first index of its string
            for (int i = 0; i < clear; i++) {
                prefix[i] = NoPrefix;
                suffix[i] = static_cast<uint8_t>(i);
                first[i] = static_cast<uint8_t>(i);
            }

            int codeSize = minCodeSize + 1;
            int next = end + 1;
            int previous = -1;
            std::vector<uint8_t> stack;
            stack.reserve(MaxCodes);
            indices.clear();
            indices.reserve(count);

            const size_t totalBits = bytes.size() * 8;
            size_t bit = 0;
            while (indices.size() < count && bit + codeSize <= totalBits) {
                int code = 0;
                for (int b = 0; b < codeSize; b++, bit++)
                    code |= ((bytes[bit >> 3] >> (bit & 7)) & 1) << b;

                if (code == clear) {
                    codeSize = minCodeSize + 1;
                    next = end + 1;
                    previous = -1;
                    continue;
                }
                if (code == end)
                    break;
                if (previous < 0) {
                    // First code after a clear: a bare index, nothing to add.
                    if (code >= clear)
                        return false;
                    indices.push_back(static_cast<uint8_t>(code));
                    previous = code;
                    continue;
                }

                // Output the string for `code`; a code not yet in the table
                // (code == next) is the previous string plus its own first index.
                int emit;
                uint8_t firstIndex;
                if (code < next) {
                    emit = code;
                    firstIndex = first[code];
                } else if (code == next) {
                    emit = previous;
                    firstIndex = first[previous];
                } else {
                    return false;
                }
                stack.clear();
                for (int c = emit; c != NoPrefix; c = prefix[c]) {
                    stack.push_back(suffix[c]);
                    if (stack.size() > MaxCodes)
                        return false;
                }
                for (auto it = stack.rbegin(); it != stack.rend() && indices.size() < count; ++it)
                    indices.push_back(*it);
                if (code == next && indices.size() < count)
                    indices.push_back(firstIndex);

                // New entry: previous string plus the first index of this one.
                // A full table keeps decoding without new entries until a clear.
                if (next < MaxCodes) {
                    prefix[next] = static_cast<uint16_t>(previous);
                    suffix[next] = firstIndex;
                    first[next] = first[previous];
                    next++;
                    if (next == (1 << codeSize) && codeSize < 12)
                        codeSize++;
                }
                previous = code;
            }
            return indices.size() == count;
        }

        // Interlaced images store rows in four passes: every 8th row from 0,
        // every 8th from 4, every 4th from 2, then every 2nd from 1.
        int InterlacedRow(int index, int height) {
            int pass = (height + 7) / 8;
            if (index < pass)
                return index * 8;
            index -= pass;
            pass = (height + 3) / 8;
            if (index < pass)
                return 4 + index * 8;
            index -= pass;
            pass = (height + 1) / 4;
            if (index < pass)
                return 2 + index * 4;
            index -= pass;
            return 1 + index * 2;
        }

        std::vector<uint8_t> ReadPalette(Reader& reader, uint8_t flags) {
            size_t bytes = 3u << ((flags & 0x07) + 1);
            if (!reader.Has(bytes))
                return {};
            std::vector<uint8_t> palette(reader.data.begin() + reader.pos, reader.data.begin() + reader.pos + bytes);
            reader.pos += bytes;
            return palette;
        }
    }

    bool LooksLikeGif(std::span<uint8_t const> data) {
        return data.size() >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8' && (data[4] == '7' || data[4] == '9') &&
               data[5] == 'a';
    }

    bool DecodeGifFirstFrame(std::span<uint8_t const> data, int& width, int& height, std::vector<uint8_t>& rgba) {
        if (!LooksLikeGif(data))
            return false;
        Reader reader{data, 6};
        if (!reader.Has(7))
            return false;

        // Logical screen descriptor.
        width = reader.U16();
        height = reader.U16();
        uint8_t screenFlags = reader.U8();
        reader.pos += 2;  // background index, aspect ratio
        if (width <= 0 || height <= 0 || width > MaxSide || height > MaxSide)
            return false;
        std::vector<uint8_t> globalPalette;
        if (screenFlags & 0x80) {
            globalPalette = ReadPalette(reader, screenFlags);
            if (globalPalette.empty())
                return false;
        }

        int transparent = -1;
        while (reader.Has(1)) {
            uint8_t block = reader.U8();
            if (block == 0x21) {  // extension
                if (!reader.Has(1))
                    return false;
                uint8_t label = reader.U8();
                // Graphic control extension: the only part that matters here
                // is the first frame's transparent index.
                if (label == 0xF9 && reader.Has(5) && reader.data[reader.pos] == 4)
                    transparent = (reader.data[reader.pos + 1] & 0x01) ? reader.data[reader.pos + 4] : -1;
                if (!ReadSubBlocks(reader, nullptr))
                    return false;
            } else if (block == 0x2C) {  // image: the first frame
                if (!reader.Has(9))
                    return false;
                int left = reader.U16(), top = reader.U16(), frameWidth = reader.U16(), frameHeight = reader.U16();
                uint8_t frameFlags = reader.U8();
                auto palette = (frameFlags & 0x80) ? ReadPalette(reader, frameFlags) : globalPalette;
                if (palette.empty() || frameWidth <= 0 || frameHeight <= 0 || frameWidth > MaxSide || frameHeight > MaxSide || !reader.Has(1))
                    return false;
                int minCodeSize = reader.U8();
                std::vector<uint8_t> compressed;
                if (!ReadSubBlocks(reader, &compressed)) return false;

                std::vector<uint8_t> indices;
                if (!LzwDecode(compressed, minCodeSize, static_cast<size_t>(frameWidth) * frameHeight, indices))
                    return false;

                rgba.assign(static_cast<size_t>(width) * height * 4, 0);
                bool interlaced = frameFlags & 0x40;
                size_t colors = palette.size() / 3;
                for (int i = 0; i < frameHeight; i++) {
                    int y = top + (interlaced ? InterlacedRow(i, frameHeight) : i);
                    if (y < 0 || y >= height)
                        continue;
                    for (int x = 0; x < frameWidth; x++) {
                        int canvasX = left + x;
                        if (canvasX >= width)
                            break;
                        int index = indices[static_cast<size_t>(i) * frameWidth + x];
                        if (index == transparent || static_cast<size_t>(index) >= colors)
                            continue;
                        // GIF rows run top-down; Unity's run bottom-up.
                        auto pixel = &rgba[(static_cast<size_t>(height - 1 - y) * width + canvasX) * 4];
                        pixel[0] = palette[index * 3];
                        pixel[1] = palette[index * 3 + 1];
                        pixel[2] = palette[index * 3 + 2];
                        pixel[3] = 255;
                    }
                }
                return true;
            } else {
                return false;  // trailer before any image, or garbage
            }
        }
        return false;
    }

    bool DecodeGifAnimation(std::span<uint8_t const> data, GifAnimation& animation) {
        animation = {};
        if (!LooksLikeGif(data)) return false;
        Reader reader{data, 6};
        if (!reader.Has(7)) return false;
        int width = reader.U16(), height = reader.U16();
        auto flags = reader.U8();
        int background = reader.U8(); reader.U8();
        // At most two 1 MiB source canvases, decoded serially on one worker.
        if (width <= 0 || height <= 0 || width > 512 || height > 512) return false;
        auto global = (flags & 0x80) ? ReadPalette(reader, flags) : std::vector<uint8_t>{};
        animation.height = std::min(32, height);
        animation.width = std::max(1, static_cast<int>(std::lround(width * static_cast<float>(animation.height) / height)));
        if (animation.width > 134) {
            animation.width = 134;
            animation.height = std::max(1, static_cast<int>(std::lround(height * 134.0f / width)));
        }
        size_t frameBytes = static_cast<size_t>(animation.width) * animation.height * 4;
        std::vector<uint8_t> canvas(static_cast<size_t>(width) * height * 4, 0), restore;
        int transparent = -1, disposal = 0, delay = 100;
        int previousDisposal = 0, previousTransparent = -1, previousLeft = 0, previousTop = 0, previousWidth = 0, previousHeight = 0;
        size_t work = 0;
        size_t decodedFrames = 0;
        uint32_t sampleDuration = 100;
        auto firstOnly = [&] { if (animation.frames.size() > 1) animation.frames.resize(1); return !animation.frames.empty(); };
        while (reader.Has(1)) {
            auto block = reader.U8();
            if (block == 0x3B) return !animation.frames.empty();
            if (block == 0x21) {
                if (!reader.Has(1)) return firstOnly();
                auto label = reader.U8();
                std::vector<uint8_t> extension;
                if (!ReadSubBlocks(reader, label == 0xF9 ? &extension : nullptr)) return firstOnly();
                if (label == 0xF9 && extension.size() == 4) {
                    disposal = (extension[0] >> 2) & 7;
                    transparent = (extension[0] & 1) ? extension[3] : -1;
                    delay = std::clamp((extension[1] | (extension[2] << 8)) * 10, 20, 10000);
                }
                continue;
            }
            if (block != 0x2C || !reader.Has(9)) return firstOnly();
            int left = reader.U16(), top = reader.U16(), frameWidth = reader.U16(), frameHeight = reader.U16();
            auto frameFlags = reader.U8();
            if (frameWidth <= 0 || frameHeight <= 0 || frameWidth > 512 || frameHeight > 512 || left >= width || top >= height) return firstOnly();
            work += static_cast<size_t>(frameWidth) * frameHeight;
            if (++decodedFrames > 1024 || work > 8 * 1024 * 1024) return firstOnly();
            auto palette = (frameFlags & 0x80) ? ReadPalette(reader, frameFlags) : global;
            if (palette.empty() || !reader.Has(1)) return firstOnly();
            auto codeSize = reader.U8();
            std::vector<uint8_t> compressed, indices;
            if (!ReadSubBlocks(reader, &compressed) || !LzwDecode(compressed, codeSize, static_cast<size_t>(frameWidth) * frameHeight, indices)) return firstOnly();
            if (previousDisposal == 2) {
                for (int y = previousTop; y < std::min(height, previousTop + previousHeight); y++)
                    for (int x = previousLeft; x < std::min(width, previousLeft + previousWidth); x++) {
                        auto pixel = &canvas[(static_cast<size_t>(y) * width + x) * 4];
                        std::fill_n(pixel, 4, 0);
                        if (previousTransparent < 0 && static_cast<size_t>(background) * 3 + 2 < global.size()) {
                            std::copy_n(global.data() + background * 3, 3, pixel); pixel[3] = 255;
                        }
                    }
            } else if (previousDisposal == 3 && restore.size() == canvas.size()) canvas = restore;
            if (disposal == 3) restore = canvas;
            else restore.clear();
            for (int y = 0; y < frameHeight; y++) {
                int canvasY = top + ((frameFlags & 0x40) ? InterlacedRow(y, frameHeight) : y);
                if (canvasY >= height) continue;
                for (int x = 0; x < frameWidth && left + x < width; x++) {
                    int index = indices[static_cast<size_t>(y) * frameWidth + x];
                    if (index == transparent || static_cast<size_t>(index) * 3 + 2 >= palette.size()) continue;
                    auto pixel = &canvas[(static_cast<size_t>(canvasY) * width + left + x) * 4];
                    std::copy_n(palette.data() + index * 3, 3, pixel); pixel[3] = 255;
                }
            }
            // Sample fast clips at <=10 fps. If the retained set fills, merge
            // adjacent frame durations instead of allocating more frame data.
            if (!animation.frames.empty() && animation.frames.back().durationMs < sampleDuration) {
                animation.frames.back().durationMs += delay;
                previousDisposal = disposal; previousTransparent = transparent;
                previousLeft = left; previousTop = top; previousWidth = frameWidth; previousHeight = frameHeight;
                transparent = -1; disposal = 0; delay = 100;
                continue;
            }
            if (animation.frames.size() >= MaxAnimationFrames || frameBytes * (animation.frames.size() + 1) > MaxAnimationBytes) {
                std::vector<GifFrame> sampled;
                sampled.reserve((animation.frames.size() + 1) / 2);
                for (size_t i = 0; i < animation.frames.size(); i += 2) {
                    auto frame = std::move(animation.frames[i]);
                    if (i + 1 < animation.frames.size()) frame.durationMs += animation.frames[i + 1].durationMs;
                    sampled.push_back(std::move(frame));
                }
                animation.frames = std::move(sampled);
                sampleDuration *= 2;
            }
            GifFrame frame;
            frame.durationMs = delay; frame.rgba.resize(frameBytes);
            for (int y = 0; y < animation.height; y++)
                for (int x = 0; x < animation.width; x++) {
                    auto src = &canvas[(static_cast<size_t>(y * height / animation.height) * width + x * width / animation.width) * 4];
                    auto dst = &frame.rgba[(static_cast<size_t>(animation.height - 1 - y) * animation.width + x) * 4];
                    std::copy_n(src, 4, dst);
                }
            animation.frames.push_back(std::move(frame));
            previousDisposal = disposal; previousTransparent = transparent;
            previousLeft = left; previousTop = top; previousWidth = frameWidth; previousHeight = frameHeight;
            transparent = -1; disposal = 0; delay = 100;
        }
        return firstOnly();
    }
}
