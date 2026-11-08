#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace StandaloneStreamChat::UI {
    /// Chat text (and anything else viewers control) is untrusted: wraps it so
    /// TMP shows it literally instead of reading it as rich-text tags.
    inline std::string Escape(std::string_view text) {
        std::string body;
        body.reserve(text.size());
        static constexpr std::string_view close = "</noparse>";
        for (char c : text) {
            body.push_back(c);
            if (c != '>' || body.size() < close.size())
                continue;
            auto start = body.size() - close.size();
            bool match = true;
            for (size_t j = 0; j < close.size() && match; j++)
                match = std::tolower(static_cast<unsigned char>(body[start + j])) == close[j];
            if (match)
                body.resize(start);
        }
        return "<noparse>" + body + "</noparse>";
    }
}
