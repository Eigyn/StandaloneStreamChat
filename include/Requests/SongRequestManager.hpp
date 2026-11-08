#pragma once

#include "Chat/ChatMessage.hpp"
#include "Requests/SongRequest.hpp"
#include "Songs/LevelNavigator.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace StandaloneStreamChat::Net {
    class HttpClient;
}
namespace StandaloneStreamChat::Songs {
    class MapInstaller;
}

namespace StandaloneStreamChat::Requests {
    struct MapLookup;

    /// The song-request queue. "!bsr" commands arrive on the chat threads; a
    /// worker thread looks each map up on BeatSaver (retrying while it's
    /// unreachable) and drops unknown keys after a while, and a saver thread
    /// keeps the stored queue current.
    /// The UI polls Version() and copies a Snapshot() when it changes, as with
    /// ChatManager.
    ///
    /// One active acceptance downloads only when needed, loads the expected
    /// hash, then filters and confirms selection before consuming the request.
    class SongRequestManager {
       public:
        static SongRequestManager& Instance();

        /// Restores the saved queue and starts the workers. Main thread, once.
        void Start();
        /// Handles a "!bsr" message. Any thread.
        void OnChatMessage(Chat::ChatMessage const& message);
        /// Prepares/selects a request. Main thread; one active acceptance.
        void Accept(int64_t id);
        bool CanAccept() const;
        /// Main-thread availability check for ready rows, called by the UI.
        void RefreshInstalledRequests();
        /// Removes a request that isn't installing. Main thread.
        void Deny(int64_t id);
        /// Saves, or deletes, the stored queue after Persist Queue changed.
        void PersistSettingChanged();

        int Version() const;
        std::vector<SongRequest> Snapshot() const;
        size_t Count() const;
        /// True once after a request joined the queue (the button's unread mark).
        bool TakeAdded() { return _added.exchange(false); }
        /// True once after an accepted map opened in the song list.
        bool TakeOpened() { return _opened.exchange(false); }

       private:
        using Clock = std::chrono::steady_clock;

        /// Installed files that SongCore refreshed without loading the hash.
        /// ExistingCopy: files from before this Accept (an older or damaged
        /// copy), so the next Accept downloads a fresh one. DownloadedCopy: our
        /// own fresh download, so the next Accept reports it instead of
        /// refreshing the whole library again.
        enum class LoadFailure { None, ExistingCopy, DownloadedCopy };

        struct Entry {
            SongRequest request;
            Clock::time_point nextLookup;  // FetchingInfo: when to ask BeatSaver
            int failedLookups = 0;
            Clock::time_point dropAt;  // NotFound: when it leaves the queue
            // Where this session's download went, which may be a de-duplicated
            // name; checked again on every Accept in case it was deleted.
            std::filesystem::path installedFolder;
            LoadFailure loadFailure = LoadFailure::None;
        };

        enum class Stage { Checking, Downloading, WaitingForRefresh, Refreshing, Opening, Selecting };
        struct Install {
            int64_t id = 0;
            std::string hash;
            std::string url;
            std::filesystem::path folder;
            bool filesInstalled = false;
            bool downloaded = false;  // files came from this Accept's download
            LoadFailure previousFailure = LoadFailure::None;
            bool probeExisting = false;
            bool existingChecked = false;
            Stage stage = Stage::Checking;
            std::shared_future<void> refresh;
            float deadline = 0.0f;   // unscaled time to give up waiting
            float openAfter = 0.0f;  // unscaled time the song list may be touched
            Songs::SelectionState selection;
        };

        enum class Enqueue { Ok, NotAllowed, OnCooldown, QueueFull, TooManyPerUser, Duplicate };

        SongRequestManager();
        ~SongRequestManager();

        Enqueue TryEnqueueLocked(Chat::ChatMessage const& message, std::string const& key);
        Entry* FindLocked(int64_t id);
        /// Posts `text` to the chat feed, at most once per 5 s per `rateKey`.
        /// May be called with _lock held (ChatManager never calls in under its own).
        void NoticeLocked(std::string text, std::string const& rateKey = {});
        void ChangedLocked(bool save);

        void LoadStored();

        // Lookup worker thread.
        void RunWorker();
        void ApplyLookupLocked(int64_t id, MapLookup const& lookup);
        void PruneLocked(Clock::time_point now);
        // Saver thread: writes or deletes the stored queue, so a save never
        // waits behind a BeatSaver lookup, and flushes a pending save on stop.
        void RunSaver();

        // Installs; main thread.
        void Drive();
        bool Step();
        void Begin(int64_t id);
        void Advance();
        void StartDownload(float now);
        void BeginOpening(float now);
        void RememberInstalled(int64_t id, std::filesystem::path const& folder);
        void RememberLoadFailure(int64_t id, LoadFailure failure);
        /// No-op (no redraw) when nothing changes, since waiting steps repeat every frame.
        void SetStatus(int64_t id, RequestStatus status, std::string detail);
        void SetProgress(int64_t id, float fraction);  // any thread
        /// Ends the current install, leaving its request in `status` (if any).
        void FinishInstall(std::optional<RequestStatus> status, std::string detail = {});

        mutable std::mutex _lock;
        std::condition_variable _wake;      // lookup worker
        std::condition_variable _saveWake;  // saver
        std::vector<Entry> _queue;
        int _version = 0;
        int64_t _nextId = 0;
        // Write (or, with Persist Queue off, delete) the stored queue. The
        // saver reads the setting when it runs, so the latest value wins.
        bool _saveDue = false;
        std::atomic<bool> _stopping{false};
        Clock::time_point _nextPrune;
        std::unordered_map<std::string, Clock::time_point> _lastRequest;  // per viewer
        std::unordered_map<std::string, Clock::time_point> _lastNotice;   // per rate key
        std::atomic<bool> _added{false};
        std::atomic<bool> _opened{false};

        // Main thread only.
        std::optional<int64_t> _accepted;
        std::optional<Install> _install;
        bool _driving = false;

        std::unique_ptr<Songs::MapInstaller> _installer;
        std::thread _worker;
        std::thread _saver;
    };
}
