#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

typedef void CURL;

namespace StandaloneStreamChat::Net {
    /// What a connection does when the headset's CA certificates can't be read:
    /// BestEffort connects unverified (read-only chat), Required fails (anything
    /// that decides what gets written to disk, such as map lookups and downloads).
    enum class TlsTrust { BestEffort, Required };

    /// Blocking HTTPS requests on one reused libcurl handle, so repeated polls
    /// keep the connection (and its TLS session) alive. Not thread-safe.
    /// Transport errors throw std::runtime_error; HTTP errors come back as a status.
    /// Bodies over MaxResponseBytes throw too.
    class HttpClient {
       public:
        struct Response {
            long status = 0;
            std::string body;
            bool Ok() const { return status >= 200 && status < 300; }
        };

        /// Inspects the body received so far; returning true ends the download
        /// early and keeps what arrived (for huge pages where only the start matters).
        using StopWhen = std::function<bool(std::string_view body)>;

        /// Far above any page or API reply the mod reads; only stops a bad
        /// server from filling memory.
        static constexpr size_t MaxResponseBytes = 16 * 1024 * 1024;

        explicit HttpClient(std::atomic<bool> const& stopping, TlsTrust trust = TlsTrust::BestEffort);
        ~HttpClient();
        HttpClient(HttpClient const&) = delete;
        HttpClient& operator=(HttpClient const&) = delete;

        Response Get(std::string const& url, std::vector<std::string> const& headers = {}, StopWhen stopWhen = {});
        /// POSTs `body` as application/json.
        Response PostJson(std::string const& url, std::string_view body, std::vector<std::string> const& headers = {});

       private:
        Response Perform(std::string const& url, std::vector<std::string> const& headers, StopWhen const& stopWhen);

        CURL* _curl = nullptr;
    };

    /// Percent-encodes everything except RFC 3986 unreserved characters.
    std::string UrlEncode(std::string_view value);
}
