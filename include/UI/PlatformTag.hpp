#pragma once

#include "Chat/ChatMessage.hpp"

#include <string_view>

namespace SimpleBSStreamChat::UI {
    /// TMP rich-text tag standing in for a platform icon, with a trailing
    /// space; the PC panel's fallback when its icons are unavailable.
    inline std::string_view PlatformTag(Chat::ChatPlatform platform) {
        switch (platform) {
            case Chat::ChatPlatform::Twitch:
                return "<color=#A970FF>[T]</color> ";
            case Chat::ChatPlatform::YouTube:
                return "<color=#FF4444>[Y]</color> ";
            case Chat::ChatPlatform::Kick:
                return "<color=#53FC18>[K]</color> ";
            case Chat::ChatPlatform::System:
                break;
        }
        return "";
    }
}
