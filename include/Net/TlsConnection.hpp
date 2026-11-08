#pragma once

#include <atomic>
#include <string>
#include <string_view>

typedef void CURL;

namespace StandaloneStreamChat::Net {
    /// A raw TLS byte stream (libcurl CONNECT_ONLY), used for Twitch IRC and
    /// under WebSocket.
    /// Not thread-safe: one thread connects, sends and receives. Every call
    /// that can block returns early once `stopping` is set.
    /// Errors are thrown as std::runtime_error.
    class TlsConnection {
       public:
        explicit TlsConnection(std::atomic<bool> const& stopping);
        ~TlsConnection();
        TlsConnection(TlsConnection const&) = delete;
        TlsConnection& operator=(TlsConnection const&) = delete;

        /// Connects and completes the TLS handshake (certificate verified
        /// against the headset's system CA store).
        void Connect(std::string_view host, int port);
        void SendAll(std::string_view data);
        /// Appends whatever arrives within `timeoutMs` to `out`.
        /// Returns false when the server closed the connection.
        bool Receive(std::string& out, int timeoutMs);

       private:
        CURL* _curl = nullptr;
        int _socket = -1;
        std::atomic<bool> const& _stopping;
    };
}
