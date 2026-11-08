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
    };

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
