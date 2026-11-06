#include "Requests/BsrCommand.hpp"

#include <algorithm>
#include <cctype>

namespace SimpleBSStreamChat::Requests {
    namespace {
        constexpr std::string_view Command = "!bsr";

        bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

        std::string_view Trim(std::string_view text) {
            while (!text.empty() && IsSpace(text.front()))
                text.remove_prefix(1);
            while (!text.empty() && IsSpace(text.back()))
                text.remove_suffix(1);
            return text;
        }

        bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
            return text.size() >= prefix.size() &&
                   std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                   });
        }

        // The key in the first word after the command, bare or in a BeatSaver link.
        std::string ExtractKey(std::string_view remainder) {
            auto tokenEnd = std::find_if(remainder.begin(), remainder.end(), IsSpace);
            std::string raw(remainder.begin(), tokenEnd);

            // A pasted BeatSaver URL: take the part after "maps/".
            std::string lower = raw;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (auto maps = lower.find("maps/"); maps != std::string::npos)
                lower.erase(0, maps + 5);
            // Drop any trailing path or query, a leading '!', and leading zeros
            // (BeatSaver keys have none).
            lower.erase(std::min(lower.find_first_of("/?#&"), lower.size()));
            lower.erase(0, std::min(lower.find_first_not_of('!'), lower.size()));
            lower.erase(0, std::min(lower.find_first_not_of('0'), lower.size()));
            return IsValidKey(lower) ? lower : std::string();
        }
    }

    bool IsBsrCommand(std::string_view text) {
        text = Trim(text);
        // A word boundary, so "!bsrfoo" isn't a request.
        return StartsWithIgnoreCase(text, Command) && (text.size() == Command.size() || IsSpace(text[Command.size()]));
    }

    BsrCommand ParseBsr(std::string_view text) {
        if (!IsBsrCommand(text))
            return {};
        auto remainder = Trim(Trim(text).substr(Command.size()));
        return {true, ExtractKey(remainder)};
    }

    bool IsValidKey(std::string_view key) {
        if (key.empty() || key.size() > 6)
            return false;
        return std::all_of(key.begin(), key.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }
}
