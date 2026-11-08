#include "Chat/ChatManager.hpp"
#include "Chat/KickChatClient.hpp"
#include "Chat/RecentIds.hpp"
#include "Chat/MessageHistory.hpp"
#include "Chat/TwitchIrcClient.hpp"
#include "Chat/YouTubeChatClient.hpp"
#include "Requests/BsrCommand.hpp"
#include "Requests/SongRequestManager.hpp"
#include "Config.hpp"
#include "logging.hpp"

#include <algorithm>
#include <atomic>
#include <optional>
#include <thread>

namespace StandaloneStreamChat::Chat {
    namespace {
        constexpr ChatPlatform Platforms[] = {ChatPlatform::Twitch, ChatPlatform::YouTube, ChatPlatform::Kick};

        size_t SlotOf(ChatPlatform platform) { return static_cast<size_t>(platform); }
    }

    ChatManager& ChatManager::Instance() {
        static ChatManager instance;
        return instance;
    }

    // Kept for every YouTube client so re-enabling doesn't replay the backlog.
    ChatManager::ChatManager() : _youTubeSeen(std::make_shared<RecentIds>(2048)) {}

    ChatManager::~ChatManager() { Stop(); }

    void ChatManager::Start() {
        for (auto platform : Platforms)
            StartPlatform(platform);

        auto const& config = GetConfig();
        if (config.twitchChannel.empty() && config.youTubeChannel.empty() && config.kickChannel.empty())
            AddSystemMessage("Set a Twitch, YouTube or Kick channel in Mods > StandaloneStreamChat.");
    }

    void ChatManager::Stop() {
        // Signal everything first so the services wind down in parallel.
        for (auto& service : _services) {
            if (service)
                service->RequestStop();
        }
        for (auto& service : _services)
            service.reset();
        JoinRetired(true);
    }

    void ChatManager::JoinRetired(bool all) {
        std::erase_if(_retiring, [all](Retiring& retiring) {
            if (!all && !retiring.done->load())
                return false;
            if (retiring.thread.joinable())
                retiring.thread.join();
            return true;
        });
    }

    void ChatManager::RestartPlatform(ChatPlatform platform, bool clearMessages) {
        RetirePlatform(platform);
        if (clearMessages) {
            std::lock_guard lock(_lock);
            std::erase_if(_messages, [platform](ChatMessage const& m) { return m.platform == platform; });
            _version++;
        }
        StartPlatform(platform);
    }

    void ChatManager::StartPlatform(ChatPlatform platform) {
        auto slot = SlotOf(platform);
        if (_services[slot])
            return;

        int generation;
        {
            std::lock_guard lock(_lock);
            generation = ++_generations[slot];
        }
        ChatService::MessageHandler onMessage = [this, slot, generation](ChatMessage message) {
            AddFromService(std::move(message), slot, generation);
        };
        ChatService::StatusHandler onStatus = [this, slot, generation](std::string status) {
            INFO("{}", status);
            if (!GetConfig().showSystemMessages)
                return;
            ChatMessage message;
            message.platform = ChatPlatform::System;
            message.text = std::move(status);
            AddFromService(std::move(message), slot, generation);
        };

        auto const& config = GetConfig();
        std::unique_ptr<ChatService> service;
        switch (platform) {
            case ChatPlatform::Twitch:
                if (config.enableTwitch && !config.twitchChannel.empty())
                    service = std::make_unique<TwitchIrcClient>(config.twitchChannel, onMessage, onStatus);
                break;
            case ChatPlatform::YouTube:
                if (config.enableYouTube && !config.youTubeChannel.empty())
                    service = std::make_unique<YouTubeChatClient>(config.youTubeChannel, _youTubeSeen, onMessage, onStatus);
                break;
            case ChatPlatform::Kick:
                if (config.enableKick && !config.kickChannel.empty())
                    service = std::make_unique<KickChatClient>(config.kickChannel, onMessage, onStatus);
                break;
            case ChatPlatform::System:
                break;
        }
        if (!service)
            return;
        service->Start();
        _services[slot] = std::move(service);
    }

    void ChatManager::RetirePlatform(ChatPlatform platform) {
        auto slot = SlotOf(platform);
        {
            // Anything the old service still delivers is dropped from here on.
            std::lock_guard dispatch(_requestDispatchLock);
            std::lock_guard lock(_lock);
            ++_generations[slot];
            _channelIds[slot].clear();
        }
        auto service = std::move(_services[slot]);
        if (!service)
            return;
        service->RequestStop();
        // Joining can take up to a second while a transfer notices the stop;
        // don't stall the game for it. Earlier ones that finished are joined now.
        JoinRetired(false);
        auto done = std::make_shared<std::atomic<bool>>(false);
        std::thread thread([service = std::move(service), done]() mutable {
            service.reset();
            *done = true;
        });
        _retiring.push_back({std::move(thread), std::move(done)});
    }

    void ChatManager::AddFromService(ChatMessage message, size_t slot, int generation) {
        // Trace the first few messages with emotes, for checking emote parsing in a headset log.
        static std::atomic<int> emoteTraces{10};
        auto emote = std::find_if(message.segments.begin(), message.segments.end(), [](ChatSegment const& s) { return s.IsEmote(); });
        if (emote != message.segments.end() && emoteTraces.fetch_sub(1) > 0)
            INFO("Chat: message from {} has emote {} ({})", message.author, emote->emoteKey, emote->text);

        // A "!bsr" also goes to the request queue, after it is in the feed so
        // the reply shows below it, and outside our lock.
        std::optional<ChatMessage> command;
        if (message.event == ChatEvent::Message && !message.isNotice && message.platform != ChatPlatform::System && Requests::IsBsrCommand(message.text))
            command = message;
        bool moderation = message.event != ChatEvent::Message && message.event != ChatEvent::ChannelIdentity;
        std::unique_lock dispatch(_requestDispatchLock, std::defer_lock);
        if (command || moderation)
            dispatch.lock();
        {
            std::lock_guard lock(_lock);
            if (_generations[slot] != generation)
                return;
            if (message.event == ChatEvent::ChannelIdentity) {
                _channelIds[slot] = message.channelId;
                return;
            }
            if (moderation) {
                auto before = _messages.size();
                if (ApplyModeration(_messages, message)) {
                    _version++;
                    if (message.event == ChatEvent::RemoveMessage)
                        INFO("Chat moderation: marked deleted message on platform {}", slot);
                    else
                        INFO("Chat moderation: removed {} messages from platform {}", before - _messages.size(), slot);
                }
            } else {
                PushLocked(std::move(message));
            }
        }
        if (command)
            Requests::SongRequestManager::Instance().OnChatMessage(*command);
        else if (moderation)
            Requests::SongRequestManager::Instance().OnModeration(message);
    }

    void ChatManager::AddSystemMessage(std::string text, bool always) {
        if (!always && !GetConfig().showSystemMessages)
            return;
        ChatMessage message;
        message.platform = ChatPlatform::System;
        message.text = std::move(text);
        std::lock_guard lock(_lock);
        PushLocked(std::move(message));
    }

    void ChatManager::PushLocked(ChatMessage message) {
        message.sequence = ++_nextSequence;
        _messages.push_back(std::move(message));
        while (_messages.size() > MaxMessages)
            _messages.pop_front();
        _version++;
    }

    int ChatManager::Viewers(ChatPlatform platform) const {
        auto const& service = _services[SlotOf(platform)];
        return service ? service->Viewers() : -1;
    }

    int ChatManager::Version() const {
        std::lock_guard lock(_lock);
        return _version;
    }

    std::string ChatManager::ChannelId(ChatPlatform platform) const {
        std::lock_guard lock(_lock);
        return _channelIds[SlotOf(platform)];
    }

    std::vector<ChatMessage> ChatManager::Snapshot() const {
        std::lock_guard lock(_lock);
        return {_messages.begin(), _messages.end()};
    }
}
