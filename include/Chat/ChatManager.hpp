#pragma once

#include "Chat/ChatMessage.hpp"

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace SimpleBSStreamChat::Chat {
    class ChatService;
    class RecentIds;

    /// Aggregates the chat services into one thread-safe message buffer.
    /// The panel polls Version() from the main thread and copies a snapshot
    /// when it changes, so no Unity API is ever touched from here.
    /// Start/Stop/RestartPlatform are main-thread only.
    class ChatManager {
       public:
        static ChatManager& Instance();

        /// Connects every enabled platform that has a channel set.
        void Start();
        void Stop();
        /// Reconnects one platform from the current config and leaves the
        /// others alone; `clearMessages` drops that platform's messages
        /// (for a channel change).
        void RestartPlatform(ChatPlatform platform, bool clearMessages);

        void AddSystemMessage(std::string text);

        /// The platform's live viewer count, or -1 if unknown, offline or not
        /// connected. Main thread.
        int Viewers(ChatPlatform platform) const;
        std::string ChannelId(ChatPlatform platform) const;

        int Version() const;
        std::vector<ChatMessage> Snapshot() const;

       private:
        ChatManager();
        ~ChatManager();

        static constexpr size_t MaxMessages = 100;
        static constexpr size_t PlatformSlots = 4;  // indexed by ChatPlatform

        void StartPlatform(ChatPlatform platform);
        void RetirePlatform(ChatPlatform platform);
        /// Adds a service's message unless that service has since been retired.
        void AddFromService(ChatMessage message, size_t slot, int generation);
        void PushLocked(ChatMessage message);

        mutable std::mutex _lock;
        std::deque<ChatMessage> _messages;
        int _version = 0;
        uint64_t _nextSequence = 0;
        std::array<int, PlatformSlots> _generations{};  // guarded by _lock
        std::array<std::string, PlatformSlots> _channelIds;  // guarded by _lock
        std::array<std::unique_ptr<ChatService>, PlatformSlots> _services;
        std::shared_ptr<RecentIds> _youTubeSeen;

        /// A retired service winding down off the main thread. Stop() joins
        /// these too, so none outlives the manager its callbacks point at.
        struct Retiring {
            std::thread thread;
            std::shared_ptr<std::atomic<bool>> done;
        };
        std::vector<Retiring> _retiring;  // main thread
        void JoinRetired(bool all);
    };
}
