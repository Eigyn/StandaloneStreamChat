#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace StandaloneStreamChat::Songs {
    /// Downloads and unpacks one map at a time on its own thread, so neither
    /// the network nor the disk ever holds up the game. A map is unpacked into
    /// a scratch folder first and only moved into place once complete, so a
    /// failed download never leaves a half-written song for SongCore to load
    /// or replaces a good one.
    class MapInstaller {
       public:
        struct Job {
            int64_t id = 0;
            std::string url;
            std::filesystem::path target;  // the song's folder under CustomLevels
        };
        struct Result {
            int64_t id = 0;
            bool ok = false;
            std::string error;  // short, for the request row
        };
        /// Called on the installer thread as the download advances (0..1).
        using Progress = std::function<void(int64_t id, float fraction)>;

        explicit MapInstaller(Progress progress);
        ~MapInstaller();
        MapInstaller(MapInstaller const&) = delete;
        MapInstaller& operator=(MapInstaller const&) = delete;

        /// Starts a job; the previous one must have finished (Take returned it).
        void Install(Job job);
        /// The finished job's result, once.
        std::optional<Result> Take();
        /// A candidate installation; the expected hash must still resolve in SongCore.
        static bool HasMapFiles(std::filesystem::path const& folder);

       private:
        void Run();
        void Download(std::string const& url, int64_t id, std::string& body);
        void Unpack(Job const& job, std::string const& body);

        Progress _progress;
        std::mutex _lock;
        std::condition_variable _wake;
        std::optional<Job> _job;
        std::optional<Result> _result;
        std::atomic<bool> _stopping{false};
        std::thread _thread;  // last: starts once everything above exists
    };
}
