#pragma once

#include "Chat/ChatMessage.hpp"

#include <string>
#include <string_view>
#include <vector>

// The Filters tab. Set on the main thread, checked on the chat threads.
namespace StandaloneStreamChat::Chat {
    enum class FilterResult {
        Show,
        Hide,   // kept out of the feed, but requests still see it
        Block,  // on a Blocklist: dropped, requests included
    };

    /// A Blocklist hides what's on it; an Allowlist shows only what's on it,
    /// and lets everything through while it's empty.
    struct FilterList {
        std::vector<std::string> entries;  // from NormalizeFilterEntry
        bool allow = false;
    };

    /// "  @Spam Bot " -> "spam bot": trimmed, lowercased, a leading '@' dropped.
    std::string NormalizeFilterEntry(std::string_view text);

    /// Whether `text` holds `phrase` (lowercase) with no letter or digit
    /// touching either end, ignoring ASCII case: "ass" isn't found in "class".
    bool ContainsPhrase(std::string_view text, std::string_view phrase);

    void SetFilters(FilterList users, FilterList words, bool hideCommands);

    /// What the filters do with a viewer's message. Users match the display
    /// name or login; words match the text.
    FilterResult CheckFilters(ChatMessage const& message);
}
