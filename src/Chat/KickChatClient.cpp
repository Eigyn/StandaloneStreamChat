#include "Chat/KickChatClient.hpp"
#include "Chat/MessageMetadata.hpp"
#include "Net/Curl.hpp"
#include "Net/HttpClient.hpp"
#include "Net/WebSocket.hpp"
#include "logging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <random>
#include <stdexcept>

namespace StandaloneStreamChat::Chat {
    namespace {
        constexpr char const* RealtimeApi = "https://web.kick.com/api/v1/realtime";
        // Fallback when the realtime lookup fails; it returned this server for every channel tried.
        constexpr char const* DefaultRealtimeUrl = "wss://realtime.us-west-2.platform.kick.com/connection/websocket";
        constexpr std::string_view ChatMessageEvent = "App\\Events\\ChatMessageEvent";

        std::vector<std::string> ApiHeaders(std::string_view slug) {
            return {"Accept: application/json", "Accept-Language: en-US,en;q=0.9", "Origin: https://kick.com",
                    "Referer: https://kick.com/" + std::string(slug)};
        }

        std::string Lowercase(std::string value) {
            for (auto& c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }

        // Accepts a slug, "@slug", a kick.com URL, or "chatroom:123".
        std::string NormalizeChannel(std::string_view channel) {
            std::string value;
            for (char c : channel) {
                if (!std::isspace(static_cast<unsigned char>(c)))
                    value.push_back(c);
            }
            if (auto scheme = value.find("://"); scheme != std::string::npos) {
                auto pathStart = value.find('/', scheme + 3);
                value = pathStart == std::string::npos ? "" : value.substr(pathStart + 1);
                value = value.substr(0, value.find_first_of("/?#"));
            }
            value.erase(0, value.find_first_not_of("@#"));
            value = Lowercase(value);
            if (value.starts_with("chatroom:"))
                value.erase(0, 9);
            return value;
        }

        bool IsNumber(std::string_view value) {
            return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); });
        }

        std::string NewClientId() {
            // Random UUID v4, like the web client's crypto.randomUUID().
            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> nibble(0, 15);
            std::string id = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
            for (auto& c : id) {
                if (c == 'x')
                    c = "0123456789abcdef"[nibble(rng)];
                else if (c == 'y')
                    c = "89ab"[nibble(rng) & 3];
            }
            return id;
        }

        void AppendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x110000) {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        // Kick HTML-escapes message content.
        std::string HtmlDecode(std::string_view text) {
            std::string out;
            for (size_t i = 0; i < text.size(); i++) {
                auto end = text[i] == '&' ? text.find(';', i) : std::string_view::npos;
                if (end == std::string_view::npos || end - i > 10) {
                    out.push_back(text[i]);
                    continue;
                }
                auto entity = text.substr(i + 1, end - i - 1);
                auto before = out.size();
                if (entity == "amp")
                    out += '&';
                else if (entity == "lt")
                    out += '<';
                else if (entity == "gt")
                    out += '>';
                else if (entity == "quot")
                    out += '"';
                else if (entity == "apos")
                    out += '\'';
                else if (entity.size() > 1 && entity[0] == '#') {
                    bool hex = entity[1] == 'x' || entity[1] == 'X';
                    auto digits = std::string(entity.substr(hex ? 2 : 1));
                    char* parsedEnd = nullptr;
                    auto cp = std::strtoul(digits.c_str(), &parsedEnd, hex ? 16 : 10);
                    if (!digits.empty() && parsedEnd && *parsedEnd == '\0')
                        AppendUtf8(out, static_cast<uint32_t>(cp));
                }
                if (out.size() == before)
                    out.push_back('&');  // unknown entity: keep it literally
                else
                    i = end;
            }
            return out;
        }

        // Message content as segments: "[emote:123:name]" tokens become emotes
        // (served as GIFs), line breaks become spaces, and the ends are
        // trimmed. Plain text comes back as a single segment.
        std::vector<ChatSegment> ContentSegments(std::string_view content) {
            auto text = HtmlDecode(content);
            std::vector<ChatSegment> segments;
            auto appendText = [&segments](char c) {
                if (segments.empty() || segments.back().IsEmote())
                    segments.emplace_back();
                segments.back().text.push_back(c == '\r' || c == '\n' ? ' ' : c);
            };
            constexpr std::string_view emote = "[emote:";
            for (size_t i = 0; i < text.size();) {
                if (text.compare(i, emote.size(), emote) == 0) {
                    auto close = text.find(']', i);
                    auto nameStart = text.find(':', i + emote.size());
                    if (close != std::string::npos && nameStart != std::string::npos && nameStart < close) {
                        auto id = text.substr(i + emote.size(), nameStart - i - emote.size());
                        segments.push_back({text.substr(nameStart + 1, close - nameStart - 1), "kick:" + StableKey(id),
                                            "https://files.kick.com/emotes/" + id + "/fullsize", "https://files.kick.com/emotes/" + id + "/fullsize"});
                        i = close + 1;
                        continue;
                    }
                }
                appendText(text[i]);
                i++;
            }

            if (!segments.empty() && !segments.front().IsEmote()) {
                auto& first = segments.front().text;
                first.erase(0, first.find_first_not_of(' '));
            }
            if (!segments.empty() && !segments.back().IsEmote()) {
                auto& last = segments.back().text;
                last.erase(last.find_last_not_of(' ') + 1);
            }
            std::erase_if(segments, [](ChatSegment const& s) { return !s.IsEmote() && s.text.empty(); });
            return segments;
        }

        rapidjson::Document ParseJson(std::string const& text) {
            rapidjson::Document doc;
            doc.Parse(text.c_str(), text.size());
            if (doc.HasParseError())
                doc.SetNull();
            return doc;
        }
    }

    KickChatClient::KickChatClient(std::string_view channel, MessageHandler onMessage, StatusHandler onStatus)
        : ChatService(std::move(onMessage), std::move(onStatus)), _channel(NormalizeChannel(channel)) {}

    KickChatClient::~KickChatClient() { Stop(); }

    void KickChatClient::Run() {
        std::optional<ChannelInfo> info;
        int delaySeconds = 2;
        while (!_stopping) {
            try {
                Net::HttpClient http(_stopping);
                if (!info) {
                    _onStatus(fmt::format("Kick: looking up {}...", _channel));
                    info = Resolve(http);
                    if (!info) {
                        _onStatus(fmt::format("Kick: channel {} not found - retrying in 60s", _channel));
                        SleepFor(std::chrono::seconds(60));
                        continue;
                    }
                }
                ReadChat(http, *info);
                if (_stopping)
                    break;
                delaySeconds = 2;
                _onStatus(fmt::format("Kick: disconnected - reconnecting in {}s", delaySeconds));
            } catch (std::exception const& ex) {
                if (_stopping)
                    break;
                _onStatus(fmt::format("Kick: connection lost ({}), retrying in {}s", ex.what(), delaySeconds));
            }

            SleepFor(std::chrono::seconds(delaySeconds));
            delaySeconds = std::min(delaySeconds * 2, 120);
        }
    }

    std::optional<KickChatClient::ChannelInfo> KickChatClient::Resolve(Net::HttpClient& http) {
        if (_channel.empty())
            return std::nullopt;
        if (IsNumber(_channel))
            return ChannelInfo{_channel, _channel, _channel};

        auto response = http.Get("https://kick.com/api/v2/channels/" + Net::UrlEncode(_channel), ApiHeaders(_channel));
        if (response.status == 404)
            return std::nullopt;
        if (!response.Ok())
            throw std::runtime_error(fmt::format("channel lookup HTTP {}", response.status));

        // A Cloudflare challenge comes back as HTML, which fails to parse here.
        auto doc = ParseJson(response.body);
        ChannelInfo info;
        info.chatroomId = Json::Scalar(Json::Path(&doc, {"chatroom", "id"}));
        if (info.chatroomId.empty())
            throw std::runtime_error("channel lookup returned no chatroom");
        info.channelId = Json::Scalar(Json::Get(&doc, "id"));
        if (info.channelId.empty())
            info.channelId = info.chatroomId;
        info.displayName = Json::Scalar(Json::Path(&doc, {"user", "username"}));
        if (info.displayName.empty())
            info.displayName = _channel;
        return info;
    }

    std::string KickChatClient::RealtimeUrl(Net::HttpClient& http, ChannelInfo const& info, std::string const& clientId) {
        auto body = fmt::format(R"({{"client":{{"id":{},"type":"web"}},"capabilities":{{"accepted_providers":[{{"provider":"centrifugo"}}]}}}})",
                                Json::Quote(clientId));
        try {
            auto response = http.PostJson(fmt::format("{}/channels/{}/chat/connection", RealtimeApi, info.channelId), body, ApiHeaders(_channel));
            auto doc = ParseJson(response.body);
            auto connections = Json::Path(&doc, {"data", "connections"});
            if (response.Ok() && Json::IsArray(connections)) {
                for (auto const& connection : connections->GetArray()) {
                    auto url = Json::Scalar(Json::Path(&connection, {"credentials", "url"}));
                    if (Json::Scalar(Json::Get(&connection, "provider")) == "centrifugo" && url.starts_with("wss://"))
                        return url;
                }
            }
            WARNING("Kick: realtime lookup gave HTTP {} without a server, using the default", response.status);
        } catch (std::exception const& ex) {
            if (_stopping)
                throw;
            WARNING("Kick: realtime lookup failed ({}), using the default server", ex.what());
        }
        return DefaultRealtimeUrl;
    }

    std::string KickChatClient::GuestToken(Net::HttpClient& http, std::string const& clientId) {
        auto response = http.PostJson(std::string(RealtimeApi) + "/auth/connection", fmt::format(R"({{"client_id":{}}})", Json::Quote(clientId)),
                                      ApiHeaders(_channel));
        if (!response.Ok())
            throw std::runtime_error(fmt::format("guest token HTTP {}", response.status));
        auto doc = ParseJson(response.body);
        auto token = Json::Scalar(Json::Path(&doc, {"data", "token"}));
        if (token.empty())
            throw std::runtime_error("no guest token");
        return token;
    }

    void KickChatClient::ReadChat(Net::HttpClient& http, ChannelInfo const& info) {
        _chatroomId = info.chatroomId;
        auto clientId = NewClientId();
        auto url = RealtimeUrl(http, info, clientId);
        auto token = GuestToken(http, clientId);

        _onStatus(fmt::format("Kick: connecting to {}...", info.displayName));
        Net::WebSocket socket(_stopping);
        socket.Connect(url, {"Origin: https://kick.com", std::string("User-Agent: ") + Net::BrowserUserAgent});

        int nextId = 1;
        socket.SendText(fmt::format(R"({{"id":{},"connect":{{"token":{},"name":"js"}}}})", nextId++, Json::Quote(token)));
        int subscribeId = nextId++;
        socket.SendText(fmt::format(R"({{"id":{},"subscribe":{{"channel":{}}}}})", subscribeId, Json::Quote("chatrooms." + info.chatroomId + ".v2")));

        // Guest tokens expire (the server says when); swap in a fresh one first.
        using Clock = std::chrono::steady_clock;
        std::optional<Clock::time_point> refreshAt;
        auto scheduleRefresh = [&refreshAt](Json::Value const* reply) {
            auto ttl = Json::Get(reply, "ttl");
            auto expires = Json::Get(reply, "expires");
            if (expires && expires->IsBool() && expires->GetBool() && ttl && ttl->IsInt() && ttl->GetInt() > 0) {
                auto lifetime = std::chrono::milliseconds(static_cast<int64_t>(ttl->GetInt()) * 1000);
                refreshAt = Clock::now() + std::max(lifetime - std::chrono::seconds(120),
                                                     lifetime / 2);
            }
        };

        std::string frame;
        while (!_stopping) {
            if (!socket.Receive(frame, 250))
                return;
            if (refreshAt && Clock::now() >= *refreshAt) {
                refreshAt.reset();
                socket.SendText(fmt::format(R"({{"id":{},"refresh":{{"token":{}}}}})", nextId++, Json::Quote(GuestToken(http, clientId))));
            }

            // A frame can carry several newline-separated replies.
            std::string_view rest(frame);
            while (!rest.empty()) {
                auto newline = rest.find('\n');
                auto line = rest.substr(0, newline);
                rest = newline == std::string_view::npos ? std::string_view() : rest.substr(newline + 1);
                if (line.empty())
                    continue;
                if (line == "{}") {  // server ping
                    socket.SendText("{}");
                    continue;
                }

                auto doc = ParseJson(std::string(line));
                if (auto error = Json::Get(&doc, "error"))
                    throw std::runtime_error("server error: " + Json::Scalar(Json::Get(error, "message")));

                if (auto connect = Json::Get(&doc, "connect")) {
                    scheduleRefresh(connect);
                } else if (auto refresh = Json::Get(&doc, "refresh")) {
                    scheduleRefresh(refresh);
                } else if (Json::Get(&doc, "subscribe") && Json::Scalar(Json::Get(&doc, "id")) == std::to_string(subscribeId)) {
                    _onStatus(fmt::format("Kick: connected to {} chat", info.displayName));
                } else if (auto push = Json::Get(&doc, "push")) {
                    if (Json::Get(push, "disconnect"))
                        return;
                    auto data = Json::Path(push, {"pub", "data"});
                    auto event = Json::Scalar(Json::Get(data, "event"));
                    if (event == ChatMessageEvent)
                        HandleChatMessage(Json::Get(data, "data"));
                    else
                        HandleControl(event, Json::Get(data, "data"));
                }
            }
        }
    }

    // The same channel lookup as Resolve; `livestream` is null while offline.
    std::optional<int> KickChatClient::FetchViewers(Net::HttpClient& http) {
        if (IsNumber(_channel))
            return -1;  // a bare chatroom id has no channel page to ask
        auto response = http.Get("https://kick.com/api/v2/channels/" + Net::UrlEncode(_channel), ApiHeaders(_channel));
        if (!response.Ok()) {
            DEBUG("Kick: viewer count HTTP {}", response.status);
            return std::nullopt;
        }
        auto doc = ParseJson(response.body);
        auto count = Json::Scalar(Json::Path(&doc, {"livestream", "viewer_count"}));
        return count.empty() ? -1 : std::atoi(count.c_str());
    }

    void KickChatClient::HandleChatMessage(Json::Value const* payload) {
        // The event payload is itself JSON, encoded as a string.
        rapidjson::Document parsed;
        Json::Value const* data = payload;
        if (payload && payload->IsString()) {
            parsed.Parse(payload->GetString(), payload->GetStringLength());
            if (parsed.HasParseError())
                return;
            data = &parsed;
        }
        if (!data || !data->IsObject())
            return;

        // Each message can arrive more than once across reconnects.
        auto id = Json::Scalar(Json::Get(data, "id"));
        if (!id.empty() && !_seen.Insert(id))
            return;

        auto segments = ContentSegments(Json::Scalar(Json::Get(data, "content")));
        if (segments.empty())
            return;
        std::string text;
        for (auto const& segment : segments)
            text += segment.text;
        if (segments.size() == 1 && !segments.front().IsEmote())
            segments.clear();  // plain text needs no segments

        auto sender = Json::Get(data, "sender");
        auto identity = Json::Get(sender, "identity");

        ChatMessage chat;
        chat.platform = ChatPlatform::Kick;
        chat.messageId = id;
        chat.userId = Json::Scalar(Json::Get(sender, "id"));
        chat.channelName = _channel;
        chat.channelId = _chatroomId;
        chat.author = Json::FirstScalar(sender, {"username", "slug"});
        if (chat.author.empty())
            chat.author = "Kick";
        chat.color = Json::Scalar(Json::Get(identity, "color"));  // the panel validates it
        chat.text = std::move(text);
        chat.segments = std::move(segments);
        chat.isMention = ContainsMention(chat.text, _channel);
        if (auto badges = Json::Get(identity, "badges"); Json::IsArray(badges)) {
            for (auto const& badge : badges->GetArray()) {
                auto type = Json::Scalar(Json::Get(&badge, "type"));
                chat.isBroadcaster |= type == "broadcaster";
                chat.isModerator |= type == "moderator";
                if (chat.badges.size() < 16)
                    chat.badges.push_back({type, Json::Scalar(Json::Get(&badge, "count")), type, Json::Scalar(Json::Get(&badge, "image_url"))});
            }
        }
        if (auto badges = Json::Get(identity, "badges_v2"); Json::IsArray(badges)) {
            chat.badges.clear();
            for (auto const& badge : badges->GetArray()) {
                auto type = Json::Scalar(Json::Get(&badge, "badge_type"));
                chat.isBroadcaster |= type == "broadcaster";
                chat.isModerator |= type == "moderator";
                auto selected = Json::Get(&badge, "selected");
                if (selected && selected->IsBool() && !selected->GetBool()) continue;
                if (chat.badges.size() >= 16) continue;
                chat.badges.push_back({type, Json::Scalar(Json::Path(&badge, {"metadata", "level"})),
                                       Json::Scalar(Json::Get(&badge, "name")), Json::Scalar(Json::Get(&badge, "image_url"))});
            }
        }

        auto count = ++_messageCount;
        if (count <= 5 || count % 100 == 0)
            INFO("Kick: received message #{} (from {})", count, chat.author);

        _onMessage(std::move(chat));
    }

    void KickChatClient::HandleControl(std::string_view name, Json::Value const* payload) {
        ChatMessage event;
        event.platform = ChatPlatform::Kick;
        event.channelName = _channel; event.channelId = _chatroomId;
        if (name == "App\\Events\\MessageDeletedEvent") event.event = ChatEvent::RemoveMessage;
        else if (name == "App\\Events\\UserBannedEvent") event.event = ChatEvent::RemoveUser;
        else if (name == "App\\Events\\ChatroomClearEvent") event.event = ChatEvent::ClearChannel;
        else return;
        rapidjson::Document parsed;
        if (payload && payload->IsString()) {
            parsed.Parse(payload->GetString(), payload->GetStringLength());
            if (parsed.HasParseError()) return;
            payload = &parsed;
        }
        // Event IDs identify the moderation action, not the deleted message.
        event.messageId = Json::Scalar(Json::Path(payload, {"message", "id"}));
        event.userId = Json::Scalar(Json::Path(payload, {"user", "id"}));
        _onMessage(std::move(event));
    }
}
