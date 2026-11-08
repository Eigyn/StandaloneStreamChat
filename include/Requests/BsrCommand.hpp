#pragma once

#include <string>
#include <string_view>

namespace StandaloneStreamChat::Requests {
    struct BsrCommand {
        bool isCommand = false;  // the message starts with !bsr (a request attempt)
        std::string key;         // the validated map key; empty when missing or invalid
    };

    /// True if `text` starts with the !bsr command word.
    bool IsBsrCommand(std::string_view text);

    /// Reads "!bsr <key>" or "!bsr https://beatsaver.com/maps/<key>".
    /// !request is deliberately not supported.
    BsrCommand ParseBsr(std::string_view text);

    /// A BeatSaver key: 1-6 lowercase hex digits, no leading zeros.
    bool IsValidKey(std::string_view key);
}
