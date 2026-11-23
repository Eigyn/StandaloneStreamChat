#pragma once

#include <string>

namespace StandaloneStreamChat::Net {
    class HttpClient;
}

namespace StandaloneStreamChat::Requests {
    struct MapInfo {
        std::string songName;
        std::string songAuthor;
        std::string mapper;
        std::string hash;  // lowercase
        std::string downloadUrl;
        // For the map filters; negative when BeatSaver didn't say.
        float rating = -1.0f;  // BeatSaver's score, 0..1
        int durationSeconds = -1;
        float lowestNps = -1.0f;  // the newest version's easiest difficulty
    };

    /// Why the map filters turn `map` down ("rated 62%, below 70%"), or empty
    /// if it passes. A 0 limit is off, and a value BeatSaver didn't give passes.
    std::string MapFilterReason(MapInfo const& map, int minRatingPercent, int maxLengthMinutes, float maxNps);

    /// NotFound is a real 404 (or a map with nothing to download), which
    /// drops the request; Error is anything transient, which is retried, so
    /// a BeatSaver outage never purges valid requests.
    enum class LookupOutcome { Found, NotFound, Error };

    struct MapLookup {
        LookupOutcome outcome = LookupOutcome::Error;
        MapInfo info;
        std::string error;
    };

    /// Fetches the map's metadata and newest version from the BeatSaver API.
    /// Blocking; never throws.
    MapLookup LookUpMap(Net::HttpClient& http, std::string const& key);

    /// "User-Agent: StandaloneStreamChat/<version>", which BeatSaver asks API clients to send.
    std::string UserAgentHeader();
}
