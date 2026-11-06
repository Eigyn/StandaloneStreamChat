#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

typedef void CURL;

namespace SimpleBSStreamChat::Net {
    /// Blocking HTTPS requests on one reused libcurl handle, so repeated polls
    /// keep the connection (and its TLS session) alive. Not thread-safe.
    /// Transport errors throw std::runtime_error; HTTP errors come back as a status.
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

        explicit HttpClient(std::atomic<bool> const& stopping);
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
