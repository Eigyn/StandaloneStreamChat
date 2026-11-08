#pragma once

#include "Chat/ChatMessage.hpp"

#include <cstdint>
#include <string>

namespace StandaloneStreamChat::Requests {
    enum class RequestStatus {
        FetchingInfo,  // queued; BeatSaver lookup in flight (Accept disabled)
        Ready,         // map info known; Accept/Deny enabled
        NotFound,      // bad or deleted key; drops out of the queue after a while
        Preparing,     // accepted; switching to the song list before downloading
        Downloading,   // zip download in progress, or waiting its turn
        Installing,    // unpacked; waiting for SongCore to load it
        Failed,        // download/install failed; Accept = retry, Deny = remove
        Installed,     // files present; Accept loads/selects without downloading
        Selecting,     // awaiting the filtered collection and confirmed selection
    };

    /// One queued request. Its position isn't stored: it's the index in the
    /// snapshot, so positions renumber when an entry leaves.
    struct SongRequest {
        int64_t id = 0;   // stable handle for the UI
        std::string key;  // BeatSaver key, lowercase hex
        std::string requester;
        std::string requesterId;  // the platform's user id; empty if it gave none
        Chat::ChatPlatform platform = Chat::ChatPlatform::System;
        // Filled by the BeatSaver lookup.
        std::string songName;
        std::string songAuthor;
        std::string mapper;
        std::string hash;  // lowercase, as SongCore keys levels
        std::string downloadUrl;
        RequestStatus status = RequestStatus::FetchingInfo;
        std::string detail;     // short status text, e.g. "42%"
        float progress = 0.0f;  // download progress, 0..1
    };
}
