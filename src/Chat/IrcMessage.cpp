#include "Chat/IrcMessage.hpp"
#include "Chat/ChatMessage.hpp"

#include <array>
#include <cctype>

namespace StandaloneStreamChat::Chat {
    namespace {
        // IRCv3 tag value escaping: \: -> ';', \s -> ' ', \\ -> '\', \r, \n.
        std::string UnescapeTagValue(std::string_view value) {
            if (value.find('\\') == std::string_view::npos)
                return std::string(value);

            std::string out;
            out.reserve(value.size());
            for (size_t i = 0; i < value.size(); i++) {
                char c = value[i];
                if (c != '\\') {
                    out.push_back(c);
                    continue;
                }
                if (++i >= value.size())
                    break;  // lone trailing backslash is dropped per spec
                switch (value[i]) {
                    case ':': out.push_back(';'); break;
                    case 's': out.push_back(' '); break;
                    case '\\': out.push_back('\\'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'n': out.push_back('\n'); break;
                    default: out.push_back(value[i]); break;
                }
            }
            return out;
        }

        void SkipSpaces(std::string_view line, size_t& pos) {
            while (pos < line.size() && line[pos] == ' ')
                pos++;
        }
    }

    std::string_view IrcMessage::PrefixNick() const {
        std::string_view p = prefix;
        auto bang = p.find('!');
        return bang == std::string_view::npos ? p : p.substr(0, bang);
    }

    std::string_view IrcMessage::TrailingOrEmpty() const {
        return params.empty() ? std::string_view() : std::string_view(params.back());
    }

    std::string_view IrcMessage::Tag(std::string const& key) const {
        auto it = tags.find(key);
        return it == tags.end() ? std::string_view() : std::string_view(it->second);
    }

    std::optional<IrcMessage> IrcMessage::Parse(std::string_view line) {
        if (line.empty())
            return std::nullopt;

        IrcMessage msg;
        size_t pos = 0;

        if (line[pos] == '@') {
            auto end = line.find(' ', pos);
            if (end == std::string_view::npos)
                return std::nullopt;
            auto tagsPart = line.substr(1, end - 1);
            size_t start = 0;
            while (start <= tagsPart.size()) {
                auto semi = tagsPart.find(';', start);
                auto pair = tagsPart.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start);
                if (!pair.empty()) {
                    auto eq = pair.find('=');
                    if (eq == std::string_view::npos)
                        msg.tags[std::string(pair)] = "";
                    else
                        msg.tags[std::string(pair.substr(0, eq))] = UnescapeTagValue(pair.substr(eq + 1));
                }
                if (semi == std::string_view::npos)
                    break;
                start = semi + 1;
            }
            pos = end + 1;
        }

        SkipSpaces(line, pos);

        if (pos < line.size() && line[pos] == ':') {
            auto end = line.find(' ', pos);
            if (end == std::string_view::npos)
                return std::nullopt;
            msg.prefix = std::string(line.substr(pos + 1, end - pos - 1));
            pos = end + 1;
            SkipSpaces(line, pos);
        }

        if (pos >= line.size())
            return std::nullopt;

        auto cmdEnd = line.find(' ', pos);
        if (cmdEnd == std::string_view::npos) {
            msg.command = std::string(line.substr(pos));
            pos = line.size();
        } else {
            msg.command = std::string(line.substr(pos, cmdEnd - pos));
            pos = cmdEnd + 1;
        }

        while (pos < line.size()) {
            SkipSpaces(line, pos);
            if (pos >= line.size())
                break;
            if (line[pos] == ':') {
                msg.params.emplace_back(line.substr(pos + 1));
                break;
            }
            auto end = line.find(' ', pos);
            if (end == std::string_view::npos) {
                msg.params.emplace_back(line.substr(pos));
                break;
            }
            msg.params.emplace_back(line.substr(pos, end - pos));
            pos = end + 1;
        }

        return msg;
    }

    std::string NameColorFor(std::string_view name) {
        static constexpr std::array<char const*, 15> palette = {
            "#FF0000", "#0000FF", "#00FF00", "#B22222", "#FF7F50",
            "#9ACD32", "#FF4500", "#2E8B57", "#DAA520", "#D2691E",
            "#5F9EA0", "#1E90FF", "#FF69B4", "#8A2BE2", "#00FF7F",
        };
        // A stable hash of the name's UTF-8 bytes.
        int hash = 0;
        for (unsigned char c : name)
            hash = (hash * 31 + c) & 0x7FFFFFFF;
        return palette[hash % palette.size()];
    }

    std::string StableKey(std::string_view value) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string key;
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '_' || c == '-') {
                key.push_back(static_cast<char>(c));
            } else {
                key.push_back(hex[c >> 4]);
                key.push_back(hex[c & 0xF]);
            }
        }
        return key.empty() ? "unknown" : key;
    }
}
