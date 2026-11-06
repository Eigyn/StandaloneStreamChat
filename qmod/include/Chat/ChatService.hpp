#pragma once

#include "Chat/ChatMessage.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace SimpleBSStreamChat::Chat {
    /// One platform's chat connection, run on its own background thread.
    /// Callbacks fire on that thread. Subclasses must call Stop() in their
    /// destructor, before their own members go away.
    class ChatService {
       public:
        using MessageHandler = std::function<void(ChatMessage)>;
        using StatusHandler = std::function<void(std::string)>;

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
        /// Sleeps in short slices; returns false if asked to stop meanwhile.
        bool SleepFor(std::chrono::milliseconds duration) const;
        /// True right away, then at most every 30 s, while viewer counts are
        /// shown; the run loops refresh _viewers between chat reads with it.
        bool ViewerPollDue();

        std::atomic<bool> _stopping{false};
        std::atomic<int> _viewers{-1};
        MessageHandler _onMessage;
        StatusHandler _onStatus;

       private:
        std::thread _thread;
        std::chrono::steady_clock::time_point _nextViewerPoll{};
    };
}
