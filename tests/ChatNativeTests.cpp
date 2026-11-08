// Runs the production clients/loaders/decoder with an in-memory transport.
// No messages or HTTP requests leave this executable.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "Chat/Json.hpp"
#include "Config.hpp"
#define private public
#include "Chat/TwitchIrcClient.hpp"
#include "Chat/YouTubeChatClient.hpp"
#include "Chat/KickChatClient.hpp"
#include "Chat/ThirdPartyEmotes.hpp"
#undef private
#include "Chat/MessageHistory.hpp"
#include "Chat/MessageMetadata.hpp"
#include "UI/EmoteReferences.hpp"
#include "UI/GifDecoder.hpp"
#include "UI/RichText.hpp"
#include "Net/HttpClient.hpp"
#include "Net/TlsConnection.hpp"
#include "Net/WebSocket.hpp"
#include "logging.hpp"
#include "Downloader-under-test.hpp"

namespace Fixture {
using namespace StandaloneStreamChat;
std::string wire;
bool received = false;
std::string imageBytes = "image bytes";
std::string youTubeBody;  // replaces the default YouTube poll reply when set
std::atomic<int> ffzStatus{200}, httpCalls{0}, channelCalls{0};
void Check(bool condition, char const* reason) {
    if (!condition) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
void Wait(std::function<bool()> ready) {
    for (int i = 0; i < 3000 && !ready(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(ready(), "worker did not complete");
}
}
namespace StandaloneStreamChat {
Config& GetConfig() { static Config config; config.showViewerCounts = false; return config; }
}
namespace StandaloneStreamChat::Net {
TlsConnection::TlsConnection(std::atomic<bool> const& stopping) : _stopping(stopping) {}
TlsConnection::~TlsConnection() = default;
void TlsConnection::Connect(std::string_view, int) {}
void TlsConnection::SendAll(std::string_view) {}
bool TlsConnection::Receive(std::string& out, int) {
    if (Fixture::received) return false;
    Fixture::received = true; out += Fixture::wire; return true;
}
WebSocket::WebSocket(std::atomic<bool> const& stopping) : _tls(stopping), _stopping(stopping) {}
void WebSocket::Connect(std::string_view, std::vector<std::string> const&) {}
void WebSocket::SendText(std::string_view) {}
bool WebSocket::Receive(std::string&, int) { return false; }
HttpClient::HttpClient(std::atomic<bool> const&) {}
HttpClient::~HttpClient() = default;
HttpClient::Response HttpClient::Get(std::string const& url, std::vector<std::string> const&, StopWhen) {
    ++Fixture::httpCalls;
    if (url.starts_with("fixture:")) return {200, Fixture::imageBytes};
    if (url.find("/room/") != std::string::npos)
        return {Fixture::ffzStatus, R"({"room":{"twitch_id":1234},"sets":{"1":{"emoticons":[{"id":4,"name":"ChannelFfz","urls":{"2":"https://fixture/ffz.png"}}]}}})"};
    if (url.find("/cached/users/twitch/") != std::string::npos) {
        ++Fixture::channelCalls;
        return {200, R"({"channelEmotes":[{"id":"bttv","code":"ChannelBttv","imageType":"gif"}],"sharedEmotes":[]})"};
    }
    if (url.find("/users/twitch/") != std::string::npos) {
        ++Fixture::channelCalls;
        return {200, R"({"emote_set":{"emotes":[{"id":"seven","name":"ChannelSeven","data":{"host":{"url":"//fixture/seven","files":[{"name":"2x.avif"},{"name":"2x.gif"},{"name":"2x.webp"}]}}},)"
                     R"({"id":"still","name":"StillSeven","data":{"host":{"url":"//fixture/still","files":[{"name":"2x.png"},{"name":"2x.webp"}]}}},)"
                     R"({"id":"both","name":"BothSeven","data":{"host":{"url":"//fixture/both","files":[{"name":"2x.png"},{"name":"2x.gif"}]}}}]}})"};
    }
    if (url.find("betterttv") != std::string::npos) return {200, "[]"};
    if (url.find("frankerfacez") != std::string::npos) return {200, R"({"sets":{}})"};
    return {200, R"({"emotes":[]})"};
}
HttpClient::Response HttpClient::PostJson(std::string const& url, std::string_view, std::vector<std::string> const&) {
    ++Fixture::httpCalls;
    if (url.find("youtube") != std::string::npos && !Fixture::youTubeBody.empty())
        return {200, Fixture::youTubeBody};
    if (url.find("youtube") != std::string::npos)
        return {200, R"({"continuationContents":{"liveChatContinuation":{"continuations":[{"invalidationContinuationData":{"continuation":"next","timeoutMs":1000}}],"actions":[{"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"yt1","authorExternalChannelId":"u1","authorName":{"simpleText":"Alice"},"message":{"runs":[{"text":"hello"}]},"authorBadges":[{"liveChatAuthorBadgeRenderer":{"tooltip":"Member","customThumbnail":{"thumbnails":[{"url":"https://fixture/member.png"}]}}}]}}}},{"removeChatItemAction":{"targetItemId":"yt1"}},{"markChatItemsByAuthorAsDeletedAction":{"externalChannelId":"u2"}}]}}})"};
    return {200, R"({"data":{"badges":[{"setID":"vip","version":"1","imageURL":"https://fixture/vip.png"}],"user":{"id":"1234","broadcastBadges":[{"setID":"subscriber","version":"12","imageURL":"https://fixture/sub.png"}]}}})"};
}
std::string UrlEncode(std::string_view value) { return std::string(value); }
}

namespace {
using namespace StandaloneStreamChat;
using namespace Chat;
using Fixture::Check;
void Moderation() {
    for (auto command : {"@target-msg-id=one :tmi.twitch.tv CLEARMSG #fixture :hello\r\n",
                         "@target-user-id=42 :tmi.twitch.tv CLEARCHAT #fixture :alice\r\n",
                         ":tmi.twitch.tv CLEARCHAT #fixture\r\n"}) {
        std::deque<ChatMessage> history;
        ChatMessage other; other.platform = ChatPlatform::YouTube; other.channelName = "fixture"; other.messageId = "one";
        history.push_back(other);
        other.platform = ChatPlatform::Twitch; other.channelName = "other"; history.push_back(other);
        std::string identity;
        TwitchIrcClient client("fixture", [&](ChatMessage message) {
            if (message.event == ChatEvent::ChannelIdentity) identity = message.channelId;
            else if (message.event == ChatEvent::Message) history.push_back(std::move(message));
            else ApplyModeration(history, message);
        }, [](std::string) {});
        Fixture::wire = "@room-id=1234 :tmi.twitch.tv ROOMSTATE #fixture\r\n@id=one;user-id=42;display-name=Alice;room-id=1234 :alice!a@a PRIVMSG #fixture :hello\r\n" + std::string(command);
        Fixture::received = false; client.ConnectAndRead();
        bool single = std::string_view(command).find("CLEARMSG") != std::string_view::npos;
        Check(identity == "1234" && history.size() == (single ? 3 : 2), "moderation targets and channel/platform scope");
        if (single) Check(history.back().isDeleted && history.back().revision == 1 && history.back().text == "[message deleted]", "single deletion retains marker and invalidates its row");
    }
    std::vector<ChatMessage> messages;
    TwitchIrcClient client("fixture", [&](ChatMessage m) { messages.push_back(std::move(m)); }, [](std::string) {});
    Fixture::wire = "@id=rich;user-id=42;badges=subscriber/12,vip/1;bits=100;msg-id=highlighted-message;display-name=Alice;emotes=25:2-6 :alice!a@a PRIVMSG #fixture :\x01" "ACTION 😀 Kappa @FiXtUrE\x01\r\n";
    Fixture::received = false; client.ConnectAndRead();
    Check(messages.size() == 1, "rich message parsed");
    auto const& m = messages.front();
    Check(m.badges.size() == 2 && m.bits == 100 && m.isHighlighted && m.isMention && m.isAction, "badge/bits/highlight/mention metadata");
    Check(m.segments.size() == 3 && m.segments[1].text == "Kappa" && !m.segments[1].animatedUrl.empty(), "Unicode/action emote indexing and animation URL");
    std::deque<ChatMessage> redacted{m};
    ChatMessage deletion; deletion.platform = ChatPlatform::Twitch; deletion.channelName = "fixture";
    deletion.event = ChatEvent::RemoveMessage; deletion.messageId = "rich";
    Check(ApplyModeration(redacted, deletion) && redacted.front().segments.empty() && redacted.front().bits == 0 &&
          !redacted.front().isMention && !redacted.front().isHighlighted, "deleted markers release old emotes and special-message metadata");
    Check(!ContainsMention("email@fixture.com @fixtureOther", "fixture") && ContainsMention("hey @FIXTURE!", "fixture"), "mention word boundaries");
    std::cout << "PASS Twitch moderation scope, identity, Unicode action emotes, badges, bits and mentions\n";
}
void OtherPlatforms() {
    std::atomic<bool> stop{false}; Net::HttpClient http(stop);
    YouTubeChatClient youtube("@fixture", std::make_shared<RecentIds>(20), [](ChatMessage) {}, [](std::string) {});
    auto result = youtube.Poll(http);
    Check(result.valid && result.messages.size() == 3, "YouTube ordered message/delete/user-purge actions");
    Check(result.messages[0].messageId == "yt1" && result.messages[0].userId == "u1" && result.messages[0].badges[0].imageUrl == "https://fixture/member.png", "YouTube identity and member badge");
    Check(result.messages[1].event == ChatEvent::RemoveMessage && result.messages[1].messageId == "yt1" && result.messages[2].userId == "u2", "YouTube deletion targets");
    // A new session's first poll replays chat sent before the client started; only newer lines show.
    Fixture::youTubeBody = R"({"continuationContents":{"liveChatContinuation":{"continuations":[{"invalidationContinuationData":{"continuation":"next","timeoutMs":1000}}],"actions":[)"
                           R"({"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"old","timestampUsec":"1000","authorName":{"simpleText":"Bob"},"message":{"runs":[{"text":"before"}]}}}}},)"
                           R"({"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"new","timestampUsec":"9000","authorName":{"simpleText":"Bob"},"message":{"runs":[{"text":"after"}]}}}}}]}}})";
    YouTubeChatClient late("@fixture", std::make_shared<RecentIds>(20), [](ChatMessage) {}, [](std::string) {});
    late._startedUsec = 5000;
    auto backlog = late.Poll(http);
    Check(backlog.valid && backlog.messages.size() == 1 && backlog.messages[0].messageId == "new", "YouTube skips chat sent before it started");
    Check(late.Poll(http).messages.empty(), "skipped YouTube backlog stays seen");
    Fixture::youTubeBody.clear();
    std::deque<ChatMessage> history;
    KickChatClient kick("fixture", [&](ChatMessage m) { if (m.event == ChatEvent::Message) history.push_back(std::move(m)); else ApplyModeration(history, m); }, [](std::string) {});
    kick._chatroomId = "1234";
    rapidjson::Document message;
    message.Parse(R"({"id":"k1","content":"hello @fixture [emote:1:Hi]","sender":{"id":42,"username":"Alice","identity":{"badges_v2":[{"badge_type":"subscriber","name":"Subscriber","image_url":"https://fixture/kick.png","selected":true},{"badge_type":"moderator","selected":false}]}}})");
    kick.HandleChatMessage(&message);
    Check(history.size() == 1 && history[0].userId == "42" && history[0].isMention && history[0].badges.size() == 1 && !history[0].segments.back().animatedUrl.empty(), "Kick badges and GIF metadata");
    Check(history.front().isModerator, "Kick v2 role is available even when its badge is hidden");
    rapidjson::Document deletion; deletion.Parse(R"({"id":"action-id","message":{"id":"k1"}})");
    kick.HandleControl("App\\Events\\MessageDeletedEvent", &deletion);
    Check(history.size() == 1 && history.front().isDeleted, "Kick deletion uses message.id, not action id");
    rapidjson::Document founder;
    founder.Parse(R"({"id":"k2","content":"hi","sender":{"id":43,"username":"Bob","identity":{"badges_v2":[{"badge_type":"kick_founder22","name":"Founder","image_url":"https://ext.cdn.kick.com/chat/badges/founder.png","selected":true}]}}})");
    kick.HandleChatMessage(&founder);
    Check(history.size() == 2 && history[1].badges.size() == 1 &&
              history[1].badges[0].imageUrl == "https://ext.cdn.kick.com/chat/badges/founder.png?width=128",
          "Kick badge images are requested at badge size");
    std::cout << "PASS YouTube ordered removal actions and Kick badges/deletion\n";
}
void Resources() {
    ThirdPartyEmotes loader;
    for (int status : {200, 404, 503}) {
        ThirdPartyEmotes::Cache cache; cache.login = "fixture";
        Fixture::ffzStatus = status; Fixture::channelCalls = 0;
        loader.Load(cache, true, true);
        auto emotes = ThirdPartyEmotes::Merge(cache);
        Check(Fixture::channelCalls == 2 && emotes->contains("ChannelBttv") && emotes->contains("ChannelSeven"), "FFZ failure must not suppress BTTV/7TV");
        auto const& gifOnly = emotes->at("ChannelSeven");
        auto const& stillOnly = emotes->at("StillSeven");
        auto const& both = emotes->at("BothSeven");
        Check(gifOnly.url == "https://fixture/seven/2x.gif" && gifOnly.animatedUrl == gifOnly.url, "7TV emote without a PNG uses its GIF's first frame as the still");
        Check(stillOnly.url == "https://fixture/still/2x.png" && stillOnly.animatedUrl.empty(), "static 7TV emote uses its PNG");
        Check(both.url == "https://fixture/both/2x.png" && both.animatedUrl == "https://fixture/both/2x.gif", "7TV emote with both keeps the PNG still");
        Check(cache.badges->contains("subscriber/12") && cache.badges->contains("vip/1"), "global and channel badge resources");
    }
    Fixture::ffzStatus = 503;
    auto idle = [&] { std::lock_guard lock(loader._lock); return !loader._loading; };
    loader.EnsureLoaded("fixture", true);
    Fixture::Wait(idle);
    auto calls = Fixture::httpCalls.load();
    for (int i = 0; i < 100; i++) loader.EnsureLoaded("fixture", true);
    Check(Fixture::httpCalls == calls, "backoff prevents a retry per frame");
    Fixture::ffzStatus = 200;
    { std::lock_guard lock(loader._lock); loader._nextAttempt = {}; }
    loader.EnsureLoaded("fixture", true);
    Fixture::Wait(idle);
    Check(loader.Snapshot()->contains("ChannelFfz") && Fixture::httpCalls == calls + 1, "retry recovers only failed provider");
    Fixture::ffzStatus = 503;
    { std::lock_guard lock(loader._lock); loader._nextAttempt = {}; }
    loader.EnsureLoaded("fixture", true);
    Fixture::Wait(idle);
    Check(loader.Snapshot()->contains("ChannelFfz"), "refresh failure preserves previously loaded emotes");
    std::cout << "PASS independent provider lookup, retry/backoff, retained resources and channel badges\n";
}
struct Gif {
    std::vector<uint8_t> data;
    size_t widthOffset = 0;
    void U16(int n) { data.push_back(n & 255); data.push_back((n >> 8) & 255); }
    Gif(int w, int h) {
        for (char c : std::string("GIF89a")) data.push_back(c);
        U16(w); U16(h);
        for (int n : {0x81,3,0, 255,0,0, 0,255,0, 0,0,255, 0,0,0}) data.push_back(n);
    }
    void Frame(int left, int top, int w, int h, std::vector<uint8_t> const& pixels, int disposal = 1, bool interlaced = false) {
        for (int n : {0x21,0xF9,4,(disposal << 2) | 1,10,0,3,0,0x2C}) data.push_back(n);
        U16(left); U16(top); widthOffset = data.size(); U16(w); U16(h); data.push_back(interlaced ? 0x40 : 0); data.push_back(2);
        std::vector<uint8_t> packed; unsigned bits = 0, used = 0;
        auto code = [&](unsigned value) { bits |= value << used; used += 3; while (used >= 8) { packed.push_back(bits & 255); bits >>= 8; used -= 8; } };
        for (auto value : pixels) { code(4); code(value); } code(5); if (used) packed.push_back(bits & 255);
        for (size_t start = 0; start < packed.size();) {
            auto count = std::min<size_t>(255, packed.size() - start); data.push_back(count);
            data.insert(data.end(), packed.begin() + start, packed.begin() + start + count); start += count;
        }
        data.push_back(0);
    }
    std::vector<uint8_t> End() { auto bytes = data; bytes.push_back(0x3B); return bytes; }
};
void GifAndReferences() {
    UI::EmoteReferences refs;
    refs.Begin(1); Check(refs.Use("shared"), "reference insert"); refs.End();
    refs.Begin(2); refs.Use("shared"); refs.Use("other"); refs.End();
    std::vector<uint64_t> rows{1}; refs.Retain(rows);
    Check(refs.Contains("shared") && !refs.Contains("other"), "recycling preserves shared image references");
    refs.Begin(3); for (int i = 0; i < 32; i++) Check(refs.Use(std::to_string(i)), "row reference budget");
    Check(!refs.Use("excess"), "over-budget row uses text fallback"); refs.End(); refs.Retain({});
    Check(!refs.Contains("shared"), "hidden rows release pins");
    Gif gif(3, 1); gif.Frame(0,0,3,1,{0,0,0}); gif.Frame(1,0,1,1,{1},2); gif.Frame(2,0,1,1,{2},3); gif.Frame(0,0,1,1,{1});
    UI::GifAnimation animation;
    Check(UI::DecodeGifAnimation(gif.End(), animation) && animation.frames.size() == 4, "GIF multi-frame decode");
    Check(animation.frames[1].rgba[5] == 255 && animation.frames[2].rgba[7] == 0 && animation.frames[2].rgba[10] == 255, "GIF disposal to background/transparency");
    Check(animation.frames[3].rgba[1] == 255 && animation.frames[3].rgba[7] == 0 && animation.frames[3].rgba[8] == 255, "GIF restore-to-previous disposal");
    Check(animation.frames[0].durationMs == 100, "GIF playback rate bound");
    Gif interlaced(1,5); interlaced.Frame(0,0,1,5,{0,1,2,1,0},1,true);
    Check(UI::DecodeGifAnimation(interlaced.End(), animation) && animation.frames[0].rgba[1] == 255 && animation.frames[0].rgba[16] == 255, "GIF interlace and bottom-up rows");
    Gif many(64,64); for (int i = 0; i < 128; i++) many.Frame(0,0,64,64,std::vector<uint8_t>(4096,i%3));
    Check(UI::DecodeGifAnimation(many.End(), animation) && animation.frames.size() == UI::MaxAnimationFrames && animation.Bytes() == UI::MaxAnimationBytes, "GIF frame memory boundary");
    many.Frame(0,0,64,64,std::vector<uint8_t>(4096,0));
    Check(UI::DecodeGifAnimation(many.End(), animation) && animation.frames.size() > 1 && animation.frames.size() <= UI::MaxAnimationFrames && animation.Bytes() <= UI::MaxAnimationBytes, "GIF sampling stays in frame/RAM budget");
    Gif longClip(1,1);
    for (int i = 0; i < 1025; i++) longClip.Frame(0,0,1,1,{static_cast<uint8_t>(i%3)});
    Check(UI::DecodeGifAnimation(longClip.End(), animation) && animation.frames.size() == 1, "GIF over-work-budget static fallback");
    Gif wide(513,1); wide.Frame(0,0,1,1,{0}); Check(!UI::DecodeGifAnimation(wide.End(), animation), "animated source dimension bound");
    Gif invalid(1,1); invalid.Frame(0,0,1,1,{0}); invalid.data[invalid.widthOffset] = 255; invalid.data[invalid.widthOffset + 1] = 255;
    int width, height; std::vector<uint8_t> rgba;
    Check(!UI::DecodeGifFirstFrame(invalid.End(), width, height, rgba), "malformed GIF frame cannot allocate oversized indices");
    std::cout << "PASS active-row pins, GIF compositing/interlace, frame/RAM/rate limits and malformed input\n";
}
void Backpressure() {
    Fixture::httpCalls = 0;
    {
    UI::EmoteDownloader downloader;
    int accepted = 0;
    for (int i = 0; i < 1000; i++) accepted += downloader.Request(std::to_string(i), "fixture:image", "fixture:image", false);
    Fixture::Wait([] { return Fixture::httpCalls > 0; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Check(accepted <= 65 && Fixture::httpCalls == 1, "bounded jobs and one-result download backpressure");
    UI::EmoteDownloader::Result result; Check(downloader.Take(result), "take completed image");
    Fixture::Wait([] { return Fixture::httpCalls == 2; });
    }
    Gif gif(3,1); gif.Frame(0,0,3,1,{0,0,0}); gif.Frame(0,0,3,1,{1,1,1});
    auto bytes = gif.End(); Fixture::imageBytes.assign(bytes.begin(), bytes.end());
    UI::EmoteDownloader animated;
    animated.SetAnimations(true);
    Check(animated.Request("gif", "fixture:gif", "fixture:gif", true), "animated request queued");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    animated.SetAnimations(false);
    UI::EmoteDownloader::Result still;
    bool taken = false;
    for (int i = 0; i < 3000 && !taken; i++) { taken = animated.Take(still); if (!taken) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    Check(taken && still.animation.frames.empty() && !still.rgba.empty(), "disabling animation frees completed/in-flight frame sets");
    std::cout << "PASS 1,000-image burst stays bounded while the consumer is paused\n";
}
void OversizedImages() {
    // A big animated Kick emote: its first frame, then more data past the download cap.
    Gif gif(2,1); gif.Frame(0,0,2,1,{0,1});
    auto bytes = gif.End(); bytes.resize(UI::MaxDownloadBytes + 4096, 0);
    Fixture::imageBytes.assign(bytes.begin(), bytes.end());
    auto take = [](UI::EmoteDownloader& downloader, UI::EmoteDownloader::Result& result) {
        bool taken = false;
        for (int i = 0; i < 3000 && !taken; i++) { taken = downloader.Take(result); if (!taken) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        return taken;
    };
    {
    UI::EmoteDownloader downloader;
    Check(downloader.Request("big", "fixture:big", "fixture:big", false), "oversized GIF queued");
    UI::EmoteDownloader::Result result;
    Check(take(downloader, result) && result.width == 2 && result.height == 1 && result.rgba.size() == 8 && result.bytes.empty(), "oversized GIF keeps its first frame");
    }
    // A cut-off PNG or JPEG can't be decoded, so nothing reaches Unity.
    Fixture::imageBytes.assign(UI::MaxDownloadBytes + 4096, 'x');
    UI::EmoteDownloader downloader;
    Check(downloader.Request("png", "fixture:png", "fixture:png", false), "oversized image queued");
    UI::EmoteDownloader::Result result;
    Check(take(downloader, result) && result.bytes.empty() && result.rgba.empty(), "oversized non-GIF image is dropped");
    std::cout << "PASS oversized GIFs keep their first frame; other oversized images are dropped\n";
}
}
int main(int argc, char** argv) {
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        StandaloneStreamChat::UI::GifAnimation animation;
        Fixture::Check(StandaloneStreamChat::UI::DecodeGifAnimation(bytes, animation), "real provider GIF decode");
        Fixture::Check(animation.Bytes() <= StandaloneStreamChat::UI::MaxAnimationBytes && animation.frames.size() <= StandaloneStreamChat::UI::MaxAnimationFrames, "real GIF memory/frame limit");
        std::cout << "PASS real GIF: " << animation.width << 'x' << animation.height << ", " << animation.frames.size() << " sampled frames, " << animation.Bytes() << " bytes\n";
        return 0;
    }
    Fixture::Check(StandaloneStreamChat::UI::Escape("</no</NoParse>parse><size=200%>fixture</size>") ==
                   "<noparse><size=200%>fixture</size></noparse>", "closing tags formed by erasing another tag stay literal");
    Moderation(); OtherPlatforms(); Resources(); GifAndReferences(); Backpressure(); OversizedImages();
    std::cout << "All chat native tests passed; external network traffic: none.\n";
}
