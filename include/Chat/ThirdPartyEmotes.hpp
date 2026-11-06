#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace SimpleBSStreamChat::Chat {
    class ThirdPartyEmotes {
       public:
        struct Emote { std::string key, url, animatedUrl; };
        using EmoteMap = std::unordered_map<std::string, Emote>;
        using BadgeMap = std::unordered_map<std::string, std::string>; // set/version -> image
        static ThirdPartyEmotes& Instance();
        ~ThirdPartyEmotes();
        void EnsureLoaded(std::string const& login, bool emotesEnabled, std::string const& twitchId = {}, bool badgesEnabled = true);
        std::shared_ptr<EmoteMap const> Snapshot() const;
        std::shared_ptr<BadgeMap const> Badges() const;
        int Version() const { return _version; }

       private:
        struct Source {
            std::shared_ptr<EmoteMap const> emotes = std::make_shared<EmoteMap const>();
            bool loaded = false;
        };
        struct Cache {
            std::string login, twitchId;
            std::array<Source, 6> sources; // global 7TV/BTTV/FFZ, channel 7TV/BTTV/FFZ
            std::shared_ptr<BadgeMap const> badges = std::make_shared<BadgeMap const>();
            bool resourcesLoaded = false;
        };
        ThirdPartyEmotes() = default;
        bool Load(Cache& cache, bool emotesEnabled, bool badgesEnabled);
        static std::shared_ptr<EmoteMap const> Merge(Cache const& cache);
        mutable std::mutex _lock;
        Cache _cache;
        std::shared_ptr<EmoteMap const> _emotes = std::make_shared<EmoteMap const>();
        std::shared_ptr<BadgeMap const> _badges = std::make_shared<BadgeMap const>();
        std::string _wanted;
        uint64_t _generation = 0;
        bool _loading = false;
        int _failures = 0;
        std::chrono::steady_clock::time_point _nextAttempt{};
        std::atomic<int> _version{0};
        std::atomic<bool> _stopping{false};
        std::thread _worker;
    };
}
