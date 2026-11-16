#pragma once

#include "Chat/ChatMessage.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace StandaloneStreamChat::Net {
    class HttpClient;
}

namespace StandaloneStreamChat::Chat {
    /// One platform's chat connection, run on its own background thread.
    /// Callbacks fire on that thread. The viewer count is polled on a second
    /// thread, so a slow count request never holds up chat or keepalive
    /// replies. Subclasses must call Stop() in their destructor, before their
    /// own members go away.
    class ChatService {
       public:
        using MessageHandler = std::function<void(ChatMessage)>;
        /// Every status line, to be logged; `showInFeed` is false for a
        /// repeat during an outage (see Report).
        using StatusHandler = std::function<void(std::string text, bool showInFeed)>;

        virtual ~ChatService();
        ChatService(ChatService const&) = delete;
        ChatService& operator=(ChatService const&) = delete;

        void Start();
        /// Signals the thread without waiting, so several services can wind
        /// down at once.
        void RequestStop();
        /// Signals the thread and waits for it. Blocking calls notice within
        /// about a second (network transfers check the flag as they run).
        void Stop();

        /// Live viewer count, or -1 while unknown or offline. Any thread.
        int Viewers() const { return _viewers; }

       protected:
        ChatService(MessageHandler onMessage, StatusHandler onStatus);

        virtual void Run() = 0;
        /// One viewer-count request, on the viewer thread: right away, then
        /// every 30 s while counts are shown. Returns the count, -1 when
        /// offline or unknown, or nothing to keep the last count (a transient
        /// error). May throw.
        virtual std::optional<int> FetchViewers(Net::HttpClient& http) = 0;
        /// Discard the old count and any reply still in flight for that session.
        void InvalidateViewers();
        /// Sleeps in short slices; returns false if asked to stop meanwhile.
        bool SleepFor(std::chrono::milliseconds duration) const;

        /// What a status line says about the connection.
        enum class StatusKind {
            Progress,   ///< connecting, looking up, reconnecting on request
            Connected,  ///< the platform answered: joined, or said the channel is offline
            Problem,    ///< an attempt failed
        };
        /// Sends a status line on. Retries repeat until a connection works, so
        /// only the first Problem of an outage reaches the feed; later Progress
        /// and Problem lines are only logged until the next Connected one.
        /// Service thread only.
        void Report(StatusKind kind, std::string text);

        std::atomic<bool> _stopping{false};
        std::atomic<int> _viewers{-1};
        MessageHandler _onMessage;
        StatusHandler _onStatus;

       private:
        void RunViewers();

        bool _outage = false;  // a Problem was reported since the last Connected
        std::mutex _viewerLock;
        uint64_t _viewerGeneration = 0;
        std::thread _thread;
        std::thread _viewerThread;
    };
}
