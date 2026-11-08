#include "Chat/YouTubeChatClient.hpp"
#include "Chat/Json.hpp"
#include "Chat/MessageMetadata.hpp"
#include "Net/HttpClient.hpp"
#include "logging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace SimpleBSStreamChat::Chat {
    namespace {
        constexpr int MinPollDelayMs = 500;
        // YouTube's timeoutMs (10 s for live chat) is the browser's fallback
        // while it waits for push notifications, which this client doesn't get.
        // Polling that slowly delivered messages in late batches; at 1.5 s they
        // arrive about 2 s after being sent, and an empty poll is about 2 KB.
        constexpr int MaxPollDelayMs = 1500;

        std::vector<std::string> const& PageHeaders() {
            // Pre-accepted consent cookies so EU viewers don't get the consent interstitial.
            static std::vector<std::string> const headers{"Accept-Language: en-US,en;q=0.9", "Cookie: CONSENT=YES+cb; SOCS=CAI"};
            return headers;
        }

        bool IsIdChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; }

        bool LooksLikeVideoId(std::string_view value) {
            return value.size() == 11 && std::all_of(value.begin(), value.end(), IsIdChar);
        }

        std::string Trim(std::string_view value) {
            auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            return std::string(value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1));
        }

        std::string BuildLiveUrl(std::string channel) {
            if (channel.starts_with("http://") || channel.starts_with("https://"))
                return channel;
            if (channel.size() == 24 && channel.starts_with("UC") && std::all_of(channel.begin(), channel.end(), IsIdChar))
                return "https://www.youtube.com/channel/" + channel + "/live";
            if (!channel.starts_with('@'))
                channel.insert(0, "@");
            return "https://www.youtube.com/" + channel + "/live";
        }

        std::string ExtractBetween(std::string_view text, std::string_view prefix, std::string_view suffix) {
            auto start = text.find(prefix);
            if (start == std::string_view::npos)
                return {};
            start += prefix.size();
            auto end = text.find(suffix, start);
            return end == std::string_view::npos ? std::string() : std::string(text.substr(start, end - start));
        }

        std::string_view ExtractYtInitialData(std::string_view html) {
            for (std::string_view marker : {"window[\"ytInitialData\"] = ", "var ytInitialData = "}) {
                auto start = html.find(marker);
                if (start == std::string_view::npos)
                    continue;
                start += marker.size();
                auto end = html.find(";</script>", start);
                if (end != std::string_view::npos)
                    return html.substr(start, end - start);
            }
            return {};
        }

        constexpr std::string_view ContinuationKinds[] = {"invalidationContinuationData", "timedContinuationData", "reloadContinuationData"};

        std::string FindInitialContinuation(Json::Value const* root) {
            auto renderer = Json::Path(root, {"contents", "liveChatRenderer"});
            if (!renderer)
                return {};

            // Prefer the unfiltered "Live chat" view over the default "Top chat".
            // Localized titles still come in Top chat, Live chat order, so the
            // last submenu continuation is the fallback.
            auto items = Json::Path(renderer, {"header", "liveChatHeaderRenderer", "viewSelector", "sortFilterSubMenuRenderer", "subMenuItems"});
            if (Json::IsArray(items)) {
                std::string last;
                for (auto const& entry : items->GetArray()) {
                    auto continuation = Json::Scalar(Json::Path(&entry, {"continuation", "reloadContinuationData", "continuation"}));
                    if (continuation.empty())
                        continue;
                    auto title = Json::Scalar(Json::Get(&entry, "title"));
                    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (title.find("live") != std::string::npos)
                        return continuation;
                    last = std::move(continuation);
                }
                if (!last.empty())
                    return last;
            }

            auto continuations = Json::Get(renderer, "continuations");
            if (Json::IsArray(continuations) && !continuations->Empty()) {
                auto head = &(*continuations)[0];
                for (auto kind : ContinuationKinds) {
                    auto continuation = Json::Scalar(Json::Path(head, {kind, "continuation"}));
                    if (!continuation.empty())
                        return continuation;
                }
            }
            return {};
        }

        // Text and emoji runs as segments. Custom channel emotes become emote
        // segments (their image, falling back to the :shortcut:); standard emoji
        // stay text, which the Unicode core mod's font already draws.
        std::vector<ChatSegment> RunsToSegments(Json::Value const* runs) {
            std::vector<ChatSegment> segments;
            if (!Json::IsArray(runs))
                return segments;
            for (auto const& run : runs->GetArray()) {
                if (auto part = Json::Get(&run, "text"); part && part->IsString()) {
                    segments.push_back({std::string(part->GetString(), part->GetStringLength())});
                    continue;
                }
                auto emoji = Json::Get(&run, "emoji");
                if (!emoji)
                    continue;
                auto shortcuts = Json::Get(emoji, "shortcuts");
                auto shortcut = Json::IsArray(shortcuts) && !shortcuts->Empty() ? Json::Scalar(&(*shortcuts)[0]) : std::string();
                auto custom = Json::Get(emoji, "isCustomEmoji");
                if (custom && custom->IsBool() && custom->GetBool()) {
                    // Thumbnails are listed smallest first.
                    auto thumbnails = Json::Path(emoji, {"image", "thumbnails"});
                    auto url = Json::IsArray(thumbnails) && !thumbnails->Empty()
                                   ? Json::Scalar(Json::Get(&(*thumbnails)[thumbnails->Size() - 1], "url"))
                                   : std::string();
                    auto name = shortcut.empty() ? std::string(":emote:") : shortcut;
                    auto id = Json::Scalar(Json::Get(emoji, "emojiId"));
                    if (url.empty())
                        segments.push_back({std::move(name)});
                    else
                        segments.push_back({std::move(name), "yt:" + StableKey(id.empty() ? url : id), std::move(url)});
                } else {
                    auto character = Json::Scalar(Json::Get(emoji, "emojiId"));
                    segments.push_back({!character.empty() ? character : shortcut.empty() ? ":emoji:" : shortcut});
                }
            }
            return segments;
        }

        std::string TextOf(std::vector<ChatSegment> const& segments) {
            std::string text;
            for (auto const& segment : segments)
                text += segment.text;
            return text;
        }

        std::optional<ChatMessage> ParseItem(Json::Value const* item, RecentIds& seen) {
            auto renderer = Json::Get(item, "liveChatTextMessageRenderer");
            std::string paidAmount;
            bool isPaid = false;
            if (!renderer) {
                renderer = Json::Get(item, "liveChatPaidMessageRenderer");
                if (!renderer)
                    return std::nullopt;  // memberships, mode changes, deletions: ignored for now
                isPaid = true;
                paidAmount = Json::Scalar(Json::Path(renderer, {"purchaseAmountText", "simpleText"}));
            }

            ChatMessage chat;
            chat.platform = ChatPlatform::YouTube;
            chat.userId = Json::Scalar(Json::Get(renderer, "authorExternalChannelId"));
            chat.isHighlighted = isPaid;
            chat.author = Json::Scalar(Json::Path(renderer, {"authorName", "simpleText"}));
            if (chat.author.empty())
                chat.author = "unknown";
            chat.segments = RunsToSegments(Json::Path(renderer, {"message", "runs"}));
            if (isPaid)
                chat.segments.insert(chat.segments.begin(), ChatSegment{Trim("* " + paidAmount) + " "});
            chat.text = Trim(TextOf(chat.segments));
            bool anyEmote = std::any_of(chat.segments.begin(), chat.segments.end(), [](ChatSegment const& s) { return s.IsEmote(); });
            if (!anyEmote)
                chat.segments.clear();  // plain text needs no segments

            auto id = Json::Scalar(Json::Get(renderer, "id"));
            if (id.empty()) {
                auto timestamp = Json::Scalar(Json::Get(renderer, "timestampUsec"));
                if (!timestamp.empty())
                    id = "fallback:" + timestamp + ":" + chat.author + ":" + chat.text;
            }
            if (!id.empty() && !seen.Insert(id))
                return std::nullopt;
            chat.messageId = id;

            if (auto badges = Json::Get(renderer, "authorBadges"); Json::IsArray(badges)) {
                for (auto const& badge : badges->GetArray()) {
                    auto icon = Json::Scalar(Json::Path(&badge, {"liveChatAuthorBadgeRenderer", "icon", "iconType"}));
                    chat.isBroadcaster |= icon == "OWNER";
                    chat.isModerator |= icon == "MODERATOR";
                    auto badgeRenderer = Json::Get(&badge, "liveChatAuthorBadgeRenderer");
                    auto label = Json::Scalar(Json::Get(badgeRenderer, "tooltip"));
                    auto thumbnails = Json::Path(badgeRenderer, {"customThumbnail", "thumbnails"});
                    auto url = Json::IsArray(thumbnails) && !thumbnails->Empty() ? Json::Scalar(Json::Get(&(*thumbnails)[0], "url")) : std::string();
                    if (chat.badges.size() < 16)
                        chat.badges.push_back({icon.empty() ? label : icon, "", label.empty() ? icon : label, std::move(url)});
                }
            }
            chat.color = chat.isBroadcaster ? "#FFD600" : chat.isModerator ? "#5E84F1" : NameColorFor(chat.author);
            return chat;
        }
    }

    YouTubeChatClient::YouTubeChatClient(std::string_view channel, std::shared_ptr<RecentIds> seen, MessageHandler onMessage, StatusHandler onStatus)
        : ChatService(std::move(onMessage), std::move(onStatus)), _channel(Trim(channel)), _seen(std::move(seen)) {}

    YouTubeChatClient::~YouTubeChatClient() { Stop(); }

    void YouTubeChatClient::Run() {
        int errorDelaySeconds = 2;
        bool reportedOffline = false;
        while (!_stopping) {
            SetViewerSession({});  // each pass starts a new session
            try {
                Net::HttpClient http(_stopping);
                auto videoId = LooksLikeVideoId(_channel) ? _channel : ResolveLiveVideoId(http);
                if (videoId.empty()) {
                    _viewers = -1;
                    // Checked every 2 minutes; only say so once per offline stretch.
                    if (!reportedOffline)
                        _onStatus(fmt::format("YouTube: {} is not live - checking every 2 minutes", _channel));
                    reportedOffline = true;
                    SleepFor(std::chrono::minutes(2));
                    continue;
                }
                reportedOffline = false;

                if (!Bootstrap(http, videoId)) {
                    _onStatus("YouTube: live chat unavailable - retrying in 60s");
                    SleepFor(std::chrono::seconds(60));
                    continue;
                }
                _onStatus(fmt::format("YouTube: connected to live chat ({})", videoId));
                SetViewerSession({videoId, _apiKey, _clientVersion});
                errorDelaySeconds = 2;

                while (!_stopping) {
                    auto result = Poll(http);
                    for (auto& message : result.messages) {
                        message.channelId = videoId;
                        message.channelName = _channel;
                        message.isMention = message.event == ChatEvent::Message && ContainsMention(message.text, _channel);
                        auto count = ++_messageCount;
                        if (count <= 5 || count % 100 == 0)
                            INFO("YouTube: received message #{} (from {})", count, message.author);
                        _onMessage(std::move(message));
                    }
                    if (!result.valid) {
                        _viewers = -1;
                        _onStatus("YouTube: chat ended or reset - reconnecting");
                        SleepFor(std::chrono::seconds(errorDelaySeconds));
                        errorDelaySeconds = std::min(errorDelaySeconds * 2, 120);
                        break;
                    }
                    SleepFor(std::chrono::milliseconds(std::clamp(result.timeoutMs, MinPollDelayMs, MaxPollDelayMs)));
                }
            } catch (std::exception const& ex) {
                if (_stopping)
                    break;
                _onStatus(fmt::format("YouTube: error ({}), retrying in {}s", ex.what(), errorDelaySeconds));
                SleepFor(std::chrono::seconds(errorDelaySeconds));
                errorDelaySeconds = std::min(errorDelaySeconds * 2, 120);
            }
        }
    }

    std::string YouTubeChatClient::ResolveLiveVideoId(Net::HttpClient& http) {
        // The canonical link says which video /live points at. It sits around
        // 800 KB into a page with no gzip available, so stop reading once it's in.
        constexpr std::string_view canonical = "<link rel=\"canonical\" href=\"";
        size_t scanFrom = 0;
        auto response = http.Get(BuildLiveUrl(_channel), PageHeaders(), [&scanFrom, canonical](std::string_view body) {
            auto at = body.find(canonical, scanFrom);
            if (at == std::string_view::npos) {
                scanFrom = body.size() > canonical.size() ? body.size() - canonical.size() : 0;
                return false;
            }
            scanFrom = at;
            return body.find('"', at + canonical.size()) != std::string_view::npos;
        });
        if (!response.Ok())
            throw std::runtime_error(fmt::format("live page HTTP {}", response.status));

        constexpr std::string_view watch = "https://www.youtube.com/watch?v=";
        auto href = ExtractBetween(response.body, canonical, "\"");
        if (!href.starts_with(watch))
            return {};  // canonical is the channel page: not live
        auto id = href.substr(watch.size(), 11);
        return LooksLikeVideoId(id) ? id : std::string();
    }

    bool YouTubeChatClient::Bootstrap(Net::HttpClient& http, std::string const& videoId) {
        _apiKey.clear();
        _clientVersion.clear();
        _continuation.clear();
        // _seen is deliberately kept: a fresh continuation's first poll replays
        // recent messages, and the shared ids are what filter them out.

        auto response = http.Get("https://www.youtube.com/live_chat?is_popout=1&v=" + videoId, PageHeaders());
        if (!response.Ok())
            throw std::runtime_error(fmt::format("chat page HTTP {}", response.status));
        auto const& html = response.body;

        _apiKey = ExtractBetween(html, "\"INNERTUBE_API_KEY\":\"", "\"");
        _clientVersion = ExtractBetween(html, "\"INNERTUBE_CONTEXT_CLIENT_VERSION\":\"", "\"");
        if (_clientVersion.empty())
            _clientVersion = ExtractBetween(html, "\"clientVersion\":\"", "\"");
        auto initialData = ExtractYtInitialData(html);
        if (_apiKey.empty() || _clientVersion.empty() || initialData.empty()) {
            DEBUG("YouTube: bootstrap failed: apiKey={} clientVersion={} initialData={}", !_apiKey.empty(), !_clientVersion.empty(),
                  !initialData.empty());
            return false;
        }

        rapidjson::Document doc;
        doc.Parse(initialData.data(), initialData.size());
        if (doc.HasParseError())
            return false;
        _continuation = FindInitialContinuation(&doc);
        return !_continuation.empty();
    }

    void YouTubeChatClient::SetViewerSession(ViewerSession session) {
        std::lock_guard lock(_viewerSessionLock);
        _viewerSession = std::move(session);
    }

    // The watch page's live counter ("9,545 watching now"), from InnerTube's
    // updated_metadata with the chat session's key. No counter means it ended.
    std::optional<int> YouTubeChatClient::FetchViewers(Net::HttpClient& http) {
        ViewerSession session;
        {
            std::lock_guard lock(_viewerSessionLock);
            session = _viewerSession;
        }
        if (session.videoId.empty())
            return -1;  // not live, or between sessions
        auto url = "https://www.youtube.com/youtubei/v1/updated_metadata?key=" + Net::UrlEncode(session.apiKey) + "&prettyPrint=false";
        auto body = fmt::format(R"({{"context":{{"client":{{"clientName":"WEB","clientVersion":{},"hl":"en","gl":"US"}}}},"videoId":{}}})",
                                Json::Quote(session.clientVersion), Json::Quote(session.videoId));
        auto response = http.PostJson(url, body, PageHeaders());
        if (!response.Ok()) {
            DEBUG("YouTube: viewer count HTTP {}", response.status);
            return std::nullopt;
        }
        rapidjson::Document doc;
        doc.Parse(response.body.c_str(), response.body.size());
        auto actions = doc.HasParseError() ? nullptr : Json::Get(&doc, "actions");
        if (Json::IsArray(actions)) {
            for (auto const& action : actions->GetArray()) {
                auto viewCount = Json::Path(&action, {"updateViewershipAction", "viewCount", "videoViewCountRenderer", "viewCount"});
                auto text = Json::Scalar(Json::Get(viewCount, "simpleText"));
                if (auto runs = Json::Get(viewCount, "runs"); text.empty() && Json::IsArray(runs)) {
                    for (auto const& run : runs->GetArray())
                        text += Json::Scalar(Json::Get(&run, "text"));
                }
                std::string digits;
                for (char c : text) {
                    if (c >= '0' && c <= '9')
                        digits.push_back(c);
                }
                if (!digits.empty() && digits.size() < 10)
                    return std::stoi(digits);
            }
        }
        return -1;
    }

    YouTubeChatClient::PollResult YouTubeChatClient::Poll(Net::HttpClient& http) {
        PollResult result;
        auto url = "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?key=" + Net::UrlEncode(_apiKey) + "&prettyPrint=false";
        auto body = fmt::format(R"({{"context":{{"client":{{"clientName":"WEB","clientVersion":{},"hl":"en","gl":"US"}}}},"continuation":{}}})",
                                Json::Quote(_clientVersion), Json::Quote(_continuation));
        auto response = http.PostJson(url, body, PageHeaders());
        if (!response.Ok()) {
            DEBUG("YouTube: poll HTTP {}", response.status);
            return result;
        }

        rapidjson::Document doc;
        doc.Parse(response.body.c_str(), response.body.size());
        auto liveChat = doc.HasParseError() ? nullptr : Json::Path(&doc, {"continuationContents", "liveChatContinuation"});
        if (!liveChat || !liveChat->IsObject())
            return result;

        auto continuations = Json::Get(liveChat, "continuations");
        auto head = Json::IsArray(continuations) && !continuations->Empty() ? &(*continuations)[0] : nullptr;
        for (auto kind : ContinuationKinds) {
            auto data = Json::Get(head, kind);
            if (!data)
                continue;
            auto next = Json::Scalar(Json::Get(data, "continuation"));
            if (!next.empty()) {
                _continuation = std::move(next);
                result.valid = true;  // no next continuation means the chat ended or changed shape
            }
            if (auto timeout = Json::Get(data, "timeoutMs"); timeout && timeout->IsInt())
                result.timeoutMs = timeout->GetInt();
            break;
        }

        if (auto actions = Json::Get(liveChat, "actions"); Json::IsArray(actions)) {
            for (auto const& action : actions->GetArray()) {
                auto removal = Json::Get(&action, "removeChatItemAction");
                if (!removal) removal = Json::Get(&action, "markChatItemAsDeletedAction");
                auto userRemoval = Json::Get(&action, "markChatItemsByAuthorAsDeletedAction");
                if (!userRemoval) userRemoval = Json::Get(&action, "removeChatItemByAuthorAction");
                if (removal || userRemoval) {
                    ChatMessage event;
                    event.platform = ChatPlatform::YouTube;
                    event.event = userRemoval ? ChatEvent::RemoveUser : ChatEvent::RemoveMessage;
                    event.messageId = Json::Scalar(Json::Get(removal, "targetItemId"));
                    event.userId = Json::Scalar(Json::Get(userRemoval, "externalChannelId"));
                    result.messages.push_back(std::move(event));
                    continue;
                }
                auto item = Json::Path(&action, {"addChatItemAction", "item"});
                if (auto message = ParseItem(item, *_seen))
                    result.messages.push_back(std::move(*message));
            }
        }
        return result;
    }
}
