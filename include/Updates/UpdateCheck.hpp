#pragma once

#include "Chat/Json.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

// Tells the player when a newer release is on GitHub: one request to the
// releases API per game launch, and a system line in the chat feed if the
// latest release is newer than the running mod.
namespace StandaloneStreamChat::Updates {
    /// The GitHub repository releases are published to.
    inline constexpr std::string_view Repository = "Eigyn/StandaloneStreamChat";

    /// "0.10.1", "v0.10.1", or a test build's "0.10.1-2".
    struct Version {
        std::array<unsigned, 3> numbers{};
        std::string suffix;  // after the '-'; empty for a release
    };

    inline std::optional<Version> ParseVersion(std::string_view text) {
        if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
            text.remove_prefix(1);
        Version version;
        for (size_t i = 0; i < version.numbers.size(); i++) {
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), version.numbers[i]);
            if (error != std::errc() || end == text.data())
                return std::nullopt;
            text.remove_prefix(static_cast<size_t>(end - text.data()));
            if (i + 1 < version.numbers.size()) {
                if (text.empty() || text.front() != '.')
                    return std::nullopt;
                text.remove_prefix(1);
            }
        }
        if (text.empty())
            return version;
        if (text.front() != '-' || text.size() < 2 || text.size() > 33)
            return std::nullopt;
        text.remove_prefix(1);
        for (char c : text) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-')
                return std::nullopt;
        }
        version.suffix = std::string(text);
        return version;
    }

    /// True when `latest` comes after `current`. A test build ("0.10.1-2")
    /// comes before the release of the same number ("0.10.1").
    inline bool IsNewer(Version const& latest, Version const& current) {
        if (latest.numbers != current.numbers)
            return latest.numbers > current.numbers;
        if (latest.suffix.empty() || current.suffix.empty())
            return latest.suffix.empty() && !current.suffix.empty();
        unsigned a = 0, b = 0;
        auto [aEnd, aError] = std::from_chars(latest.suffix.data(), latest.suffix.data() + latest.suffix.size(), a);
        auto [bEnd, bError] = std::from_chars(current.suffix.data(), current.suffix.data() + current.suffix.size(), b);
        bool numeric = aError == std::errc() && bError == std::errc() && aEnd == latest.suffix.data() + latest.suffix.size() &&
                       bEnd == current.suffix.data() + current.suffix.size();
        return numeric ? a > b : latest.suffix > current.suffix;
    }

    /// The tag of the release in GitHub's /releases/latest reply, unless it
    /// is missing, a draft or a prerelease.
    inline std::optional<std::string> ReleaseTag(std::string_view reply) {
        rapidjson::Document doc;
        doc.Parse(reply.data(), reply.size());
        if (doc.HasParseError() || !doc.IsObject())
            return std::nullopt;
        for (auto flag : {"draft", "prerelease"}) {
            auto value = Chat::Json::Get(&doc, flag);
            if (value && value->IsBool() && value->GetBool())
                return std::nullopt;
        }
        auto tag = Chat::Json::Scalar(Chat::Json::Get(&doc, "tag_name"));
        if (tag.empty())
            return std::nullopt;
        return tag;
    }

    /// The feed line for a newer release. The link is built from the tag,
    /// which ParseVersion has already limited to version characters.
    inline std::string NoticeText(std::string_view tag) {
        return "Updated mod version available on Github (" + std::string(tag) + "). (https://github.com/" + std::string(Repository) +
               "/releases/tag/" + std::string(tag) + ").";
    }

    /// Starts the check on a background thread, once per launch. Main thread.
    void CheckForUpdate();
}
