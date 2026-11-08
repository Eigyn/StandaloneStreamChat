#include "UI/Icons.hpp"

#include "bsml/shared/Helpers/utilities.hpp"

#include "System/IntPtr.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/SpriteMeshType.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector4.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace StandaloneStreamChat::UI::Icons {
    namespace {
        constexpr unsigned char LockPng[] = {
#include "embedded/icon-lock.png.inc"
        };
        constexpr unsigned char UnlockPng[] = {
#include "embedded/icon-unlock.png.inc"
        };
        constexpr unsigned char MoveFreePng[] = {
#include "embedded/icon-move-free.png.inc"
        };
        constexpr unsigned char MoveXPng[] = {
#include "embedded/icon-move-x.png.inc"
        };
        constexpr unsigned char MoveYPng[] = {
#include "embedded/icon-move-y.png.inc"
        };
        constexpr unsigned char MoveZPng[] = {
#include "embedded/icon-move-z.png.inc"
        };
        constexpr unsigned char TwitchPng[] = {
#include "embedded/icon-twitch.png.inc"
        };
        constexpr unsigned char YouTubePng[] = {
#include "embedded/icon-youtube.png.inc"
        };
        constexpr unsigned char KickPng[] = {
#include "embedded/kick-icon.png.inc"
        };
        constexpr unsigned char BroadcasterIconPng[] = {
#include "embedded/icon-broadcaster.png.inc"
        };
        constexpr unsigned char ModeratorIconPng[] = {
#include "embedded/icon-moderator.png.inc"
        };
        constexpr unsigned char AcceptPng[] = {
#include "embedded/accept.png.inc"
        };
        constexpr unsigned char DenyPng[] = {
#include "embedded/deny.png.inc"
        };
        constexpr unsigned char RequestsPng[] = {
#include "embedded/icon-requests.png.inc"
        };
        constexpr unsigned char RequestsUnreadPng[] = {
#include "embedded/icon-requests-notification.png.inc"
        };
        constexpr unsigned char ChatPng[] = {
#include "embedded/icon-chat.png.inc"
        };

        UnityEngine::Sprite* Load(std::span<unsigned char const> png) {
            ArrayW<uint8_t> bytes(png.size());
            std::copy(png.begin(), png.end(), bytes.begin());
            auto sprite = BSML::Utilities::LoadSpriteRaw(bytes);
            sprite->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
            sprite->get_texture()->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
            return sprite;
        }

        // Reloads if Unity destroyed the cached sprite anyway.
        UnityEngine::Sprite* Cached(UnityW<UnityEngine::Sprite>& slot, std::span<unsigned char const> png) {
            if (!slot)
                slot = Load(png);
            return slot.ptr();
        }

        // A white `size`-square texture whose alpha at each pixel centre is
        // `coverage(x, y)` (y up), clamped to 0..1.
        template <typename Coverage>
        UnityEngine::Texture2D* DrawTexture(int size, Coverage coverage) {
            std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4, 255);
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    float alpha = std::clamp(coverage(x + 0.5f, y + 0.5f), 0.0f, 1.0f);
                    pixels[(static_cast<size_t>(y) * size + x) * 4 + 3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
                }
            }
            auto texture = UnityEngine::Texture2D::New_ctor(size, size, UnityEngine::TextureFormat::RGBA32, false);
            texture->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
            texture->LoadRawTextureData(System::IntPtr(pixels.data()), static_cast<int>(pixels.size()));
            texture->Apply(false, true);
            texture->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
            return texture;
        }

        template <typename Coverage>
        UnityEngine::Sprite* Draw(int size, Coverage coverage) {
            auto sprite = BSML::Utilities::LoadSpriteFromTexture(DrawTexture(size, coverage));
            sprite->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
            return sprite;
        }
    }

    UnityEngine::Sprite* Lock() {
        static UnityW<UnityEngine::Sprite> sprite;
        return Cached(sprite, LockPng);
    }

    UnityEngine::Sprite* Unlock() {
        static UnityW<UnityEngine::Sprite> sprite;
        return Cached(sprite, UnlockPng);
    }

    UnityEngine::Sprite* Move(int mode) {
        static UnityW<UnityEngine::Sprite> sprites[4];
        static constexpr std::span<unsigned char const> pngs[4] = {MoveFreePng, MoveXPng, MoveYPng, MoveZPng};
        mode = std::clamp(mode, 0, 3);
        return Cached(sprites[mode], pngs[mode]);
    }

    UnityEngine::Sprite* Accept() {
        static UnityW<UnityEngine::Sprite> sprite;
        return Cached(sprite, AcceptPng);
    }

    UnityEngine::Sprite* Deny() {
        static UnityW<UnityEngine::Sprite> sprite;
        return Cached(sprite, DenyPng);
    }

    UnityEngine::Sprite* Requests(bool unread) {
        static UnityW<UnityEngine::Sprite> sprite, unreadSprite;
        return unread ? Cached(unreadSprite, RequestsUnreadPng) : Cached(sprite, RequestsPng);
    }

    UnityEngine::Sprite* ChatMode() {
        static UnityW<UnityEngine::Sprite> sprite;
        return Cached(sprite, ChatPng);
    }

    // The progress ring: 64 px, outer radius 29, inner 21.
    UnityEngine::Sprite* Ring() {
        static UnityW<UnityEngine::Sprite> sprite;
        if (!sprite) {
            sprite = Draw(64, [](float x, float y) {
                float distance = std::hypot(x - 32.0f, y - 32.0f);
                return std::min(29.0f - distance, distance - 21.0f);
            });
        }
        return sprite.ptr();
    }

    UnityEngine::Sprite* ArrowUp() {
        static UnityW<UnityEngine::Sprite> sprite;
        if (!sprite) {
            // A triangle pointing up; coverage is the distance inside its
            // nearest edge, which antialiases the outline.
            sprite = Draw(64, [](float x, float y) {
                constexpr float apexY = 52.0f, baseY = 14.0f, halfBase = 22.0f;
                float slopeLength = std::hypot(halfBase, apexY - baseY);
                float left = ((x - (32.0f - halfBase)) * (apexY - baseY) - (y - baseY) * halfBase) / slopeLength;
                float right = (((32.0f + halfBase) - x) * (apexY - baseY) - (y - baseY) * halfBase) / slopeLength;
                return std::min({y - baseY, left, right}) + 0.5f;
            });
        }
        return sprite.ptr();
    }

    UnityEngine::Sprite* RoundedPanel(float radius, float referencePixelsPerUnit) {
        if (radius <= 0.0f || referencePixelsPerUnit <= 0.0f)
            return nullptr;
        // One antialiased rounded square, sliced so only its corners keep
        // their shape. The corner's on-screen size comes from the sprite's
        // pixels-per-unit: a sliced border of Corner px spans
        // Corner / (ppu / reference) units, so ppu = Corner * reference / radius.
        constexpr int Corner = 32;
        constexpr int Size = Corner * 2 + 2;
        static UnityW<UnityEngine::Texture2D> texture;
        static std::map<std::pair<int, int>, UnityW<UnityEngine::Sprite>> sprites;  // by (radius, reference) in hundredths
        if (!texture) {
            sprites.clear();
            texture = DrawTexture(Size, [](float x, float y) {
                float dx = std::max({Corner - x, 0.0f, x - (Size - Corner)});
                float dy = std::max({Corner - y, 0.0f, y - (Size - Corner)});
                return Corner - std::hypot(dx, dy) + 0.5f;
            });
        }
        auto& sprite = sprites[{static_cast<int>(std::lround(radius * 100.0f)), static_cast<int>(std::lround(referencePixelsPerUnit * 100.0f))}];
        if (!sprite) {
            sprite = UnityEngine::Sprite::Create(texture.ptr(), UnityEngine::Rect(0.0f, 0.0f, Size, Size), UnityEngine::Vector2(0.5f, 0.5f),
                                                 Corner * referencePixelsPerUnit / radius, 0, UnityEngine::SpriteMeshType::FullRect,
                                                 UnityEngine::Vector4(Corner, Corner, Corner, Corner));
            sprite->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
        }
        return sprite.ptr();
    }

    UnityEngine::Sprite* Platform(Chat::ChatPlatform platform) {
        static UnityW<UnityEngine::Sprite> twitch, youTube, kick;
        switch (platform) {
            case Chat::ChatPlatform::Twitch:
                return Cached(twitch, TwitchPng);
            case Chat::ChatPlatform::YouTube:
                return Cached(youTube, YouTubePng);
            case Chat::ChatPlatform::Kick:
                return Cached(kick, KickPng);
            case Chat::ChatPlatform::System:
                break;
        }
        return nullptr;
    }

    std::span<unsigned char const> PlatformPng(Chat::ChatPlatform platform) {
        switch (platform) {
            case Chat::ChatPlatform::Twitch:
                return TwitchPng;
            case Chat::ChatPlatform::YouTube:
                return YouTubePng;
            case Chat::ChatPlatform::Kick:
                return KickPng;
            case Chat::ChatPlatform::System:
                break;
        }
        return {};
    }

    std::span<unsigned char const> BroadcasterPng() { return BroadcasterIconPng; }
    std::span<unsigned char const> ModeratorPng() { return ModeratorIconPng; }
}
