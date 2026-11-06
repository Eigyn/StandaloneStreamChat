#pragma once

#include "Chat/ChatMessage.hpp"

#include "UnityEngine/Sprite.hpp"

#include <span>

namespace SimpleBSStreamChat::UI::Icons {
    // Button art from assets/, compiled into the .so. Loaded on first use;
    // Unity's scene-change asset cleanup is told to leave them alone.
    UnityEngine::Sprite* Lock();
    UnityEngine::Sprite* Unlock();
    /// Move-mode arrows: 0 = free, 1 = X only, 2 = Y only, 3 = Z only.
    UnityEngine::Sprite* Move(int mode);
    /// Request row buttons.
    UnityEngine::Sprite* Accept();
    UnityEngine::Sprite* Deny();
    /// The side button's two looks: the request list (marked when new
    /// requests came in unseen), and the chat bubble that switches back.
    UnityEngine::Sprite* Requests(bool unread);
    UnityEngine::Sprite* ChatMode();
    // Drawn at runtime rather than shipped: the download progress ring (an
    // image to fill radially) and the request list's scroll arrow.
    UnityEngine::Sprite* Ring();
    UnityEngine::Sprite* ArrowUp();
    /// A sliced backdrop sprite with `radius`-unit rounded corners on a canvas
    /// with `referencePixelsPerUnit`; null for 0 (square corners). Cached.
    UnityEngine::Sprite* RoundedPanel(float radius, float referencePixelsPerUnit);
    /// Twitch/YouTube/Kick logo; null for System.
    UnityEngine::Sprite* Platform(Chat::ChatPlatform platform);

    // Raw PNG bytes, for packing into the emote atlas (chat-line icons).
    std::span<unsigned char const> PlatformPng(Chat::ChatPlatform platform);  // empty for System
    std::span<unsigned char const> BroadcasterPng();
    std::span<unsigned char const> ModeratorPng();
}
