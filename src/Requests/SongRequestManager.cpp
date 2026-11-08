#include "Requests/SongRequestManager.hpp"
#include "Requests/BeatSaver.hpp"
#include "Requests/BsrCommand.hpp"
#include "Chat/ChatManager.hpp"
#include "Chat/Json.hpp"
#include "Chat/MessageHistory.hpp"
#include "Net/HttpClient.hpp"
#include "Songs/LevelNavigator.hpp"
#include "Songs/MapInstaller.hpp"
#include "Songs/MenuState.hpp"
#include "Config.hpp"
#include "logging.hpp"

#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "scotland2/shared/modloader.h"
#include "songcore/shared/SongCore.hpp"

#include "UnityEngine/Time.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <unordered_set>

namespace StandaloneStreamChat::Requests {
    namespace {
        using namespace std::chrono_literals;
        using Chat::ChatMessage;
        using Chat::ChatPlatform;
        namespace Json = Chat::Json;

        // A fixed abuse guard (the list scrolls).
        constexpr size_t QueueCapacity = 100;
        constexpr auto NotFoundLinger = 30s;
        constexpr auto NoticeInterval = 5s;
        constexpr std::string_view RetryDetail = "BeatSaver unavailable, retrying";
        // In unscaled seconds, on the main thread.
        constexpr float RefreshTimeout = 60.0f;
        constexpr float SongListTimeout = 10.0f;
        // After the song list finishes reloading, SongCore reselects the song
        // that was selected before; give that time to land before ours.
        constexpr float SongListSettle = 0.25f;

        std::filesystem::path StoredQueuePath() { return ConfigDirectory() / (MOD_ID ".Requests.json"); }

        std::string_view PlatformName(ChatPlatform platform) {
            switch (platform) {
                case ChatPlatform::Twitch:
                    return "Twitch";
                case ChatPlatform::YouTube:
                    return "YouTube";
                case ChatPlatform::Kick:
                    return "Kick";
                case ChatPlatform::System:
                    break;
            }
            return "System";
        }

        std::optional<ChatPlatform> PlatformFromName(std::string_view name) {
            for (auto platform : {ChatPlatform::Twitch, ChatPlatform::YouTube, ChatPlatform::Kick}) {
                if (name == PlatformName(platform))
                    return platform;
            }
            return std::nullopt;
        }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
            return text;
        }

        // Cooldowns and per-viewer caps follow the platform's user id when there
        // is one: YouTube display names aren't unique, and a rename shouldn't
        // reset a limit. The display name is the fallback.
        std::string UserKey(ChatPlatform platform, std::string const& id, std::string const& name) {
            return std::string(PlatformName(platform)) + (id.empty() ? ":name:" + Lower(name) : ":id:" + id);
        }
        std::string UserKey(ChatMessage const& message) { return UserKey(message.platform, message.userId, message.author); }
        std::string UserKey(SongRequest const& request) { return UserKey(request.platform, request.requesterId, request.requester); }

        // A request restored from a queue saved before 0.10.0 has no id, so it
        // matches by name.
        bool SameRequester(SongRequest const& request, ChatMessage const& message) {
            if (request.platform != message.platform)
                return false;
            if (!request.requesterId.empty() && !message.userId.empty())
                return request.requesterId == message.userId;
            return Lower(request.requester) == Lower(message.author);
        }

        // Main-thread wording for an install that refreshed but didn't load.
        constexpr std::string_view CouldNotLoad = "SongCore couldn't load this map version";
        constexpr std::string_view RetryFreshCopy = "installed copy didn't load; Accept downloads it again";

        bool Allowed(ChatMessage const& message) {
            switch (GetConfig().whoCanRequest) {
                case 2:
                    return message.isBroadcaster;
                case 1:
                    return message.isBroadcaster || message.isModerator;
                default:
                    return true;
            }
        }

        // "<key> (<song> - <mapper>)", the folder name SongDownloader uses, with
        // characters file systems refuse swapped out.
        std::string FolderName(SongRequest const& request) {
            std::string name = request.key + " (" + request.songName + " - " + request.mapper + ")";
            for (auto& c : name) {
                if (static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
                    c = '_';
            }
            // Keep it short, without cutting a UTF-8 character in half.
            if (name.size() > 120) {
                size_t cut = 120;
                while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80)
                    cut--;
                name.resize(cut);
            }
            while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
                name.pop_back();
            return name;
        }

        float Now() { return UnityEngine::Time::get_unscaledTime(); }

        double Jitter() {
            thread_local std::minstd_rand random(std::random_device{}());
            return std::uniform_real_distribution<double>(0.0, 1.0)(random);
        }

        void DeleteStored() {
            std::error_code ec;
            std::filesystem::remove(StoredQueuePath(), ec);
        }

        void WriteStored(std::vector<SongRequest> const& requests) {
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            writer.StartArray();
            for (auto const& request : requests) {
                auto platform = PlatformName(request.platform);
                writer.StartObject();
                writer.Key("Key");
                writer.String(request.key.c_str(), request.key.size());
                writer.Key("RequesterName");
                writer.String(request.requester.c_str(), request.requester.size());
                if (!request.requesterId.empty()) {
                    writer.Key("RequesterId");
                    writer.String(request.requesterId.c_str(), request.requesterId.size());
                }
                auto writeSource = [&](char const* key, std::string const& value) {
                    if (!value.empty()) {
                        writer.Key(key);
                        writer.String(value.c_str(), value.size());
                    }
                };
                writeSource("RequesterLogin", request.requesterLogin);
                writeSource("SourceMessageId", request.sourceMessageId);
                writeSource("SourceChannelId", request.sourceChannelId);
                writeSource("SourceChannelName", request.sourceChannelName);
                if (request.status == RequestStatus::Failed) {
                    writer.Key("Retryable");
                    writer.Bool(true);
                }
                writer.Key("RequesterPlatform");
                writer.String(platform.data(), platform.size());
                writer.EndObject();
            }
            writer.EndArray();

            // Swapped in whole, so a crash mid-save keeps the last good queue.
            auto path = StoredQueuePath();
            if (!WriteFileReplacing(path, {buffer.GetString(), buffer.GetSize()}))
                WARNING("Could not save the request queue to {}", path.string());
        }
    }

    SongRequestManager& SongRequestManager::Instance() {
        static SongRequestManager instance;
        return instance;
    }

    SongRequestManager::SongRequestManager() = default;

    SongRequestManager::~SongRequestManager() { Stop(); }

    void SongRequestManager::Stop() {
        {
            std::lock_guard lock(_lock);
            _stopping = true;
        }
        _wake.notify_all();
        _saveWake.notify_all();
        if (_worker.joinable())
            _worker.join();
        if (_saver.joinable())
            _saver.join();  // after writing any save still pending
        _installer.reset();
    }

    void SongRequestManager::Start() {
        if (_worker.joinable())
            return;
        _stopping = false;
        LoadStored();
        _installer = std::make_unique<Songs::MapInstaller>([this](int64_t id, float fraction) { SetProgress(id, fraction); });
        _worker = std::thread(&SongRequestManager::RunWorker, this);
        _saver = std::thread(&SongRequestManager::RunSaver, this);
    }

    void SongRequestManager::LoadStored() {
        auto const& config = GetConfig();
        if (!config.persistRequestQueue) {
            DeleteStored();
            return;
        }
        auto path = StoredQueuePath();
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return;
        std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        rapidjson::Document doc;
        doc.Parse(json.c_str(), json.size());
        if (doc.HasParseError() || !doc.IsArray()) {
            WARNING("Ignoring unreadable saved request queue {}", path.string());
            return;
        }

        std::unordered_set<std::string> pendingKeys;
        if (!config.allowDuplicates) {
            for (auto const& item : doc.GetArray()) {
                auto retryable = Json::Get(&item, "Retryable");
                if (retryable && retryable->IsTrue())
                    continue;
                auto key = Lower(Json::Scalar(Json::Get(&item, "Key")));
                key.erase(0, std::min(key.find_first_not_of('0'), key.size()));
                if (IsValidKey(key) && !Json::Scalar(Json::Get(&item, "RequesterName")).empty() &&
                    PlatformFromName(Json::Scalar(Json::Get(&item, "RequesterPlatform"))))
                    pendingKeys.insert(std::move(key));
            }
        }
        std::lock_guard lock(_lock);
        for (auto const& item : doc.GetArray()) {
            if (_queue.size() >= QueueCapacity)
                break;
            auto key = Lower(Json::Scalar(Json::Get(&item, "Key")));
            key.erase(0, std::min(key.find_first_not_of('0'), key.size()));
            auto requester = Json::Scalar(Json::Get(&item, "RequesterName"));
            auto requesterId = Json::Scalar(Json::Get(&item, "RequesterId"));  // absent before 0.10.0
            auto platform = PlatformFromName(Json::Scalar(Json::Get(&item, "RequesterPlatform")));
            if (!IsValidKey(key) || requester.empty() || !platform)
                continue;
            auto retryable = Json::Get(&item, "Retryable");
            if (retryable && retryable->IsTrue() && pendingKeys.contains(key))
                continue;
            if (!config.allowDuplicates && std::any_of(_queue.begin(), _queue.end(), [&](Entry const& e) { return e.request.key == key; }))
                continue;
            Entry entry;
            entry.request.id = ++_nextId;
            entry.request.key = std::move(key);
            entry.request.requester = std::move(requester);
            entry.request.requesterId = std::move(requesterId);
            entry.request.requesterLogin = Json::Scalar(Json::Get(&item, "RequesterLogin"));
            entry.request.sourceMessageId = Json::Scalar(Json::Get(&item, "SourceMessageId"));
            entry.request.sourceChannelId = Json::Scalar(Json::Get(&item, "SourceChannelId"));
            entry.request.sourceChannelName = Json::Scalar(Json::Get(&item, "SourceChannelName"));
            entry.request.platform = *platform;
            entry.nextLookup = Clock::now();
            _queue.push_back(std::move(entry));
        }
        if (!_queue.empty()) {
            INFO("Restored {} saved song request(s)", _queue.size());
            _version++;
        }
    }

    void SongRequestManager::OnChatMessage(ChatMessage const& message) {
        if (message.platform == ChatPlatform::System || message.author.empty() || !GetConfig().enableSongRequests)
            return;
        auto command = ParseBsr(message.text);
        if (!command.isCommand)
            return;

        auto user = UserKey(message);
        auto mention = "@" + message.author;
        std::lock_guard lock(_lock);
        if (command.key.empty()) {
            NoticeLocked(mention + " that isn't a valid map key.", "invalid:" + user);
            return;
        }
        auto result = TryEnqueueLocked(message, command.key);
        if (result != Enqueue::Ok)
            INFO("Song request {} from {} rejected ({})", command.key, message.author, static_cast<int>(result));
        switch (result) {
            case Enqueue::Ok:
                break;  // announced with its position by TryEnqueueLocked
            case Enqueue::NotAllowed:
                NoticeLocked(mention + " song requests aren't open to you right now.", "not-allowed:" + user);
                break;
            case Enqueue::OnCooldown:
                NoticeLocked(mention + " please wait before requesting again.", "cooldown:" + user);
                break;
            case Enqueue::QueueFull:
                NoticeLocked("The request queue is full.", "queue-full");
                break;
            case Enqueue::TooManyPerUser:
                NoticeLocked(mention + " you already have the max requests queued.", "too-many:" + user);
                break;
            case Enqueue::Duplicate:
                NoticeLocked(mention + " that map is already in the queue.", "duplicate:" + user + ":" + command.key);
                break;
        }
    }

    void SongRequestManager::OnModeration(ChatMessage const& event) {
        std::lock_guard lock(_lock);
        auto removed = std::erase_if(_queue, [&](Entry const& entry) {
            auto const& request = entry.request;
            if (IsInProgress(request.status) ||
                !Chat::MatchesChannel(event, request.platform, request.sourceChannelId, request.sourceChannelName))
                return false;
            switch (event.event) {
                case Chat::ChatEvent::RemoveMessage:
                    return !event.messageId.empty() && request.sourceMessageId == event.messageId;
                case Chat::ChatEvent::RemoveUser:
                    return !event.userId.empty() ? request.requesterId == event.userId
                                                : !event.authorLogin.empty() && request.requesterLogin == event.authorLogin;
                case Chat::ChatEvent::ClearChannel:
                    return true;
                default:
                    return false;
            }
        });
        if (removed > 0)
            ChangedLocked(true);
    }

    SongRequestManager::Enqueue SongRequestManager::TryEnqueueLocked(ChatMessage const& message, std::string const& key) {
        auto const& config = GetConfig();
        if (!Allowed(message))
            return Enqueue::NotAllowed;

        auto user = UserKey(message);
        // The broadcaster and mods run the queue: never throttle or cap them
        // (which also keeps solo testing from tripping the limits).
        bool trusted = message.isBroadcaster || message.isModerator;
        auto now = Clock::now();
        auto cooldown = std::chrono::seconds(std::max(0, config.perUserCooldownSeconds.Get()));
        if (auto last = _lastRequest.find(user); !trusted && cooldown > 0s && last != _lastRequest.end() && now - last->second < cooldown)
            return Enqueue::OnCooldown;
        if (_queue.size() >= QueueCapacity)
            return Enqueue::QueueFull;
        // A not-found or failed entry doesn't block asking for the same map again.
        if (!config.allowDuplicates && std::any_of(_queue.begin(), _queue.end(), [&](Entry const& e) {
                return e.request.key == key && e.request.status != RequestStatus::NotFound &&
                       e.request.status != RequestStatus::Failed && e.request.status != RequestStatus::Unplayable;
            }))
            return Enqueue::Duplicate;
        if (!trusted && config.maxRequestsPerUser > 0) {
            auto mine = std::count_if(_queue.begin(), _queue.end(), [&](Entry const& e) { return SameRequester(e.request, message); });
            if (mine >= config.maxRequestsPerUser)
                return Enqueue::TooManyPerUser;
        }

        Entry entry;
        entry.request.id = ++_nextId;
        entry.request.key = key;
        entry.request.requester = message.author;
        entry.request.requesterId = message.userId;
        entry.request.requesterLogin = message.authorLogin;
        entry.request.sourceMessageId = message.messageId;
        entry.request.sourceChannelId = message.channelId;
        entry.request.sourceChannelName = message.channelName;
        entry.request.platform = message.platform;
        entry.nextLookup = now;
        _queue.push_back(std::move(entry));
        _lastRequest[user] = now;
        _added = true;
        ChangedLocked(true);
        INFO("Song request queued (#{}): {} by {}", _queue.size(), key, message.author);
        NoticeLocked(fmt::format("Request added (#{}): {}", _queue.size(), key));
        return Enqueue::Ok;
    }

    SongRequestManager::Entry* SongRequestManager::FindLocked(int64_t id) {
        auto it = std::find_if(_queue.begin(), _queue.end(), [id](Entry const& e) { return e.request.id == id; });
        return it == _queue.end() ? nullptr : &*it;
    }

    void SongRequestManager::NoticeLocked(std::string text, std::string const& rateKey) {
        if (!rateKey.empty()) {
            auto now = Clock::now();
            auto [last, inserted] = _lastNotice.try_emplace(rateKey, now);
            if (!inserted) {
                if (now - last->second < NoticeInterval)
                    return;
                last->second = now;
            }
        }
        Chat::ChatManager::Instance().AddSystemMessage(std::move(text));
    }

    void SongRequestManager::ChangedLocked(bool save) {
        _version++;
        if (save) {
            _saveDue = true;
            _saveWake.notify_one();
        }
        _wake.notify_one();
    }

    int SongRequestManager::Version() const {
        std::lock_guard lock(_lock);
        return _version;
    }

    std::vector<SongRequest> SongRequestManager::Snapshot() const {
        std::lock_guard lock(_lock);
        std::vector<SongRequest> requests;
        requests.reserve(_queue.size());
        for (auto const& entry : _queue)
            requests.push_back(entry.request);
        return requests;
    }

    size_t SongRequestManager::Count() const {
        std::lock_guard lock(_lock);
        return _queue.size();
    }

    void SongRequestManager::PersistSettingChanged() {
        // The saver writes or deletes by the setting's value when it runs, so
        // toggling off and on again before it wakes still ends with a save.
        std::lock_guard lock(_lock);
        _saveDue = true;
        _saveWake.notify_one();
    }

    void SongRequestManager::RunSaver() {
        std::unique_lock lock(_lock);
        while (true) {
            _saveWake.wait(lock, [this] { return _saveDue || _stopping; });
            if (!_saveDue)
                return;  // stopping, nothing left to write
            _saveDue = false;
            bool remove = !GetConfig().persistRequestQueue;
            std::vector<SongRequest> requests;
            if (!remove) {
                for (auto const& entry : _queue) {
                    if (entry.request.status != RequestStatus::NotFound && entry.request.status != RequestStatus::Unplayable)
                        requests.push_back(entry.request);
                }
            }
            lock.unlock();
            if (remove)
                DeleteStored();
            else
                WriteStored(requests);
            lock.lock();
        }
    }

    void SongRequestManager::RunWorker() {
        std::optional<Net::HttpClient> http;
        std::unique_lock lock(_lock);
        while (!_stopping) {
            auto now = Clock::now();
            PruneLocked(now);

            // One lookup at a time, so a restored full queue doesn't burst BeatSaver.
            auto wakeAt = now + 1min;
            Entry* due = nullptr;
            for (auto& entry : _queue) {
                if (entry.request.status == RequestStatus::NotFound) {
                    wakeAt = std::min(wakeAt, entry.dropAt);
                } else if (entry.request.status == RequestStatus::FetchingInfo) {
                    if (entry.nextLookup <= now) {
                        due = &entry;
                        break;
                    }
                    wakeAt = std::min(wakeAt, entry.nextLookup);
                }
            }
            if (!due) {
                _wake.wait_until(lock, wakeAt);
                continue;
            }

            auto id = due->request.id;
            auto key = due->request.key;
            lock.unlock();
            MapLookup lookup;
            try {
                if (!http)
                    http.emplace(_stopping);
                lookup = LookUpMap(*http, key);
            } catch (std::exception const& ex) {
                lookup.error = ex.what();
            }
            lock.lock();
            if (!_stopping)
                ApplyLookupLocked(id, lookup);
        }
    }

    void SongRequestManager::ApplyLookupLocked(int64_t id, MapLookup const& lookup) {
        auto entry = FindLocked(id);
        if (!entry || entry->request.status != RequestStatus::FetchingInfo)
            return;  // denied while the lookup ran
        auto& request = entry->request;
        switch (lookup.outcome) {
            case LookupOutcome::Found:
                request.songName = lookup.info.songName;
                request.songAuthor = lookup.info.songAuthor;
                request.mapper = lookup.info.mapper;
                request.hash = lookup.info.hash;
                request.downloadUrl = lookup.info.downloadUrl;
                request.status = RequestStatus::Ready;
                request.detail.clear();
                ChangedLocked(false);
                break;
            case LookupOutcome::NotFound:
                request.status = RequestStatus::NotFound;
                request.detail = "not found";
                entry->dropAt = Clock::now() + NotFoundLinger;
                ChangedLocked(true);
                NoticeLocked("@" + request.requester + " couldn't find map " + request.key + " on BeatSaver.",
                             "not-found:" + UserKey(request) + ":" + request.key);
                break;
            case LookupOutcome::Error: {
                // Retried without limit, backing off 3, 6, 12, 24, 48, 48... s
                // plus jitter, so an outage never purges valid
                // requests; the streamer can Deny a stuck row meanwhile.
                entry->failedLookups++;
                double delay = static_cast<double>(std::min(48, 3 << std::min(entry->failedLookups - 1, 4)));
                delay *= 1.0 + 0.25 * Jitter();
                entry->nextLookup = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(delay));
                WARNING("BeatSaver lookup for {} failed ({}), retrying in {:.0f} s", request.key, lookup.error, delay);
                if (request.detail != RetryDetail) {
                    request.detail = RetryDetail;
                    ChangedLocked(false);
                }
                break;
            }
        }
    }

    void SongRequestManager::PruneLocked(Clock::time_point now) {
        // Unknown keys leave after a while.
        if (std::erase_if(_queue, [now](Entry const& e) { return e.request.status == RequestStatus::NotFound && now >= e.dropAt; }) > 0)
            ChangedLocked(true);

        // Forget cooldowns and notice times long past, so a long stream
        // doesn't grow these maps without bound.
        if (now < _nextPrune)
            return;
        _nextPrune = now + 1min;
        auto keep = std::max<Clock::duration>(std::chrono::seconds(std::max(0, GetConfig().perUserCooldownSeconds.Get())), 1min);
        std::erase_if(_lastRequest, [&](auto const& item) { return now - item.second >= keep; });
        std::erase_if(_lastNotice, [&](auto const& item) { return now - item.second >= 1min; });
    }

    bool SongRequestManager::CanAccept() const {
        return !_driving && !_install && !_accepted && Songs::MenuState::CanAccept();
    }

    void SongRequestManager::MenuRestarting() {
        ++_driveGeneration;
        _driving = false;
        if (_accepted) {
            SetStatus(*_accepted, RequestStatus::Ready, {});
            _accepted.reset();
        }
        if (!_install)
            return;
        if (_install->stage == Stage::Downloading) {
            _install->openWhenReady = false;
        } else {
            auto id = _install->id;
            auto status = _install->filesInstalled ? RequestStatus::Installed : RequestStatus::Ready;
            _install.reset();
            SetStatus(id, status, {});
        }
    }

    void SongRequestManager::MenuActivated() {
        if (_install || _accepted)
            Drive();
    }

    void SongRequestManager::RefreshInstalledRequests() {
        if (SongCore::API::Loading::AreSongsRefreshing() || !SongCore::API::Loading::AreSongsLoaded())
            return;
        std::lock_guard lock(_lock);
        for (auto& entry : _queue) {
            if ((entry.request.status == RequestStatus::Ready || entry.request.status == RequestStatus::Unplayable) &&
                !entry.request.hash.empty() &&
                SongCore::API::Loading::GetLevelByHash(entry.request.hash)) {
                bool save = entry.request.status == RequestStatus::Unplayable;
                entry.request.status = RequestStatus::Installed;
                entry.request.detail.clear();
                ChangedLocked(save);
            }
        }
    }

    void SongRequestManager::Accept(int64_t id) {
        if (!CanAccept())
            return;
        {
            std::lock_guard lock(_lock);
            auto entry = FindLocked(id);
            if (!entry || (entry->request.status != RequestStatus::Ready && entry->request.status != RequestStatus::Installed &&
                           entry->request.status != RequestStatus::Failed))
                return;
            entry->request.status = RequestStatus::Preparing;
            entry->request.detail.clear();
            entry->request.progress = 0.0f;
            ChangedLocked(false);
            INFO("Song request accepted: {} by {}", entry->request.key, entry->request.requester);
        }
        _accepted = id;
        Drive();
    }

    void SongRequestManager::Deny(int64_t id) {
        std::lock_guard lock(_lock);
        auto entry = FindLocked(id);
        if (!entry)
            return;
        if (IsInProgress(entry->request.status))
            return;
        INFO("Song request denied: {} by {}", entry->request.key, entry->request.requester);
        _queue.erase(_queue.begin() + (entry - _queue.data()));
        ChangedLocked(true);
    }

    void SongRequestManager::SetStatus(int64_t id, RequestStatus status, std::string detail) {
        std::lock_guard lock(_lock);
        auto entry = FindLocked(id);
        if (!entry || (entry->request.status == status && entry->request.detail == detail))
            return;
        auto previous = entry->request.status;
        entry->request.status = status;
        entry->request.detail = std::move(detail);
        if (status != RequestStatus::Downloading)
            entry->request.progress = 0.0f;
        ChangedLocked(status == RequestStatus::Failed || status == RequestStatus::Unplayable ||
                      previous == RequestStatus::Failed || previous == RequestStatus::Unplayable);
    }

    void SongRequestManager::RememberInstalled(int64_t id, std::filesystem::path const& folder) {
        std::lock_guard lock(_lock);
        if (auto entry = FindLocked(id))
            entry->installedFolder = folder;
    }

    void SongRequestManager::RememberLoadFailure(int64_t id, LoadFailure failure) {
        std::lock_guard lock(_lock);
        if (auto entry = FindLocked(id))
            entry->loadFailure = failure;
    }

    void SongRequestManager::SetProgress(int64_t id, float fraction) {
        std::lock_guard lock(_lock);
        auto entry = FindLocked(id);
        if (!entry || entry->request.status != RequestStatus::Downloading)
            return;
        entry->request.progress = std::clamp(fraction, 0.0f, 1.0f);
        entry->request.detail = fmt::format("{}%", static_cast<int>(entry->request.progress * 100.0f));
        ChangedLocked(false);
    }

    void SongRequestManager::Drive() {
        if (_driving)
            return;
        _driving = true;
        auto generation = ++_driveGeneration;
        BSML::MainThreadScheduler::ScheduleUntil([this, generation] { return _driveGeneration != generation || !Step(); }, [] {});
    }

    bool SongRequestManager::Step() {
        try {
            if (_install) {
                Advance();
            } else if (_accepted) {
                auto id = *_accepted;
                _accepted.reset();
                Begin(id);
            } else {
                _driving = false;
                return false;
            }
        } catch (std::exception const& ex) {
            ERROR("Song request preparation failed: {}", ex.what());
            FinishInstall(_install && _install->filesInstalled ? RequestStatus::Installed : RequestStatus::Failed,
                          "preparation failed; select again");
        } catch (...) {
            ERROR("Song request preparation failed");
            FinishInstall(_install && _install->filesInstalled ? RequestStatus::Installed : RequestStatus::Failed,
                          "preparation failed; select again");
        }
        return true;
    }

    void SongRequestManager::Begin(int64_t id) {
        SongRequest request;
        std::filesystem::path installedFolder;
        LoadFailure failure;
        {
            std::lock_guard lock(_lock);
            auto entry = FindLocked(id);
            if (!entry || entry->request.status != RequestStatus::Preparing)
                return;
            request = entry->request;
            installedFolder = entry->installedFolder;
            failure = entry->loadFailure;
        }
        _install = Install{};
        _install->id = id;
        _install->hash = request.hash;
        _install->url = request.downloadUrl;
        _install->previousFailure = failure;
        _install->installedFolder = std::move(installedFolder);
        _install->folderName = FolderName(request);
        if (request.hash.empty()) {
            FinishInstall(RequestStatus::Failed, "missing map hash");
            return;
        }
        _install->deadline = Now() + RefreshTimeout;
    }

    void SongRequestManager::ResolveInstallPaths() {
        auto& install = *_install;
        auto root = SongCore::API::Loading::GetPreferredCustomLevelPath();
        auto legacy = root / install.folderName;
        // Hash is part of the path: a different loaded version is never overwritten.
        install.folder = root / (install.folderName + " [" + install.hash + "]");
        // Files on disk are checked every time, so a folder deleted since the
        // last Accept is downloaded again. After existing files failed to load,
        // they are skipped and a fresh copy is downloaded beside them.
        if (install.previousFailure == LoadFailure::None) {
            install.filesInstalled = (!install.installedFolder.empty() && Songs::MapInstaller::HasMapFiles(install.installedFolder)) ||
                                     Songs::MapInstaller::HasMapFiles(install.folder);
            install.probeExisting = Songs::MapInstaller::HasMapFiles(legacy);
        }
    }

    void SongRequestManager::BeginOpening(float now) {
        auto& install = *_install;
        install.filesInstalled = true;
        RememberLoadFailure(install.id, LoadFailure::None);
        install.stage = Stage::Opening;
        install.openAfter = now + SongListSettle;
        install.deadline = now + SongListTimeout;
        SetStatus(install.id, RequestStatus::Selecting, "waiting for the song list");
    }

    void SongRequestManager::StartDownload(float now) {
        auto& install = *_install;
        if (!Songs::MenuState::CanOpenSong()) {
            FinishInstall(RequestStatus::Ready, std::string(Songs::ReturnToMenu));
            return;
        }
        if (install.url.empty()) {
            FinishInstall(RequestStatus::Failed, "missing download URL");
            return;
        }
        // Even an edited/corrupt copy at the hash path is not overwritten.
        auto base = install.folder;
        int suffix = 1;
        while (std::filesystem::exists(install.folder)) {
            if (++suffix > 1000)
                throw std::runtime_error("too many map destination collisions");
            install.folder = base.string() + " (" + std::to_string(suffix) + ")";
        }
        SetStatus(install.id, RequestStatus::Downloading, "");
        SetProgress(install.id, 0.0f);
        INFO("Downloading requested map into {}", install.folder.string());
        _installer->Install({install.id, install.url, install.folder});
        install.stage = Stage::Downloading;
    }

    void SongRequestManager::Advance() {
        auto& install = *_install;
        auto id = install.id;
        auto now = Now();
        switch (install.stage) {
            case Stage::Checking:
                if (SongCore::API::Loading::AreSongsRefreshing()) {
                    if (now >= install.deadline)
                        FinishInstall(install.filesInstalled ? RequestStatus::Installed : RequestStatus::Ready,
                                      "song loading timed out; select again");
                    return;
                }
                if (SongCore::API::Loading::GetLevelByHash(install.hash)) {
                    INFO("Requested hash {} is already loaded; skipping download and refresh", install.hash);
                    BeginOpening(now);
                    return;
                }
                ResolveInstallPaths();
                if (install.filesInstalled || (install.probeExisting && !install.existingChecked)) {
                    install.existingChecked = true;
                    install.stage = Stage::WaitingForRefresh;
                    install.deadline = now + RefreshTimeout;
                    SetStatus(id, RequestStatus::Installing, "loading installed files");
                } else {
                    StartDownload(now);
                }
                return;

            case Stage::Downloading: {
                auto result = _installer->Take();
                if (!result || result->id != id)
                    return;
                if (!result->ok) {
                    FinishInstall(RequestStatus::Failed, result->error);
                    return;
                }
                install.filesInstalled = true;
                install.downloaded = true;
                RememberInstalled(id, install.folder);
                if (!install.openWhenReady) {
                    FinishInstall(RequestStatus::Installed, "select again after restart");
                    return;
                }
                install.stage = Stage::WaitingForRefresh;
                install.deadline = now + RefreshTimeout;
                SetStatus(id, RequestStatus::Installing, "loading installed files");
                return;
            }

            case Stage::WaitingForRefresh:
                if (!Songs::MenuState::CanOpenSong()) {
                    FinishInstall(install.filesInstalled ? RequestStatus::Installed : RequestStatus::Ready,
                                  "ready to select from the main menu or Solo");
                    return;
                }
                if (SongCore::API::Loading::AreSongsRefreshing()) {
                    if (now >= install.deadline)
                        FinishInstall(install.filesInstalled ? RequestStatus::Installed : RequestStatus::Ready,
                                      "song loading timed out; select again");
                    return;
                }
                if (SongCore::API::Loading::GetLevelByHash(install.hash)) {
                    BeginOpening(now);
                    return;
                }
                install.refresh = SongCore::API::Loading::RefreshSongs(false);
                if (!install.refresh.valid()) {
                    FinishInstall(install.filesInstalled ? RequestStatus::Installed : RequestStatus::Ready,
                                  "SongCore is not ready; select again");
                    return;
                }
                install.stage = Stage::Refreshing;
                install.deadline = now + RefreshTimeout;
                return;

            case Stage::Refreshing:
                if (install.refresh.wait_for(0s) != std::future_status::ready) {
                    if (now >= install.deadline)
                        FinishInstall(install.filesInstalled ? RequestStatus::Installed : RequestStatus::Ready,
                                      "song loading timed out; select again");
                    return;
                }
                install.refresh.get();
                if (SongCore::API::Loading::GetLevelByHash(install.hash)) {
                    BeginOpening(now);
                } else if (!install.filesInstalled) {
                    // An occupied legacy path may contain another map version.
                    // Download this hash to a new path after the one discovery refresh.
                    StartDownload(now);
                } else if (install.downloaded) {
                    // Our own fresh copy: SongCore can't load it, or loads it
                    // under another hash. Retrying wouldn't change that.
                    WARNING("SongCore refreshed without loading downloaded hash {}", install.hash);
                    FinishInstall(RequestStatus::Unplayable, std::string(CouldNotLoad));
                } else {
                    // An older or damaged copy; the next Accept downloads a fresh one.
                    WARNING("SongCore refreshed without loading installed hash {}", install.hash);
                    RememberLoadFailure(id, LoadFailure::ExistingCopy);
                    FinishInstall(RequestStatus::Failed, std::string(RetryFreshCopy));
                }
                return;

            case Stage::Opening:
                if (!Songs::MenuState::CanOpenSong()) {
                    FinishInstall(RequestStatus::Installed, std::string(Songs::ReturnToMenu));
                    return;
                }
                if (now >= install.deadline) {
                    FinishInstall(RequestStatus::Installed, "song list timed out; select again");
                    return;
                }
                if (SongCore::API::Loading::AreSongsRefreshing() ||
                    (Songs::MenuState::SongListShowing() && !Songs::SongListIdle()) || now < install.openAfter)
                    return;
                install.stage = Stage::Selecting;
                install.selection = {};
                install.deadline = now + SongListTimeout;
                SetStatus(id, RequestStatus::Selecting, "");
                return;

            case Stage::Selecting: {
                if (SongCore::API::Loading::AreSongsRefreshing()) {
                    if (now >= install.deadline)
                        FinishInstall(RequestStatus::Installed, "song list timed out; select again");
                    return;
                }
                auto level = SongCore::API::Loading::GetLevelByHash(install.hash);
                if (!level) {
                    FinishInstall(RequestStatus::Installed, "loading failed; select again");
                    return;
                }
                std::string error;
                auto result = Songs::AdvanceSelection(level, install.selection, error);
                if (result == Songs::SelectionResult::Pending) {
                    if (now >= install.deadline) {
                        WARNING("Selection timed out for {} in phase {}", install.hash, static_cast<int>(install.selection.phase));
                        FinishInstall(RequestStatus::Installed, "selection timed out; select again");
                    }
                    return;
                }
                if (result == Songs::SelectionResult::Failed) {
                    FinishInstall(RequestStatus::Installed, error);
                    return;
                }

                _install.reset();
                std::lock_guard lock(_lock);
                if (auto entry = FindLocked(id)) {
                    NoticeLocked("Map selected: " + entry->request.songName + " (requested by " + entry->request.requester + ")");
                    _queue.erase(_queue.begin() + (entry - _queue.data()));
                    ChangedLocked(true);
                }
                _opened = true;
                return;
            }
        }
    }

    void SongRequestManager::FinishInstall(std::optional<RequestStatus> status, std::string detail) {
        if (!_install)
            return;
        auto id = _install->id;
        Songs::ReleaseSelectionFilter(_install->selection);
        if (!detail.empty())
            WARNING("Request {} retained: {}", id, detail);
        _install.reset();
        if (status)
            SetStatus(id, *status, std::move(detail));
    }
}
