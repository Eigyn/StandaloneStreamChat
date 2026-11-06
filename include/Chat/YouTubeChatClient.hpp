#pragma once

#include "Chat/ChatService.hpp"
#include "Chat/RecentIds.hpp"

#include <memory>
#include <string>
#include <vector>

namespace SimpleBSStreamChat::Net {
    class HttpClient;
}

namespace SimpleBSStreamChat::Chat {
    /// YouTube live chat through the public InnerTube endpoint the browser's
    /// popout chat uses: no API key from the user, no login, no quota.
    /// Port of the PC InnerTubeChatSession + YouTubeLiveChatClient.
    class YouTubeChatClient : public ChatService {
       public:
        /// `seen` outlives this client so a restart doesn't replay the backlog
        /// YouTube puts in a fresh session's first poll.
        YouTubeChatClient(std::string_view channel, std::shared_ptr<RecentIds> seen, MessageHandler onMessage, StatusHandler onStatus);
        ~YouTubeChatClient() override;

       private:
        struct PollResult {
            bool valid = false;  // false: chat ended or the format changed, so re-bootstrap
            int timeoutMs = 2000;
            std::vector<ChatMessage> messages;
        };

        void Run() override;
        /// The channel's current live video id, or empty if it isn't live.
        std::string ResolveLiveVideoId(Net::HttpClient& http);
        /// Loads the popout chat page and extracts what polling needs.
        bool Bootstrap(Net::HttpClient& http, std::string const& videoId);
        PollResult Poll(Net::HttpClient& http);
        void UpdateViewers(Net::HttpClient& http, std::string const& videoId);

        std::string _channel;  // @handle, UC channel id, video id, or URL
        std::shared_ptr<RecentIds> _seen;
        std::string _apiKey;
        std::string _clientVersion;
        std::string _continuation;
        int _messageCount = 0;
    };
}
