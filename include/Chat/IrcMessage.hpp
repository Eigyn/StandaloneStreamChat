#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace SimpleBSStreamChat::Chat {
    /// Minimal IRCv3 line parser: `@tags :prefix COMMAND params :trailing`.
    /// Pure logic, no I/O.
    struct IrcMessage {
        std::unordered_map<std::string, std::string> tags;
        std::string prefix;
        std::string command;
        /// Middle parameters; the trailing parameter (after " :") is the last entry.
        std::vector<std::string> params;

        /// Nick portion of the prefix ("nick!user@host" -> "nick").
        std::string_view PrefixNick() const;
        std::string_view TrailingOrEmpty() const;
        std::string_view Tag(std::string const& key) const;

        static std::optional<IrcMessage> Parse(std::string_view line);
    };
}
