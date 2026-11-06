#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace SimpleBSStreamChat::UI {
    /// Chat text (and anything else viewers control) is untrusted: wraps it so
    /// TMP shows it literally instead of reading it as rich-text tags.
    inline std::string Escape(std::string_view text) {
        std::string body(text);
        static constexpr std::string_view close = "</noparse>";
        for (size_t i = 0; i + close.size() <= body.size();) {
            bool match = true;
            for (size_t j = 0; j < close.size() && match; j++)
                match = std::tolower(static_cast<unsigned char>(body[i + j])) == close[j];
            if (match)
                body.erase(i, close.size());
            else
                i++;
        }
        return "<noparse>" + body + "</noparse>";
    }
}
