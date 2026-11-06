#pragma once
#include <cctype>
#include <cstdint>
#include <charconv>
#include <string_view>

namespace SimpleBSStreamChat::Chat {
    inline uint64_t UnsignedNumber(std::string_view text) {
        uint64_t value = 0;
        auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? value : 0;
    }

    inline bool ContainsMention(std::string_view text, std::string_view login) {
        if (login.starts_with('@')) login.remove_prefix(1);
        if (login.empty()) return false;
        auto word = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
        for (size_t i = 0; i + login.size() < text.size(); i++) {
            if (text[i] != '@' || (i && word(text[i - 1]))) continue;
            size_t j = 0;
            while (j < login.size() && std::tolower(static_cast<unsigned char>(text[i + 1 + j])) == std::tolower(static_cast<unsigned char>(login[j]))) j++;
            if (j == login.size() && (i + 1 + j == text.size() || !word(text[i + 1 + j]))) return true;
        }
        return false;
    }
}
