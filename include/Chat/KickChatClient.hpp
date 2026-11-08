#pragma once

#include "Chat/ChatService.hpp"
#include "Chat/Json.hpp"
#include "Chat/RecentIds.hpp"

#include <optional>
#include <string>

namespace StandaloneStreamChat::Net {
    class HttpClient;
}

namespace StandaloneStreamChat::Chat {
    /// Read-only Kick chat. Kick's official API needs an app login, so this
    /// follows the web client: look the channel up, get an anonymous guest
    /// token, then read the chatroom over Kick's Centrifugo WebSocket.
    /// (Kick moved chat off Pusher; the old Pusher keys no longer connect.)
    class KickChatClient : public ChatService {
       public:
        KickChatClient(std::string_view channel, MessageHandler onMessage, StatusHandler onStatus);
        ~KickChatClient() override;

       private:
        struct ChannelInfo {
            std::string channelId;  // the realtime lookup also accepts a chatroom id
            std::string chatroomId;
            std::string displayName;
        };

        void Run() override;
        std::optional<ChannelInfo> Resolve(Net::HttpClient& http);
        void ReadChat(Net::HttpClient& http, ChannelInfo const& info);
        std::string RealtimeUrl(Net::HttpClient& http, ChannelInfo const& info, std::string const& clientId);
        std::string GuestToken(Net::HttpClient& http, std::string const& clientId);
        void HandleChatMessage(Json::Value const* payload);
        void HandleControl(std::string_view event, Json::Value const* payload);
        std::optional<int> FetchViewers(Net::HttpClient& http) override;

        std::string _channel;  // slug, or a numeric chatroom id
        std::string _chatroomId;
        RecentIds _seen{300};
        int _messageCount = 0;
    };
}
