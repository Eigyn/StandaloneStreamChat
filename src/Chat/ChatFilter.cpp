#include "Chat/ChatFilter.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace StandaloneStreamChat::Chat {
    namespace {
        std::mutex lock;
        std::vector<std::string> users, words;
        bool hideCommands = false;

        std::string Lower(std::string_view text) {
            std::string lower(text);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            return lower;
        }

        std::string_view Trim(std::string_view text) {
            auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!text.empty() && isSpace(text.front()))
                text.remove_prefix(1);
            while (!text.empty() && isSpace(text.back()))
                text.remove_suffix(1);
            return text;
        }

        std::string_view WithoutAt(std::string_view name) { return name.starts_with('@') ? Trim(name.substr(1)) : name; }

        // Bytes of UTF-8 characters count as letters, so a word inside a longer non-English word doesn't match.
        bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || (static_cast<unsigned char>(c) & 0x80); }
    }

    std::vector<std::string> ParseFilterList(std::string_view text) {
        std::vector<std::string> list;
        while (true) {
            auto comma = text.find(',');
            auto item = WithoutAt(Trim(text.substr(0, comma)));
            if (!item.empty())
                list.push_back(Lower(item));
            if (comma == std::string_view::npos)
                return list;
            text.remove_prefix(comma + 1);
        }
    }

    bool ContainsPhrase(std::string_view text, std::string_view phrase) {
        if (phrase.empty())
            return false;
        auto lower = Lower(text);
        for (auto at = lower.find(phrase); at != std::string::npos; at = lower.find(phrase, at + 1)) {
            auto end = at + phrase.size();
            // Only a letter or digit at the phrase's own edge needs a boundary there.
            bool startFree = at == 0 || !IsWordChar(phrase.front()) || !IsWordChar(lower[at - 1]);
            bool endFree = end == lower.size() || !IsWordChar(phrase.back()) || !IsWordChar(lower[end]);
            if (startFree && endFree)
                return true;
        }
        return false;
    }

    void SetFilters(std::string_view blockedUsers, std::string_view blockedWords, bool hide) {
        auto newUsers = ParseFilterList(blockedUsers);
        auto newWords = ParseFilterList(blockedWords);
        std::lock_guard guard(lock);
        users = std::move(newUsers);
        words = std::move(newWords);
        hideCommands = hide;
    }

    FilterResult CheckFilters(ChatMessage const& message) {
        std::lock_guard guard(lock);
        if (!users.empty()) {
            // YouTube names can carry an '@', which the list entries drop.
            auto author = Lower(WithoutAt(message.author));
            auto login = Lower(message.authorLogin);
            if (std::any_of(users.begin(), users.end(), [&](auto const& user) { return user == author || user == login; }))
                return FilterResult::Block;
        }
        if (std::any_of(words.begin(), words.end(), [&](auto const& word) { return ContainsPhrase(message.text, word); }))
            return FilterResult::Block;
        if (hideCommands && !message.isNotice && Trim(message.text).starts_with('!'))
            return FilterResult::HideCommand;
        return FilterResult::Show;
    }
}
