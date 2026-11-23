#pragma once

#include "Chat/ChatMessage.hpp"

#include <string>
#include <string_view>
#include <vector>

// The Filters tab. Set on the main thread, checked on the chat threads.
namespace StandaloneStreamChat::Chat {
    enum class FilterResult {
        Show,
        HideCommand,  // a "!" message kept out of the feed; requests still see it
        Block,        // a blocked user or word: dropped, requests included
    };

    /// "Bob, @Ann,, spam bot " -> {"bob", "ann", "spam bot"}: trimmed and
    /// lowercased, with a leading '@' dropped.
    std::vector<std::string> ParseFilterList(std::string_view text);

    /// Whether `text` holds `phrase` (lowercase) with no letter or digit
    /// touching either end, ignoring ASCII case: "ass" isn't found in "class".
    bool ContainsPhrase(std::string_view text, std::string_view phrase);

    /// Replaces the lists; the comma-separated text is as typed in the settings.
    void SetFilters(std::string_view blockedUsers, std::string_view blockedWords, bool hideCommands);

    /// What the filters do with a viewer's message.
    FilterResult CheckFilters(ChatMessage const& message);
}
