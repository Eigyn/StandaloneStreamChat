#pragma once

#include "Net/TlsConnection.hpp"

#include <atomic>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace SimpleBSStreamChat::Net {
    /// Minimal RFC 6455 client over TlsConnection: the bundled libcurl is built
    /// without WebSocket support. Text messages only; answers pings itself.
    /// Not thread-safe. Errors throw std::runtime_error.
    class WebSocket {
       public:
        explicit WebSocket(std::atomic<bool> const& stopping);

        /// `url` is wss://host[:port]/path?query; `headers` are extra
        /// "Name: value" lines for the upgrade request.
        void Connect(std::string_view url, std::vector<std::string> const& headers = {});
        void SendText(std::string_view text);
        /// Waits up to `timeoutMs` for one complete message. Returns false once
        /// the server closed the connection; `message` stays empty on timeout.
        bool Receive(std::string& message, int timeoutMs);

       private:
        struct Frame {
            bool fin = false;
            uint8_t opcode = 0;
            std::string payload;
        };

        bool TakeFrame(Frame& frame);
        bool TakeMessage(std::string& message);
        void SendFrame(uint8_t opcode, std::string_view payload);

        TlsConnection _tls;
        std::atomic<bool> const& _stopping;
        std::string _buffer;     // received bytes not yet parsed into frames
        std::string _fragments;  // payload of a message still missing its final frame
        bool _closed = false;
        std::mt19937 _rng;
    };
}
