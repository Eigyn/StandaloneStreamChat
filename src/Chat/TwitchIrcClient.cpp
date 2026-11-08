#include "Chat/TwitchIrcClient.hpp"
#include "Chat/Json.hpp"
#include "Chat/MessageMetadata.hpp"
#include "Net/HttpClient.hpp"
#include "Net/TlsConnection.hpp"
#include "logging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <random>
#include <vector>

namespace StandaloneStreamChat::Chat {
    namespace {
        constexpr char const* Host = "irc.chat.twitch.tv";
        constexpr int Port = 6697;

        // The "emotes" tag ("25:0-4,12-16/1902:6-10") as segments. Its ranges
        // count Unicode code points, so map them onto the UTF-8 bytes first.
        std::vector<ChatSegment> EmoteSegments(std::string const& text, std::string_view tag) {
            if (tag.empty() || text.empty())
                return {};

            std::vector<size_t> offsets;  // byte offset of each code point, plus the end
            for (size_t i = 0; i < text.size();) {
                offsets.push_back(i);
                auto c = static_cast<unsigned char>(text[i]);
                i += c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
            }
            offsets.push_back(text.size());

            struct Span {
                size_t start, end;
                std::string id;
            };
            std::vector<Span> spans;
            while (!tag.empty()) {
                auto part = tag.substr(0, tag.find('/'));
                tag.remove_prefix(std::min(tag.size(), part.size() + 1));
                auto colon = part.find(':');
                if (colon == 0 || colon == std::string_view::npos)
                    continue;
                std::string id(part.substr(0, colon));
                auto ranges = part.substr(colon + 1);
                while (!ranges.empty()) {
                    auto range = ranges.substr(0, ranges.find(','));
                    ranges.remove_prefix(std::min(ranges.size(), range.size() + 1));
                    auto dash = range.find('-');
                    if (dash == std::string_view::npos)
                        continue;
                    size_t first = std::strtoul(std::string(range.substr(0, dash)).c_str(), nullptr, 10);
                    size_t last = std::strtoul(std::string(range.substr(dash + 1)).c_str(), nullptr, 10);
                    if (last < first || last + 1 >= offsets.size())
                        continue;
                    spans.push_back({offsets[first], offsets[last + 1], id});
                }
            }
            if (spans.empty())
                return {};

            std::sort(spans.begin(), spans.end(), [](Span const& a, Span const& b) { return a.start < b.start; });
            std::vector<ChatSegment> segments;
            size_t pos = 0;
            for (auto const& span : spans) {
                if (span.start < pos)
                    continue;  // overlapping range: keep the first
                if (span.start > pos)
                    segments.push_back({text.substr(pos, span.start - pos)});
                // The static PNG exists even for animated emotes.
                segments.push_back({text.substr(span.start, span.end - span.start), "tw:" + StableKey(span.id),
                                    "https://static-cdn.jtvnw.net/emoticons/v2/" + span.id + "/static/dark/2.0",
                                    "https://static-cdn.jtvnw.net/emoticons/v2/" + span.id + "/default/dark/2.0"});
                pos = span.end;
            }
            if (pos < text.size())
                segments.push_back({text.substr(pos)});
            return segments;
        }

        std::string NormalizeChannel(std::string_view channel) {
            std::string out;
            for (char c : channel) {
                if (std::isspace(static_cast<unsigned char>(c)) || (out.empty() && c == '#'))
                    continue;
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return out;
        }
    }

    TwitchIrcClient::TwitchIrcClient(std::string_view channel, MessageHandler onMessage, StatusHandler onStatus)
        : ChatService(std::move(onMessage), std::move(onStatus)), _channel(NormalizeChannel(channel)) {}

    TwitchIrcClient::~TwitchIrcClient() { Stop(); }

    void TwitchIrcClient::Run() {
        int delaySeconds = 1;
        while (!_stopping) {
            try {
                ConnectAndRead();
                // Server closed the connection or asked us to reconnect.
                delaySeconds = 1;
            } catch (std::exception const& ex) {
                if (_stopping)
                    break;
                _onStatus(fmt::format("Twitch: connection lost ({}), retrying in {}s", ex.what(), delaySeconds));
            }

            SleepFor(std::chrono::seconds(delaySeconds));
            delaySeconds = std::min(delaySeconds * 2, 60);
        }
    }

    void TwitchIrcClient::ConnectAndRead() {
        _onStatus(fmt::format("Twitch: connecting to #{}...", _channel));

        Net::TlsConnection connection(_stopping);
        connection.Connect(Host, Port);

        std::random_device rd;
        auto nick = fmt::format("justinfan{}", std::uniform_int_distribution<int>(10000, 99999)(rd));
        connection.SendAll("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");
        connection.SendAll("NICK " + nick + "\r\n");

        constexpr size_t MaxLineBytes = 64 * 1024;
        std::string pending;
        while (!_stopping) {
            if (!connection.Receive(pending, 250))
                return;  // server closed; outer loop reconnects

            size_t lineStart = 0;
            for (auto nl = pending.find('\n'); nl != std::string::npos; nl = pending.find('\n', lineStart)) {
                if (nl - lineStart > MaxLineBytes)
                    throw std::runtime_error("Twitch IRC line too large");
                std::string_view line(pending.data() + lineStart, nl - lineStart);
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
                lineStart = nl + 1;

                auto message = IrcMessage::Parse(line);
                if (!message)
                    continue;

                auto const& command = message->command;
                if (command == "PING") {
                    connection.SendAll("PONG :" + std::string(message->TrailingOrEmpty()) + "\r\n");
                } else if (command == "001") {  // welcome: safe to join now
                    connection.SendAll("JOIN #" + _channel + "\r\n");
                } else if (command == "366") {  // end of names: join complete
                    _onStatus(fmt::format("Twitch: joined #{}", _channel));
                } else if (command == "PRIVMSG") {
                    HandlePrivmsg(*message);
                } else if (command == "ROOMSTATE" || command == "CLEARMSG" || command == "CLEARCHAT" || command == "USERNOTICE") {
                    HandleControl(*message);
                } else if (command == "NOTICE") {
                    _onStatus("Twitch: " + std::string(message->TrailingOrEmpty()));
                } else if (command == "RECONNECT") {
                    _onStatus("Twitch: server requested reconnect");
                    return;
                }
            }
            pending.erase(0, lineStart);
            if (pending.size() > MaxLineBytes)
                throw std::runtime_error("Twitch IRC line too large");
        }
    }

    // Twitch's public web GQL endpoint with the website's own client id (no
    // user login).
    std::optional<int> TwitchIrcClient::FetchViewers(Net::HttpClient& http) {
        constexpr char const* ClientIdHeader = "Client-Id: kimne78kx3ncx6brgo4mv6wki5h1ko";
        auto body = std::string(R"({"query":"query($login: String!) { user(login: $login) { stream { viewersCount } } }","variables":{"login":)") +
                    Json::Quote(_channel) + "}}";
        auto response = http.PostJson("https://gql.twitch.tv/gql", body, {ClientIdHeader});
        if (!response.Ok()) {
            DEBUG("Twitch: viewer count HTTP {}", response.status);
            return std::nullopt;
        }
        rapidjson::Document doc;
        doc.Parse(response.body.c_str(), response.body.size());
        // user is null for an unknown channel, stream is null when offline.
        auto count = doc.HasParseError() ? std::string() : Json::Scalar(Json::Path(&doc, {"data", "user", "stream", "viewersCount"}));
        return count.empty() ? -1 : std::atoi(count.c_str());
    }

    void TwitchIrcClient::HandlePrivmsg(IrcMessage const& message) {
        if (message.params.size() < 2)
            return;

        std::string text(message.TrailingOrEmpty());
        bool isAction = false;
        constexpr std::string_view actionPrefix = "\x01" "ACTION ";
        if (text.starts_with(actionPrefix)) {
            isAction = true;
            text.erase(0, actionPrefix.size());
            if (!text.empty() && text.back() == '\x01')
                text.pop_back();
        }

        std::string author(message.Tag("display-name"));
        if (author.empty())
            author = std::string(message.PrefixNick());

        std::string color(message.Tag("color"));
        if (color.empty())
            color = NameColorFor(author);

        auto badges = message.Tag("badges");

        ChatMessage chat;
        chat.platform = ChatPlatform::Twitch;
        chat.messageId = message.Tag("id");
        chat.userId = message.Tag("user-id");
        chat.channelId = message.Tag("room-id");
        if (chat.channelId.empty()) chat.channelId = _channelId;
        chat.channelName = _channel;
        chat.authorLogin = message.PrefixNick();
        chat.author = std::move(author);
        chat.color = std::move(color);
        chat.isBroadcaster = badges.find("broadcaster/") != std::string_view::npos;
        chat.isModerator = badges.find("moderator/") != std::string_view::npos;
        while (!badges.empty() && chat.badges.size() < 16) {
            auto item = badges.substr(0, badges.find(','));
            badges.remove_prefix(std::min(badges.size(), item.size() + 1));
            auto slash = item.find('/');
            if (slash != std::string_view::npos && slash > 0)
                chat.badges.push_back({std::string(item.substr(0, slash)), std::string(item.substr(slash + 1)), std::string(item.substr(0, slash))});
        }
        chat.bits = UnsignedNumber(message.Tag("bits"));
        chat.isHighlighted = message.Tag("msg-id") == "highlighted-message" || !message.Tag("custom-reward-id").empty();
        chat.isMention = ContainsMention(text, _channel);
        chat.segments = EmoteSegments(text, message.Tag("emotes"));
        chat.text = std::move(text);
        chat.isAction = isAction;

        auto count = ++_messageCount;
        if (count <= 5 || count % 100 == 0)
            INFO("Twitch: received message #{} (from {})", count, chat.author);

        _onMessage(std::move(chat));
    }

    void TwitchIrcClient::HandleControl(IrcMessage const& message) {
        ChatMessage event;
        event.platform = ChatPlatform::Twitch;
        event.channelName = _channel;
        event.channelId = message.Tag("room-id");
        if (event.channelId.empty()) event.channelId = _channelId;
        if (message.command == "ROOMSTATE") {
            if (event.channelId.empty()) return;
            _channelId = event.channelId;
            event.event = ChatEvent::ChannelIdentity;
        } else if (message.command == "CLEARMSG") {
            event.event = ChatEvent::RemoveMessage;
            event.messageId = message.Tag("target-msg-id");
        } else if (message.command == "CLEARCHAT") {
            event.userId = message.Tag("target-user-id");
            if (message.params.size() > 1) event.authorLogin = message.TrailingOrEmpty();
            event.event = event.userId.empty() && event.authorLogin.empty() ? ChatEvent::ClearChannel : ChatEvent::RemoveUser;
        } else {
            event.isNotice = true;
            event.isHighlighted = true;
            event.messageId = message.Tag("id");
            event.userId = message.Tag("user-id");
            event.author = message.Tag("display-name");
            if (event.author.empty()) event.author = message.Tag("login");
            if (event.author.empty()) event.author = "Twitch";
            event.color = message.Tag("color");
            event.text = message.Tag("system-msg");
            if (message.params.size() > 1) {
                if (!event.text.empty()) event.text += " — ";
                event.text += message.TrailingOrEmpty();
            }
            if (event.text.empty()) return;
        }
        _onMessage(std::move(event));
    }
}
