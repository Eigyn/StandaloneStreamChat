#include "Net/WebSocket.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <stdexcept>

// OpenSSL's SHA1 is exported by the bundled libcurl archive on both targets.
extern "C" unsigned char* SHA1(unsigned char const* data, size_t size, unsigned char* digest);

namespace StandaloneStreamChat::Net {
    namespace {
        enum Opcode : uint8_t { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

        // Chat frames are a few KB; refuse anything absurd instead of buffering it.
        constexpr uint64_t MaxPayload = 16 * 1024 * 1024;
        constexpr size_t MaxUpgradeResponse = 64 * 1024;

        std::string Base64(std::string_view bytes) {
            static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            for (size_t i = 0; i < bytes.size(); i += 3) {
                uint32_t n = static_cast<uint8_t>(bytes[i]) << 16;
                if (i + 1 < bytes.size())
                    n |= static_cast<uint8_t>(bytes[i + 1]) << 8;
                if (i + 2 < bytes.size())
                    n |= static_cast<uint8_t>(bytes[i + 2]);
                out.push_back(table[(n >> 18) & 63]);
                out.push_back(table[(n >> 12) & 63]);
                out.push_back(i + 1 < bytes.size() ? table[(n >> 6) & 63] : '=');
                out.push_back(i + 2 < bytes.size() ? table[n & 63] : '=');
            }
            return out;
        }

        std::string_view Trim(std::string_view value) {
            auto first = value.find_first_not_of(" \t");
            if (first == value.npos)
                return {};
            return value.substr(first, value.find_last_not_of(" \t") - first + 1);
        }

        bool EqualAscii(std::string_view a, std::string_view b) {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
                return std::tolower(x) == std::tolower(y);
            });
        }

        bool HasToken(std::string_view value, std::string_view token) {
            while (!value.empty()) {
                auto comma = value.find(',');
                if (EqualAscii(Trim(value.substr(0, comma)), token))
                    return true;
                if (comma == value.npos)
                    break;
                value.remove_prefix(comma + 1);
            }
            return false;
        }

        bool ValidUpgrade(std::string_view headers, std::string const& key) {
            auto end = headers.find("\r\n");
            auto status = headers.substr(0, end);
            if (end == headers.npos || (status != "HTTP/1.1 101" && !status.starts_with("HTTP/1.1 101 ")))
                return false;
            headers.remove_prefix(end + 2);

            auto challenge = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            std::array<unsigned char, 20> digest{};
            if (!SHA1(reinterpret_cast<unsigned char const*>(challenge.data()), challenge.size(), digest.data()))
                return false;
            auto expected = Base64({reinterpret_cast<char const*>(digest.data()), digest.size()});
            bool upgrade = false, connection = false, accepted = false;
            while (!headers.empty()) {
                end = headers.find("\r\n");
                auto line = headers.substr(0, end);
                auto colon = line.find(':');
                if (colon == line.npos)
                    return false;
                auto name = line.substr(0, colon);
                auto value = Trim(line.substr(colon + 1));
                if (EqualAscii(name, "Upgrade"))
                    upgrade |= EqualAscii(value, "websocket");
                else if (EqualAscii(name, "Connection"))
                    connection |= HasToken(value, "Upgrade");
                else if (EqualAscii(name, "Sec-WebSocket-Accept")) {
                    if (accepted || value != expected)
                        return false;
                    accepted = true;
                } else if (EqualAscii(name, "Sec-WebSocket-Extensions") || EqualAscii(name, "Sec-WebSocket-Protocol")) {
                    return false;  // the request offered neither
                }
                if (end == headers.npos)
                    break;
                headers.remove_prefix(end + 2);
            }
            return upgrade && connection && accepted;
        }
    }

    WebSocket::WebSocket(std::atomic<bool> const& stopping) : _tls(stopping), _stopping(stopping), _rng(std::random_device{}()) {}

    void WebSocket::Connect(std::string_view url, std::vector<std::string> const& headers) {
        constexpr std::string_view scheme = "wss://";
        if (!url.starts_with(scheme))
            throw std::runtime_error("only wss:// URLs are supported");
        url.remove_prefix(scheme.size());

        auto pathStart = url.find_first_of("/?");
        auto authority = url.substr(0, pathStart);
        std::string path = pathStart == std::string_view::npos ? "/" : std::string(url.substr(pathStart));
        if (path.front() == '?')
            path.insert(0, "/");
        auto host = authority;
        int port = 443;
        if (auto colon = authority.find(':'); colon != std::string_view::npos) {
            host = authority.substr(0, colon);
            port = std::stoi(std::string(authority.substr(colon + 1)));
        }

        _tls.Connect(host, port);

        std::string key(16, '\0');
        for (auto& c : key)
            c = static_cast<char>(_rng());
        key = Base64(key);
        std::string request = "GET " + path + " HTTP/1.1\r\n" +
                              "Host: " + std::string(authority) + "\r\n" +
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Key: " + key + "\r\n" +
                              "Sec-WebSocket-Version: 13\r\n";
        for (auto const& header : headers)
            request += header + "\r\n";
        request += "\r\n";
        _tls.SendAll(request);

        // Read the response headers; whatever follows them is already frame data.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        size_t headerEnd;
        while ((headerEnd = _buffer.find("\r\n\r\n")) == std::string::npos) {
            if (_stopping || std::chrono::steady_clock::now() > deadline || _buffer.size() > MaxUpgradeResponse)
                throw std::runtime_error("no upgrade response");
            if (!_tls.Receive(_buffer, 250))
                throw std::runtime_error("closed during upgrade");
        }
        if (headerEnd > MaxUpgradeResponse || !ValidUpgrade(std::string_view(_buffer).substr(0, headerEnd), key))
            throw std::runtime_error("invalid WebSocket upgrade response");
        _buffer.erase(0, headerEnd + 4);
    }

    void WebSocket::SendText(std::string_view text) { SendFrame(Text, text); }

    bool WebSocket::Receive(std::string& message, int timeoutMs) {
        message.clear();
        if (TakeMessage(message))
            return true;
        if (_closed)
            return false;
        if (!_tls.Receive(_buffer, timeoutMs))
            return false;
        return TakeMessage(message) || !_closed;
    }

    bool WebSocket::TakeFrame(Frame& frame) {
        if (_buffer.size() < 2)
            return false;
        auto byte = [this](size_t i) { return static_cast<uint8_t>(_buffer[i]); };

        size_t offset = 2;
        uint64_t length = byte(1) & 0x7F;
        if (length == 126) {
            if (_buffer.size() < 4)
                return false;
            length = (byte(2) << 8) | byte(3);
            offset = 4;
        } else if (length == 127) {
            if (_buffer.size() < 10)
                return false;
            length = 0;
            for (size_t i = 2; i < 10; i++)
                length = (length << 8) | byte(i);
            offset = 10;
        }
        if (length > MaxPayload)
            throw std::runtime_error("frame too large");

        // Servers must not mask, but unmasking costs nothing.
        bool masked = byte(1) & 0x80;
        size_t maskOffset = offset;
        if (masked)
            offset += 4;
        if (_buffer.size() < offset + length)
            return false;

        frame.fin = byte(0) & 0x80;
        frame.opcode = byte(0) & 0x0F;
        frame.payload.assign(_buffer, offset, length);
        if (masked) {
            for (size_t i = 0; i < frame.payload.size(); i++)
                frame.payload[i] ^= _buffer[maskOffset + i % 4];
        }
        _buffer.erase(0, offset + length);
        return true;
    }

    bool WebSocket::TakeMessage(std::string& message) {
        Frame frame;
        while (!_closed && TakeFrame(frame)) {
            if (frame.opcode == Text || frame.opcode == Binary) {
                _fragments = std::move(frame.payload);
            } else if (frame.opcode == Continuation) {
                // The frame limit applies to the whole message, not each piece.
                if (_fragments.size() + frame.payload.size() > MaxPayload)
                    throw std::runtime_error("message too large");
                _fragments += frame.payload;
            } else if (frame.opcode == Ping) {
                SendFrame(Pong, frame.payload);
                continue;
            } else if (frame.opcode == Pong) {
                continue;
            } else if (frame.opcode == Close) {
                _closed = true;
                try {
                    SendFrame(Close, std::string_view(frame.payload).substr(0, 2));  // echo the status code
                } catch (std::exception const&) {
                    // The server may already be gone.
                }
                return false;
            } else {
                throw std::runtime_error("unknown WebSocket opcode " + std::to_string(frame.opcode));
            }

            if (frame.fin) {
                message = std::move(_fragments);
                _fragments.clear();
                return true;
            }
        }
        return false;
    }

    void WebSocket::SendFrame(uint8_t opcode, std::string_view payload) {
        std::string frame;
        frame.push_back(static_cast<char>(0x80 | opcode));
        uint64_t length = payload.size();
        if (length < 126) {
            frame.push_back(static_cast<char>(0x80 | length));
        } else if (length <= 0xFFFF) {
            frame.push_back(static_cast<char>(0x80 | 126));
            frame.push_back(static_cast<char>(length >> 8));
            frame.push_back(static_cast<char>(length & 0xFF));
        } else {
            frame.push_back(static_cast<char>(0x80 | 127));
            for (int shift = 56; shift >= 0; shift -= 8)
                frame.push_back(static_cast<char>((length >> shift) & 0xFF));
        }

        // Client frames must be masked (RFC 6455 5.3).
        char mask[4];
        for (auto& m : mask)
            m = static_cast<char>(_rng());
        frame.append(mask, sizeof(mask));
        for (size_t i = 0; i < payload.size(); i++)
            frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
        _tls.SendAll(frame);
    }
}
