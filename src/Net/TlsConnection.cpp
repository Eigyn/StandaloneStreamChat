#include "Net/TlsConnection.hpp"
#include "Net/Curl.hpp"

#include <poll.h>

namespace SimpleBSStreamChat::Net {
    TlsConnection::TlsConnection(std::atomic<bool> const& stopping) : _stopping(stopping) {}

    TlsConnection::~TlsConnection() {
        if (_curl)
            curl_easy_cleanup(_curl);
    }

    void TlsConnection::Connect(std::string_view host, int port) {
        _curl = NewEasyHandle(_stopping);

        auto url = "https://" + std::string(host) + ":" + std::to_string(port);
        curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(_curl, CURLOPT_CONNECT_ONLY, 1L);

        Check(curl_easy_perform(_curl), "connect");

        curl_socket_t socket = CURL_SOCKET_BAD;
        Check(curl_easy_getinfo(_curl, CURLINFO_ACTIVESOCKET, &socket), "socket");
        _socket = socket;
    }

    void TlsConnection::SendAll(std::string_view data) {
        while (!data.empty() && !_stopping) {
            size_t sent = 0;
            auto rc = curl_easy_send(_curl, data.data(), data.size(), &sent);
            if (rc == CURLE_AGAIN) {
                pollfd pfd{_socket, POLLOUT, 0};
                poll(&pfd, 1, 250);
                continue;
            }
            Check(rc, "send");
            data.remove_prefix(sent);
        }
    }

    bool TlsConnection::Receive(std::string& out, int timeoutMs) {
        char buffer[4096];
        size_t received = 0;
        // Try first: TLS may already hold decrypted bytes the socket won't signal.
        auto rc = curl_easy_recv(_curl, buffer, sizeof(buffer), &received);
        if (rc == CURLE_AGAIN) {
            pollfd pfd{_socket, POLLIN, 0};
            if (poll(&pfd, 1, timeoutMs) <= 0)
                return true;
            rc = curl_easy_recv(_curl, buffer, sizeof(buffer), &received);
            if (rc == CURLE_AGAIN)
                return true;
        }
        Check(rc, "receive");
        if (received == 0)
            return false;
        out.append(buffer, received);
        return true;
    }
}
