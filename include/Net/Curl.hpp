#pragma once

#include "libcurl/shared/curl.h"
#include "Net/HttpClient.hpp"

#include <atomic>

namespace StandaloneStreamChat::Net {
    /// Browser-like UA: YouTube and Kick serve different (or no) pages to
    /// unknown clients.
    inline constexpr char const* BrowserUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

    /// A new easy handle verifying TLS against the headset's CA store, whose
    /// transfers abort once `stopping` is set. If the CA store can't be read,
    /// BestEffort connects unverified and Required fails the handshake.
    /// Throws std::runtime_error.
    CURL* NewEasyHandle(std::atomic<bool> const& stopping, TlsTrust trust = TlsTrust::BestEffort);

    /// Throws std::runtime_error("<what>: <curl message>") unless rc is CURLE_OK.
    void Check(CURLcode rc, char const* what);
}
