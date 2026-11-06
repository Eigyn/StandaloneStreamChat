#pragma once

#include "UnityEngine/Color.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace SimpleBSStreamChat::UI {
    /// "#RRGGBB" (any trailing alpha ignored) as an opaque color, or
    /// `fallback` when malformed.
    inline UnityEngine::Color ColorFromHex(std::string_view hex, UnityEngine::Color fallback) {
        if (hex.size() < 7 || hex[0] != '#')
            return fallback;
        unsigned value = 0;
        for (size_t i = 1; i < 7; i++) {
            char c = hex[i];
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0)
                return fallback;
            value = value * 16 + digit;
        }
        return UnityEngine::Color(((value >> 16) & 0xFF) / 255.0f, ((value >> 8) & 0xFF) / 255.0f, (value & 0xFF) / 255.0f, 1.0f);
    }

    inline std::string HexFromColor(UnityEngine::Color color) {
        auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", channel(color.r), channel(color.g), channel(color.b));
        return buffer;
    }
}
