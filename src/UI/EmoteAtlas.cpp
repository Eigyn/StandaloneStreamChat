#include "UI/EmoteAtlas.hpp"
#include "UI/GifDecoder.hpp"
#include "UI/Icons.hpp"
#include "Net/HttpClient.hpp"
#include "logging.hpp"
#include "Config.hpp"

#include "bsml/shared/Helpers/getters.hpp"

#include "System/Collections/Generic/List_1.hpp"
#include "System/IntPtr.hpp"
#include "TMPro/TMP_Settings.hpp"
#include "TMPro/TMP_Sprite.hpp"
#include "TMPro/TMP_SpriteCharacter.hpp"
#include "TMPro/TMP_SpriteGlyph.hpp"
#include "UnityEngine/Color32.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/ImageConversion.hpp"
#include "UnityEngine/ScriptableObject.hpp"
#include "UnityEngine/TextCore/FaceInfo.hpp"
#include "UnityEngine/TextCore/GlyphMetrics.hpp"
#include "UnityEngine/TextCore/GlyphRect.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/TextureWrapMode.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace StandaloneStreamChat::UI {
    namespace {
        constexpr int AtlasSize = 1024;
        constexpr int CellContent = 64;  // pixel height every image is scaled to
        constexpr int Padding = 2;       // edge pixels repeated around each image
        constexpr int CellPitch = CellContent + 2 * Padding;
        constexpr int GridSize = AtlasSize / CellPitch;  // 15 x 15 cells
        constexpr int MaxCellsWide = 4;                  // wide emotes span up to 4 cells
        constexpr int MaxContentWidth = MaxCellsWide * CellPitch - 2 * Padding;
        // Sprites draw at 90% of the font size.
        constexpr float DisplayScale = 0.9f;
        constexpr int MaxDecodesPerPump = 3;
        constexpr size_t MaxDownloadBytes = 2 * 1024 * 1024;
        // Emotes and badges are at most a few hundred pixels. A small file
        // can still claim huge dimensions, and Unity would allocate them in full.
        constexpr uint32_t MaxDecodeSide = 2048;
        constexpr auto RetryFailedAfter = std::chrono::minutes(5);

        // Width and height from a PNG or JPEG header, read before Unity decodes
        // the image. Empty for anything else, which LoadImage can't read either.
        std::optional<std::pair<uint32_t, uint32_t>> EncodedSize(std::span<unsigned char const> data) {
            auto u16 = [&](size_t i) { return static_cast<uint32_t>(data[i] << 8 | data[i + 1]); };
            auto u32 = [&](size_t i) { return u16(i) << 16 | u16(i + 2); };
            static constexpr unsigned char Png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
            if (data.size() >= 24 && std::equal(std::begin(Png), std::end(Png), data.begin()) &&
                std::string_view(reinterpret_cast<char const*>(&data[12]), 4) == "IHDR")
                return std::pair{u32(16), u32(20)};
            if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8)
                return std::nullopt;
            // JPEG: walk the marker segments to the frame header (SOFn).
            for (size_t i = 2; i + 4 <= data.size();) {
                if (data[i] != 0xFF)
                    return std::nullopt;
                uint8_t marker = data[i + 1];
                if (marker == 0xFF) {  // fill byte
                    i++;
                    continue;
                }
                if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) {  // no length field
                    i += 2;
                    continue;
                }
                size_t length = u16(i + 2);
                if (length < 2)
                    return std::nullopt;
                if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                    if (i + 9 > data.size())
                        return std::nullopt;
                    return std::pair{u16(i + 7), u16(i + 5)};  // stored as height, then width
                }
                i += 2 + length;
            }
            return std::nullopt;
        }

        // Bilinear resize of an RGBA image (rows bottom-up, as Unity stores them).
        std::vector<uint8_t> Resize(uint8_t const* src, int sw, int sh, int dw, int dh) {
            std::vector<uint8_t> out(static_cast<size_t>(dw) * dh * 4);
            for (int y = 0; y < dh; y++) {
                float fy = std::clamp((y + 0.5f) * sh / dh - 0.5f, 0.0f, static_cast<float>(sh - 1));
                int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, sh - 1);
                float ty = fy - y0;
                for (int x = 0; x < dw; x++) {
                    float fx = std::clamp((x + 0.5f) * sw / dw - 0.5f, 0.0f, static_cast<float>(sw - 1));
                    int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, sw - 1);
                    float tx = fx - x0;
                    for (int c = 0; c < 4; c++) {
                        auto at = [&](int px, int py) { return static_cast<float>(src[(static_cast<size_t>(py) * sw + px) * 4 + c]); };
                        float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
                        float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
                        out[(static_cast<size_t>(y) * dw + x) * 4 + c] = static_cast<uint8_t>(std::lround(top + (bottom - top) * ty));
                    }
                }
            }
            return out;
        }

        void KeepLoaded(UnityEngine::Object* object) { object->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset); }

        // Trace the first few emotes end to end (request, download, decode,
        // lookup) at INFO, so a log from the headset shows where one stops.
        std::atomic<int> traceBudget{60};
        bool Trace() { return traceBudget.fetch_sub(1) > 0; }

        // A sprite's name in TMP: short, and unique because glyph indices never repeat.
        std::string SpriteName(uint32_t glyphIndex) { return "ssc" + std::to_string(glyphIndex); }

        std::string Signature(std::vector<uint8_t> const& bytes) {
            std::string hex;
            for (size_t i = 0; i < std::min<size_t>(bytes.size(), 8); i++)
                hex += fmt::format("{:02x}", bytes[i]);
            return hex;
        }
    }

    // One worker, at most 64 queued URLs and one completed result. Backpressure
    // keeps downloaded/decoded images from accumulating while the feed is hidden.
    class EmoteDownloader {
       public:
        struct Result {
            std::string key;
            std::vector<uint8_t> bytes, rgba;
            GifAnimation animation;
            int width = 0, height = 0;
        };
        EmoteDownloader() : _thread(&EmoteDownloader::Run, this) {}
        ~EmoteDownloader() {
            // Set under the lock, so the worker can't miss the wakeup between
            // checking its wait condition and starting to wait.
            { std::lock_guard lock(_lock); _stopping = true; }
            _wake.notify_all(); _thread.join();
        }
        void SetAnimations(bool enabled) {
            _animationsEnabled = enabled; ++_animationEpoch;
            if (!enabled) {
                std::lock_guard lock(_lock);
                for (auto& result : _results) StillFrame(result);
            }
        }
        bool Request(std::string key, std::string url, std::string fallback, bool animate) {
            std::lock_guard lock(_lock);
            if (_jobs.size() >= 64) return false;
            _jobs.push_back({std::move(key), std::move(url), std::move(fallback), animate, _animationEpoch.load()});
            _wake.notify_one();
            return true;
        }
        bool Take(Result& result) {
            std::lock_guard lock(_lock);
            if (_results.empty()) return false;
            result = std::move(_results.front()); _results.pop_front(); _wake.notify_one();
            return true;
        }
       private:
        struct Job { std::string key, url, fallback; bool animate; uint64_t epoch; };
        static void StillFrame(Result& result) {
            if (result.animation.frames.empty()) return;
            result.rgba = std::move(result.animation.frames.front().rgba);
            result.animation = {};
        }
        void Run() {
            Net::HttpClient http(_stopping);
            while (!_stopping) {
                Job job;
                {
                    std::unique_lock lock(_lock);
                    _wake.wait(lock, [this] { return _stopping || (!_jobs.empty() && _results.empty()); });
                    if (_stopping) return;
                    job = std::move(_jobs.front()); _jobs.pop_front();
                }
                Result result; result.key = job.key;
                try {
                    bool animate = job.animate && _animationsEnabled && job.epoch == _animationEpoch;
                    auto fetch = [&](std::string const& url) {
                        return http.Get(url, {}, [](std::string_view body) { return body.size() > MaxDownloadBytes; });
                    };
                    auto response = fetch(animate ? job.url : job.fallback);
                    if ((!response.Ok() || response.body.size() > MaxDownloadBytes) && animate && job.url != job.fallback)
                        response = fetch(job.fallback);
                    // An image over the cap was cut off there. A GIF's first frame
                    // comes first, so a cut-off GIF (a big Kick emote) still shows
                    // as a still image; a cut-off PNG or JPEG can't be decoded.
                    bool truncated = response.body.size() > MaxDownloadBytes;
                    if (response.Ok())
                        result.bytes.assign(response.body.begin(), response.body.end());
                    if (LooksLikeGif(result.bytes)) {
                        if (!truncated && animate && _animationsEnabled && job.epoch == _animationEpoch && DecodeGifAnimation(result.bytes, result.animation) && result.animation.frames.size() > 1) {
                            result.width = result.animation.width; result.height = result.animation.height;
                        } else {
                            result.animation = {};
                            DecodeGifFirstFrame(result.bytes, result.width, result.height, result.rgba);
                        }
                        result.bytes.clear();
                    } else if (truncated) {
                        result.bytes.clear();
                    }
                    if (!_animationsEnabled || job.epoch != _animationEpoch) StillFrame(result);
                } catch (std::exception const& ex) {
                    if (_stopping) return;
                    DEBUG("Emote {}: {}", job.key, ex.what());
                }
                std::lock_guard lock(_lock);
                if (!_animationsEnabled || job.epoch != _animationEpoch) StillFrame(result);
                _results.push_back(std::move(result));
            }
        }
        std::mutex _lock;
        std::condition_variable _wake;
        std::deque<Job> _jobs;
        std::deque<Result> _results;
        std::atomic<bool> _stopping{false}, _animationsEnabled{false};
        std::atomic<uint64_t> _animationEpoch{0};
        std::thread _thread;
    };

    EmoteAtlas& EmoteAtlas::Instance() {
        static EmoteAtlas instance;
        return instance;
    }

    EmoteAtlas::EmoteAtlas()
        : _pixels(static_cast<size_t>(AtlasSize) * AtlasSize * 4, 0),
          _cellOwners(GridSize * GridSize),
          _downloader(std::make_unique<EmoteDownloader>()) {
        auto texture = UnityEngine::Texture2D::New_ctor(AtlasSize, AtlasSize, UnityEngine::TextureFormat::RGBA32, false);
        texture->set_name("StandaloneStreamChatEmotes");
        texture->set_filterMode(UnityEngine::FilterMode::Bilinear);
        texture->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
        KeepLoaded(texture);
        _texture = texture;

        auto decodeTexture = UnityEngine::Texture2D::New_ctor(2, 2, UnityEngine::TextureFormat::RGBA32, false);
        KeepLoaded(decodeTexture);
        _decodeTexture = decodeTexture;

        // The sprite material: a NoGlow copy over the atlas.
        auto material = UnityEngine::Material::New_ctor(BSML::Helpers::GetUINoGlowMat());
        material->set_name("StandaloneStreamChatEmotes");
        material->set_mainTexture(texture);
        KeepLoaded(material);
        _material = material;

        auto asset = UnityEngine::ScriptableObject::CreateInstance<TMPro::TMP_SpriteAsset*>();
        asset->set_name("StandaloneStreamChatEmotes");
        KeepLoaded(asset);
        asset->spriteSheet = texture;
#ifdef SSC_BS_1_40
        asset->material = material;  // a plain field in 1.40.8's TextMeshPro
#else
        asset->set_material(material);
#endif
        // Sprite metrics below are in pixels of a CellContent-high "font".
        auto face = asset->m_FaceInfo;
        face.m_PointSize = CellContent;
        face.m_Scale = 1.0f;
        face.m_LineHeight = CellContent;
        face.m_AscentLine = CellContent * 0.86f;
        face.m_Baseline = 0.0f;
        face.m_DescentLine = -CellContent * 0.14f;
        asset->m_FaceInfo = face;
        asset->m_Version = "1.1.0";  // current format: skips TMP's legacy upgrade
        asset->spriteInfoList = System::Collections::Generic::List_1<TMPro::TMP_Sprite*>::New_ctor();
        asset->fallbackSpriteAssets = System::Collections::Generic::List_1<UnityW<TMPro::TMP_SpriteAsset>>::New_ctor();
        asset->set_spriteCharacterTable(System::Collections::Generic::List_1<TMPro::TMP_SpriteCharacter*>::New_ctor());
        asset->set_spriteGlyphTable(System::Collections::Generic::List_1<TMPro::TMP_SpriteGlyph*>::New_ctor());
        _asset = asset;

        // Chat-line icons, pinned for good.
        auto pin = [this](std::string const& name, std::span<unsigned char const> png) {
            auto key = "icon:" + name;
            auto& entry = _entries[key];
            entry.pinned = true;
            if (!AddPng(key, entry, png)) {
                ERROR("Could not load the {} icon", name);
                entry.status = Status::Unavailable;
            }
        };
        pin("twitch", Icons::PlatformPng(Chat::ChatPlatform::Twitch));
        pin("youtube", Icons::PlatformPng(Chat::ChatPlatform::YouTube));
        pin("kick", Icons::PlatformPng(Chat::ChatPlatform::Kick));
        pin("broadcaster", Icons::BroadcasterPng());
        pin("moderator", Icons::ModeratorPng());
        Upload();
    }

    std::string EmoteAtlas::IconTag(std::string_view icon) {
        auto it = _entries.find("icon:" + std::string(icon));
        return it != _entries.end() && it->second.status == Status::Ready ? it->second.tag : std::string();
    }

    EmoteAtlas::Status EmoteAtlas::Append(std::string& out, Chat::ChatSegment const& emote) {
        if (emote.emoteUrl.empty())
            return Status::Unavailable;

        bool animate = _animationsEnabled && !emote.animatedUrl.empty();
        auto key = emote.emoteKey + (animate ? ":animated:" + std::to_string(_animationGeneration) : "");
        if (!_references.Use(key)) return Status::Unavailable;
        if (!_entries.contains(key) && _entries.size() >= 512) {
            auto victim = std::find_if(_entries.begin(), _entries.end(), [&](auto const& item) {
                return !item.second.pinned && !_references.Contains(item.first) && item.second.status == Status::Unavailable;
            });
            if (victim != _entries.end()) _entries.erase(victim);
            else return Status::Unavailable;
        }
        auto [it, inserted] = _entries.try_emplace(key);
        auto& entry = it->second;
        if (inserted) {
            if (!_downloader->Request(key, animate ? emote.animatedUrl : emote.emoteUrl, emote.emoteUrl, animate)) {
                entry.status = Status::Unavailable;
                entry.failedAt = std::chrono::steady_clock::now() - RetryFailedAfter + std::chrono::seconds(5);
            }
            return entry.status;
        }
        if (entry.status == Status::Ready) {
            entry.lastUsed = ++_useCounter;
            out += entry.tag;
        } else if (entry.status == Status::Unavailable && std::chrono::steady_clock::now() - entry.failedAt > RetryFailedAfter) {
            entry.status = Status::Loading;
            if (!_downloader->Request(key, animate ? emote.animatedUrl : emote.emoteUrl, emote.emoteUrl, animate)) {
                entry.status = Status::Unavailable;
                entry.failedAt = std::chrono::steady_clock::now() - RetryFailedAfter + std::chrono::seconds(5);
            }
        }
        return entry.status;
    }

    TMPro::TMP_SpriteAsset* EmoteAtlas::SpriteAsset() {
        // The Unicode core mod draws missing characters (emoji) as sprites in
        // TMP's default sprite asset. Rows use this asset instead, so fall back
        // to that one; it's set once the main menu is up.
        if (!_defaultLinked) {
            if (auto fallback = TMPro::TMP_Settings::get_defaultSpriteAsset(); fallback && fallback.ptr() != _asset.ptr()) {
                _asset->fallbackSpriteAssets->Add(fallback);
                _defaultLinked = true;
            }
        }
        return _asset.ptr();
    }

    bool EmoteAtlas::Pump() {
        ConfigureAnimations(GetConfig().animatedEmotes && GetConfig().emojiImages);
        SpriteAsset();

        std::vector<std::string> addedKeys;
        EmoteDownloader::Result result;
        for (int i = 0; i < MaxDecodesPerPump && _downloader->Take(result); i++) {
            auto it = _entries.find(result.key);
            if (it == _entries.end())
                continue;
            bool added = !result.animation.frames.empty() ? Insert(result.key, it->second, result.animation.frames.front().rgba.data(), result.width, result.height)
                         : !result.rgba.empty() ? Insert(result.key, it->second, result.rgba.data(), result.width, result.height)
                         : !result.bytes.empty() && AddPng(result.key, it->second, result.bytes);
            if (added) {
                size_t bytes = 0, count = 0;
                for (auto const& [key, entry] : _entries) {
                    bytes += entry.animation.Bytes(); count += entry.animation.frames.size() > 1;
                }
                if (_animationsEnabled && _references.Contains(result.key) && result.animation.frames.size() > 1 &&
                    count < MaxAnimatedEmotes && bytes + result.animation.Bytes() <= MaxResidentAnimationBytes) {
                    it->second.animation = std::move(result.animation);
                    it->second.nextFrame = std::chrono::steady_clock::now() + std::chrono::milliseconds(it->second.animation.frames.front().durationMs);
                    INFO("Animation {}: {} frames, {} bytes; resident {} clips/{} bytes", result.key, it->second.animation.frames.size(), it->second.animation.Bytes(), count + 1, bytes + it->second.animation.Bytes());
                }
                addedKeys.push_back(result.key);
            } else {
                if (Trace())
                    INFO("Emote {}: not added ({} bytes, {}x{} decoded)", result.key, result.bytes.size(), result.width, result.height);
                it->second.status = Status::Unavailable;
                it->second.failedAt = std::chrono::steady_clock::now();
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (_animationsEnabled && now >= _nextAnimationTick) {
            _nextAnimationTick = now + std::chrono::milliseconds(100);
            for (auto& [key, entry] : _entries) {
                if (entry.animation.frames.size() < 2 || !_references.Contains(key) || now < entry.nextFrame) continue;
                entry.frame = (entry.frame + 1) % entry.animation.frames.size();
                auto const& frame = entry.animation.frames[entry.frame];
                Paint(entry, frame.rgba.data(), entry.animation.width, entry.animation.height);
                entry.nextFrame = now + std::chrono::milliseconds(frame.durationMs);
            }
        }
        if (_pixelsDirty || _tablesDirty)
            Upload();
        for (auto const& key : addedKeys) {
            auto it = _entries.find(key);
            if (it != _entries.end() && Trace()) {
                auto name = SpriteName(it->second.glyphIndex);
                INFO("Emote {}: added as {}, TMP finds it at index {}", key, name, _asset->GetSpriteIndexFromName(name));
            }
        }
        return !addedKeys.empty();
    }

    void EmoteAtlas::RetainRows(std::span<uint64_t const> rows) {
        _references.Retain(rows);
        for (auto& [key, entry] : _entries)
            if (!_references.Contains(key)) entry.animation = {};
    }

    void EmoteAtlas::ConfigureAnimations(bool enabled) {
        if (_animationsEnabled == enabled) return;
        _animationsEnabled = enabled;
        if (enabled) ++_animationGeneration;
        _downloader->SetAnimations(enabled);
        INFO("Animated emotes {}", enabled ? "enabled (4 clips / 2 MiB frame budget)" : "disabled; retained frames released");
        if (!enabled) {
            for (auto& [key, entry] : _entries) {
                if (entry.animation.frames.empty()) continue;
                Paint(entry, entry.animation.frames.front().rgba.data(), entry.animation.width, entry.animation.height);
                entry.animation = {}; entry.frame = 0;
            }
            if (_pixelsDirty) Upload();
        }
    }

    // PNG or JPEG through Unity's decoder (main thread only).
    bool EmoteAtlas::AddPng(std::string const& key, Entry& entry, std::span<unsigned char const> png) {
        if (png.empty())
            return false;
        auto size = EncodedSize(png);
        if (!size || size->first == 0 || size->second == 0 || size->first > MaxDecodeSide || size->second > MaxDecodeSide) {
            if (Trace())
                INFO("Emote {}: refused before decoding ({})", key, size ? fmt::format("{}x{}", size->first, size->second) : "not PNG or JPEG");
            return false;
        }
        ArrayW<uint8_t> data(png.size());
        std::copy(png.begin(), png.end(), data.begin());
        auto decode = _decodeTexture.ptr();
        if (!UnityEngine::ImageConversion::LoadImage(decode, data)) {
            if (Trace())
                INFO("Emote {}: LoadImage failed", key);
            return false;
        }
        int width = decode->get_width(), height = decode->get_height();
        auto pixels = decode->GetPixels32();
        static_assert(sizeof(UnityEngine::Color32) == 4);
        if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height) {
            if (Trace())
                INFO("Emote {}: decoded {}x{} with {} pixels", key, width, height, pixels.size());
            return false;
        }
        bool inserted = Insert(key, entry, reinterpret_cast<uint8_t const*>(pixels.begin()), width, height);
        if (!inserted && Trace())
            INFO("Emote {}: no room in the atlas", key);
        return inserted;
    }

    bool EmoteAtlas::Insert(std::string const& key, Entry& entry, uint8_t const* rgba, int width, int height) {
        // Scale to the cell height, keeping the aspect; very wide images shrink to fit.
        int contentHeight = CellContent;
        int contentWidth = std::max(1, static_cast<int>(std::lround(width * static_cast<float>(contentHeight) / height)));
        if (contentWidth > MaxContentWidth) {
            contentWidth = MaxContentWidth;
            contentHeight = std::max(1, static_cast<int>(std::lround(height * static_cast<float>(contentWidth) / width)));
        }
        int cells = (contentWidth + 2 * Padding + CellPitch - 1) / CellPitch;
        int cell = Allocate(cells);
        if (cell < 0)
            cell = EvictAndAllocate(cells);
        if (cell < 0)
            return false;

        entry.cell = cell; entry.cells = cells;
        entry.contentWidth = contentWidth; entry.contentHeight = contentHeight;
        Paint(entry, rgba, width, height);
        int originX = (cell % GridSize) * CellPitch, originY = (cell / GridSize) * CellPitch;
        for (int i = 0; i < cells; i++)
            _cellOwners[cell + i] = key;

        float metricsWidth = contentWidth * DisplayScale, metricsHeight = contentHeight * DisplayScale;
        auto glyph = TMPro::TMP_SpriteGlyph::New_ctor(
            _nextGlyphIndex++,
            UnityEngine::TextCore::GlyphMetrics(metricsWidth, metricsHeight, 0.0f, metricsHeight * (0.80f / 0.90f), metricsWidth * 1.08f),
            UnityEngine::TextCore::GlyphRect(originX + Padding, originY + Padding, contentWidth, contentHeight), 1.0f, 0);
        auto character = TMPro::TMP_SpriteCharacter::New_ctor(0xFFFE, glyph);
        // TMP ignores a rich-text tag longer than 128 characters and prints it
        // as text, and keys built from badge URLs can be that long. So each
        // sprite gets a short name of its own, unique for the session.
        auto name = SpriteName(glyph->get_index());
        character->set_name(name);
        character->set_scale(1.0f);
        _asset->get_spriteGlyphTable()->Add(glyph);
        _asset->get_spriteCharacterTable()->Add(character);
        _tablesDirty = true;
        entry.status = Status::Ready;
        entry.tag = "<sprite name=\"" + name + "\">";
        entry.glyphIndex = glyph->get_index();
        entry.lastUsed = ++_useCounter;
        return true;
    }

    void EmoteAtlas::Paint(Entry const& entry, uint8_t const* rgba, int width, int height) {
        int cell = entry.cell, cells = entry.cells;
        int contentWidth = entry.contentWidth, contentHeight = entry.contentHeight;
        auto scaled = Resize(rgba, width, height, contentWidth, contentHeight);
        int originX = (cell % GridSize) * CellPitch, originY = (cell / GridSize) * CellPitch;
        // Clear the whole span, then write the image with its edge pixels
        // repeated into the padding so filtering never picks up a neighbour.
        for (int y = 0; y < CellPitch; y++) {
            for (int x = 0; x < cells * CellPitch; x++) {
                auto dst = &_pixels[(static_cast<size_t>(originY + y) * AtlasSize + originX + x) * 4];
                if (x >= contentWidth + 2 * Padding || y >= contentHeight + 2 * Padding) {
                    std::fill(dst, dst + 4, 0);
                    continue;
                }
                int sx = std::clamp(x - Padding, 0, contentWidth - 1), sy = std::clamp(y - Padding, 0, contentHeight - 1);
                std::copy_n(&scaled[(static_cast<size_t>(sy) * contentWidth + sx) * 4], 4, dst);
            }
        }
        _pixelsDirty = true;
    }

    int EmoteAtlas::Allocate(int cells) const {
        for (int row = 0; row < GridSize; row++) {
            for (int column = 0; column + cells <= GridSize; column++) {
                int first = row * GridSize + column;
                if (std::all_of(_cellOwners.begin() + first, _cellOwners.begin() + first + cells, [](auto const& owner) { return owner.empty(); }))
                    return first;
            }
        }
        return -1;
    }

    // Frees the least recently shown emotes until `cells` adjacent cells open up.
    int EmoteAtlas::EvictAndAllocate(int cells) {
        while (true) {
            std::string const* victim = nullptr;
            uint64_t oldest = UINT64_MAX;
            for (auto const& [key, entry] : _entries) {
                if (entry.status == Status::Ready && !entry.pinned && !_references.Contains(key) && entry.lastUsed < oldest) {
                    oldest = entry.lastUsed;
                    victim = &key;
                }
            }
            if (!victim)
                return -1;
            Remove(std::string(*victim));
            int cell = Allocate(cells);
            if (cell >= 0)
                return cell;
        }
    }

    void EmoteAtlas::Remove(std::string const& key) {
        auto it = _entries.find(key);
        if (it == _entries.end())
            return;
        auto& entry = it->second;
        for (int i = 0; i < entry.cells; i++)
            _cellOwners[entry.cell + i].clear();

        auto glyphs = _asset->get_spriteGlyphTable();
        for (int i = glyphs->get_Count() - 1; i >= 0; i--) {
            if (glyphs->get_Item(i)->get_index() == entry.glyphIndex) {
                glyphs->RemoveAt(i);
                break;
            }
        }
        auto characters = _asset->get_spriteCharacterTable();
        for (int i = characters->get_Count() - 1; i >= 0; i--) {
            if (characters->get_Item(i)->get_glyphIndex() == entry.glyphIndex) {
                characters->RemoveAt(i);
                break;
            }
        }
        _tablesDirty = true;
        _entries.erase(it);  // asked for again later, it downloads again
    }

    void EmoteAtlas::Upload() {
        auto texture = _texture.ptr();
        if (_pixelsDirty) {
            texture->LoadRawTextureData(System::IntPtr(_pixels.data()), static_cast<int>(_pixels.size()));
            texture->Apply(false);
            _pixelsDirty = false;
        }
        if (_tablesDirty) {
            _asset->UpdateLookupTables();
            _tablesDirty = false;
        }
    }
}
