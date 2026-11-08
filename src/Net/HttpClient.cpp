#include "Net/HttpClient.hpp"
#include "Net/Curl.hpp"

#include <cctype>
#include <stdexcept>

namespace StandaloneStreamChat::Net {
    namespace {
        struct WriteState {
            std::string* body;
            HttpClient::StopWhen const* stopWhen;
            bool stoppedEarly = false;
            bool tooLarge = false;
        };

        size_t Write(char* data, size_t size, size_t count, void* userdata) {
            auto& state = *static_cast<WriteState*>(userdata);
            if (state.body->size() + size * count > HttpClient::MaxResponseBytes) {
                state.tooLarge = true;
                return 0;  // aborts the transfer with CURLE_WRITE_ERROR
            }
            state.body->append(data, size * count);
            if (*state.stopWhen && (*state.stopWhen)(*state.body)) {
                state.stoppedEarly = true;
                return 0;  // aborts the transfer with CURLE_WRITE_ERROR
            }
            return size * count;
        }
    }

    HttpClient::HttpClient(std::atomic<bool> const& stopping, TlsTrust trust) : _curl(NewEasyHandle(stopping, trust)) {
        curl_easy_setopt(_curl, CURLOPT_USERAGENT, BrowserUserAgent);
        curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(_curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(_curl, CURLOPT_TIMEOUT, 30L);
        // Refuses an oversized reply up front when the server states its size.
        curl_easy_setopt(_curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(MaxResponseBytes));
        // Offers only the encodings this libcurl can decode (none without zlib).
        curl_easy_setopt(_curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, &Write);
    }

    HttpClient::~HttpClient() { curl_easy_cleanup(_curl); }

    HttpClient::Response HttpClient::Get(std::string const& url, std::vector<std::string> const& headers, StopWhen stopWhen) {
        curl_easy_setopt(_curl, CURLOPT_HTTPGET, 1L);
        return Perform(url, headers, stopWhen);
    }

    HttpClient::Response HttpClient::PostJson(std::string const& url, std::string_view body, std::vector<std::string> const& headers) {
        curl_easy_setopt(_curl, CURLOPT_POST, 1L);
        curl_easy_setopt(_curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        curl_easy_setopt(_curl, CURLOPT_COPYPOSTFIELDS, std::string(body).c_str());
        auto withType = headers;
        withType.emplace_back("Content-Type: application/json");
        return Perform(url, withType, {});
    }

    HttpClient::Response HttpClient::Perform(std::string const& url, std::vector<std::string> const& headers, StopWhen const& stopWhen) {
        curl_slist* list = nullptr;
        for (auto const& header : headers)
            list = curl_slist_append(list, header.c_str());

        Response response;
        WriteState state{&response.body, &stopWhen};
        curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &state);

        auto rc = curl_easy_perform(_curl);
        curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, nullptr);
        curl_slist_free_all(list);
        if (state.tooLarge || rc == CURLE_FILESIZE_EXCEEDED)
            throw std::runtime_error("response too large");
        if (!(rc == CURLE_WRITE_ERROR && state.stoppedEarly))
            Check(rc, "request");

        curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &response.status);
        return response;
    }

    std::string UrlEncode(std::string_view value) {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    }
}
