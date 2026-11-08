#include "Chat/ThirdPartyEmotes.hpp"
#include "Chat/ChatMessage.hpp"
#include "Chat/Json.hpp"
#include "Net/HttpClient.hpp"
#include "logging.hpp"
#include <algorithm>
#include <cctype>

namespace StandaloneStreamChat::Chat {
    namespace {
        constexpr size_t MaxProviderEmotes = 1024, MaxMergedEmotes = 2048, MaxBadges = 2048, MaxMetadataBytes = 4 * 1024 * 1024;
        using EmoteMap = ThirdPartyEmotes::EmoteMap;
        enum class Fetch { Data, Missing, Retry };
        Fetch Read(Net::HttpClient& http, std::string const& url, rapidjson::Document& doc, std::string const& body = {}) {
            try {
                auto response = body.empty() ? http.Get(url, {"Accept: application/json"}, [](std::string_view text) { return text.size() > MaxMetadataBytes; })
                                             : http.PostJson(url, body, {"Client-Id: kimne78kx3ncx6brgo4mv6wki5h1ko"});
                if (response.status == 404) return Fetch::Missing;
                if (!response.Ok() || response.body.size() > MaxMetadataBytes) return Fetch::Retry;
                doc.Parse(response.body.c_str(), response.body.size());
                return doc.HasParseError() ? Fetch::Retry : Fetch::Data;
            } catch (std::exception const& ex) { DEBUG("Chat resources {}: {}", url, ex.what()); return Fetch::Retry; }
        }
        std::string Login(std::string_view value) {
            std::string result;
            for (unsigned char c : value) {
                if (std::isspace(c) || (result.empty() && (c == '#' || c == '@'))) continue;
                result.push_back(static_cast<char>(std::tolower(c)));
            }
            return result;
        }
        void Add(EmoteMap& map, std::string name, std::string key, std::string url, std::string animated = {}) {
            if (map.size() < MaxProviderEmotes && !name.empty() && !url.empty())
                map.try_emplace(std::move(name), ThirdPartyEmotes::Emote{std::move(key), std::move(url), std::move(animated)});
        }
        void SevenTv(EmoteMap& map, Json::Value const* emotes) {
            if (!Json::IsArray(emotes)) return;
            for (auto const& emote : emotes->GetArray()) {
                auto id = Json::Scalar(Json::Get(&emote, "id"));
                auto host = Json::Scalar(Json::Path(&emote, {"data", "host", "url"}));
                if (host.empty()) host = "https://cdn.7tv.app/emote/" + id;
                if (host.starts_with("//")) host = "https:" + host;
                std::string animated;
                bool listed = false, png = false;
                if (auto files = Json::Path(&emote, {"data", "host", "files"}); Json::IsArray(files)) {
                    listed = true;
                    for (auto const& file : files->GetArray()) {
                        auto name = Json::Scalar(Json::Get(&file, "name"));
                        if (name == "2x.gif") animated = host + "/2x.gif";
                        else if (name == "2x.png") png = true;
                    }
                }
                // Animated 7TV emotes come only as AVIF, WebP and GIF, with no
                // PNG. With animations off, their GIF's first frame is the still.
                auto still = !listed || png || animated.empty() ? host + "/2x.png" : animated;
                Add(map, Json::Scalar(Json::Get(&emote, "name")), "7tv:" + StableKey(id), std::move(still), std::move(animated));
            }
        }
        void Bttv(EmoteMap& map, Json::Value const* emotes) {
            if (!Json::IsArray(emotes)) return;
            for (auto const& emote : emotes->GetArray()) {
                auto id = Json::Scalar(Json::Get(&emote, "id"));
                if (id.empty()) continue;
                auto base = "https://cdn.betterttv.net/emote/" + id;
                Add(map, Json::Scalar(Json::Get(&emote, "code")), "bttv:" + StableKey(id), base + "/2x.png",
                    Json::Scalar(Json::Get(&emote, "imageType")) == "gif" ? base + "/2x.gif" : "");
            }
        }
        void Ffz(EmoteMap& map, Json::Value const* sets) {
            if (!sets || !sets->IsObject()) return;
            for (auto const& set : sets->GetObject()) {
                auto emotes = Json::Get(&set.value, "emoticons");
                if (!Json::IsArray(emotes)) continue;
                for (auto const& emote : emotes->GetArray()) {
                    auto url = Json::FirstScalar(Json::Get(&emote, "urls"), {"2", "1"});
                    auto animated = Json::FirstScalar(Json::Get(&emote, "animated"), {"2", "1"});
                    if (url.starts_with("//")) url = "https:" + url;
                    if (animated.starts_with("//")) animated = "https:" + animated;
                    Add(map, Json::Scalar(Json::Get(&emote, "name")), "ffz:" + StableKey(Json::Scalar(Json::Get(&emote, "id"))), std::move(url), std::move(animated));
                }
            }
        }
    }

    ThirdPartyEmotes& ThirdPartyEmotes::Instance() { static ThirdPartyEmotes instance; return instance; }
    ThirdPartyEmotes::~ThirdPartyEmotes() { _stopping = true; if (_worker.joinable()) _worker.join(); }
    std::shared_ptr<ThirdPartyEmotes::EmoteMap const> ThirdPartyEmotes::Snapshot() const { std::lock_guard lock(_lock); return _emotes; }
    std::shared_ptr<ThirdPartyEmotes::BadgeMap const> ThirdPartyEmotes::Badges() const { std::lock_guard lock(_lock); return _badges; }

    std::shared_ptr<ThirdPartyEmotes::EmoteMap const> ThirdPartyEmotes::Merge(Cache const& cache) {
        auto merged = std::make_shared<EmoteMap>();
        for (int index : {3, 4, 5, 0, 1, 2})
            for (auto const& [name, emote] : *cache.sources[index].emotes)
                if (merged->size() < MaxMergedEmotes) merged->try_emplace(name, emote);
        return merged;
    }

    void ThirdPartyEmotes::EnsureLoaded(std::string const& channel, bool emotesEnabled, std::string const& twitchId, bool badgesEnabled) {
        auto login = Login(channel);
        auto wanted = login + (emotesEnabled ? "|e1" : "|e0") + (badgesEnabled ? "|b1|" : "|b0|") + twitchId;
        auto now = std::chrono::steady_clock::now();
        Cache cache;
        uint64_t generation;
        {
            std::lock_guard lock(_lock);
            if (wanted != _wanted) {
                _wanted = wanted; ++_generation; _failures = 0; _nextAttempt = now;
                if (login != _cache.login) {
                    _cache.login = login; _cache.twitchId.clear(); _cache.resourcesLoaded = false;
                    _cache.badges = std::make_shared<BadgeMap const>();
                    for (int i = 3; i < 6; i++) _cache.sources[i] = Source{};
                }
                if (!twitchId.empty()) _cache.twitchId = twitchId;
                if (!emotesEnabled) for (auto& source : _cache.sources) source = Source{};
                if (!badgesEnabled) { _cache.badges = std::make_shared<BadgeMap const>(); _cache.resourcesLoaded = false; }
                _emotes = emotesEnabled ? Merge(_cache) : std::make_shared<EmoteMap const>();
                _badges = badgesEnabled ? _cache.badges : std::make_shared<BadgeMap const>();
                ++_version;
            }
            if (_loading || (!emotesEnabled && !badgesEnabled) || now < _nextAttempt) return;
            // A successful set is periodically refreshed; keep its data during retries.
            if (_failures == 0) {
                for (auto& source : _cache.sources) source.loaded = false;
                _cache.resourcesLoaded = false;
            }
            _loading = true; generation = _generation; cache = _cache;
        }
        if (_worker.joinable()) _worker.join();
        _worker = std::thread([this, cache = std::move(cache), generation, emotesEnabled, badgesEnabled]() mutable {
            bool complete = false;
            try { complete = Load(cache, emotesEnabled, badgesEnabled); }
            catch (std::exception const& ex) { DEBUG("Chat resource load: {}", ex.what()); }
            std::lock_guard lock(_lock);
            _loading = false;
            if (generation != _generation) return;
            _cache = std::move(cache);
            _emotes = emotesEnabled ? Merge(_cache) : std::make_shared<EmoteMap const>();
            _badges = badgesEnabled ? _cache.badges : std::make_shared<BadgeMap const>();
            _failures = complete ? 0 : std::min(_failures + 1, 7);
            auto delay = complete ? std::chrono::seconds(600) : std::chrono::seconds(std::min(300, 5 << (_failures - 1)));
            _nextAttempt = std::chrono::steady_clock::now() + delay;
            ++_version;
            INFO("Chat resources: {} emotes, {} badges; {}", _emotes->size(), _badges->size(), complete ? "loaded" : "partial, retry scheduled");
        });
    }

    bool ThirdPartyEmotes::Load(Cache& cache, bool emotesEnabled, bool badgesEnabled) {
        Net::HttpClient http(_stopping);
        rapidjson::Document doc;
        if (!cache.resourcesLoaded && (badgesEnabled || (emotesEnabled && cache.twitchId.empty() && !cache.login.empty()))) {
            auto query = badgesEnabled
                ? R"(query($login:String!){badges{setID version imageURL(size:DOUBLE)} user(login:$login){id broadcastBadges{setID version imageURL(size:DOUBLE)}}})"
                : R"(query($login:String!){user(login:$login){id}})";
            auto body = std::string("{\"query\":") + Json::Quote(query) + ",\"variables\":{\"login\":" + Json::Quote(cache.login) + "}}";
            if (Read(http, "https://gql.twitch.tv/gql", doc, body) == Fetch::Data && !Json::Get(&doc, "errors") && Json::Get(&doc, "data")) {
                auto user = Json::Path(&doc, {"data", "user"});
                auto id = Json::Scalar(Json::Get(user, "id"));
                if (!id.empty()) cache.twitchId = id;
                auto global = Json::Path(&doc, {"data", "badges"});
                if (!badgesEnabled || Json::IsArray(global)) {
                    auto badges = std::make_shared<BadgeMap>();
                    auto add = [&](Json::Value const* list) {
                        if (!Json::IsArray(list)) return;
                        for (auto const& badge : list->GetArray()) {
                            auto key = Json::Scalar(Json::Get(&badge, "setID")) + "/" + Json::Scalar(Json::Get(&badge, "version"));
                            auto url = Json::Scalar(Json::Get(&badge, "imageURL"));
                            if (!url.empty() && badges->size() < MaxBadges) badges->insert_or_assign(std::move(key), std::move(url));
                        }
                    };
                    if (badgesEnabled) { add(global); add(Json::Get(user, "broadcastBadges")); cache.badges = std::move(badges); }
                    cache.resourcesLoaded = true;
                }
            }
        }
        if (emotesEnabled) {
            for (int index : {0, 1, 2, 5, 3, 4}) {
                auto& source = cache.sources[index];
                if (source.loaded) continue;
                if (index >= 3 && cache.login.empty()) { source = Source{}; source.loaded = true; continue; }
                if ((index == 3 || index == 4) && cache.twitchId.empty()) continue;
                std::string url;
                switch (index) {
                    case 0: url = "https://7tv.io/v3/emote-sets/global"; break;
                    case 1: url = "https://api.betterttv.net/3/cached/emotes/global"; break;
                    case 2: url = "https://api.frankerfacez.com/v1/set/global"; break;
                    case 3: url = "https://7tv.io/v3/users/twitch/" + cache.twitchId; break;
                    case 4: url = "https://api.betterttv.net/3/cached/users/twitch/" + cache.twitchId; break;
                    case 5: url = "https://api.frankerfacez.com/v1/room/" + Net::UrlEncode(cache.login); break;
                }
                auto result = Read(http, url, doc);
                if (result == Fetch::Retry) continue;
                auto emotes = std::make_shared<EmoteMap>();
                if (result == Fetch::Data) {
                    if (index == 0 || index == 3) {
                        auto list = index == 0 ? Json::Get(&doc, "emotes") : Json::Path(&doc, {"emote_set", "emotes"});
                        if (!Json::IsArray(list)) continue;
                        SevenTv(*emotes, list);
                    } else if (index == 1) {
                        if (!doc.IsArray()) continue;
                        Bttv(*emotes, &doc);
                    } else if (index == 4) {
                        if (!Json::IsArray(Json::Get(&doc, "channelEmotes"))) continue;
                        Bttv(*emotes, Json::Get(&doc, "channelEmotes")); Bttv(*emotes, Json::Get(&doc, "sharedEmotes"));
                    } else {
                        auto sets = Json::Get(&doc, "sets");
                        if (!sets || !sets->IsObject()) continue;
                        Ffz(*emotes, sets);
                        if (index == 5 && cache.twitchId.empty()) cache.twitchId = Json::Scalar(Json::Path(&doc, {"room", "twitch_id"}));
                    }
                }
                source.emotes = std::move(emotes); source.loaded = true;
            }
        }
        bool complete = !badgesEnabled || cache.resourcesLoaded;
        if (emotesEnabled) {
            for (auto const& source : cache.sources) complete &= source.loaded;
        }
        return complete;
    }
}
