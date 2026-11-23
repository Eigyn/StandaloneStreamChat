#include "Chat/ChatFilter.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace StandaloneStreamChat::Chat {
    namespace {
        std::mutex lock;
        FilterList users, words;
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

        bool Listed(FilterList const& list, auto matches) { return std::any_of(list.entries.begin(), list.entries.end(), matches); }
    }

    std::string NormalizeFilterEntry(std::string_view text) { return Lower(WithoutAt(Trim(text))); }

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

    void SetFilters(FilterList newUsers, FilterList newWords, bool hide) {
        std::lock_guard guard(lock);
        users = std::move(newUsers);
        words = std::move(newWords);
        hideCommands = hide;
    }

    FilterResult CheckFilters(ChatMessage const& message) {
        std::lock_guard guard(lock);
        bool userListed = false;
        if (!users.entries.empty()) {
            // YouTube names can carry an '@', which the entries drop.
            auto author = Lower(WithoutAt(message.author));
            auto login = Lower(message.authorLogin);
            userListed = Listed(users, [&](auto const& user) { return user == author || user == login; });
        }
        bool wordListed = Listed(words, [&](auto const& word) { return ContainsPhrase(message.text, word); });
        if ((userListed && !users.allow) || (wordListed && !words.allow))
            return FilterResult::Block;
        if ((users.allow && !users.entries.empty() && !userListed) || (words.allow && !words.entries.empty() && !wordListed))
            return FilterResult::Hide;
        if (hideCommands && !message.isNotice && Trim(message.text).starts_with('!'))
            return FilterResult::Hide;
        return FilterResult::Show;
    }
}
