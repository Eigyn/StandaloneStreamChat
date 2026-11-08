#pragma once

#include "Chat/ChatService.hpp"
#include "Chat/IrcMessage.hpp"

#include <memory>
#include <string>

namespace SimpleBSStreamChat::Net {
    class HttpClient;
}

namespace SimpleBSStreamChat::Chat {
    /// Anonymous read-only Twitch chat over IRC/TLS using the documented
    /// "justinfan" login: no OAuth, no client id.
    class TwitchIrcClient : public ChatService {
       public:
        TwitchIrcClient(std::string_view channel, MessageHandler onMessage, StatusHandler onStatus);
        ~TwitchIrcClient() override;

       private:
        void Run() override;
        void ConnectAndRead();
        void HandlePrivmsg(IrcMessage const& message);
        void HandleControl(IrcMessage const& message);
        std::optional<int> FetchViewers(Net::HttpClient& http) override;

        std::string _channel;  // lowercase, without '#'
        std::string _channelId;
        int _messageCount = 0;
    };
}
