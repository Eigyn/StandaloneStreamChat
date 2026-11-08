#pragma once

#include "Chat/ChatMessage.hpp"
#include <algorithm>
#include <deque>

namespace StandaloneStreamChat::Chat {
    // Called under ChatManager's history lock. Control events share the same
    // callback/order as messages, and never enter the rendered history.
    inline bool ApplyModeration(std::deque<ChatMessage>& messages, ChatMessage const& event) {
        auto channelMatches = [&](ChatMessage const& message) {
            if (message.platform != event.platform)
                return false;
            if (!event.channelId.empty() && !message.channelId.empty()) {
                if (event.channelId != message.channelId)
                    return false;
            } else if (event.channelName.empty() || event.channelName != message.channelName) {
                return false;
            }
            return true;
        };
        if (event.event == ChatEvent::RemoveMessage) {
            bool changed = false;
            for (auto& message : messages) {
                if (!channelMatches(message) || event.messageId.empty() || message.messageId != event.messageId || message.isDeleted) continue;
                message.text = "[message deleted]";
                message.segments.clear();
                message.bits = 0;
                message.isAction = message.isHighlighted = message.isMention = false;
                message.isDeleted = true;
                ++message.revision;
                changed = true;
            }
            return changed;
        }
        auto removed = std::erase_if(messages, [&](ChatMessage const& message) {
            if (!channelMatches(message)) return false;
            switch (event.event) {
                case ChatEvent::RemoveUser:
                    return !event.userId.empty() ? message.userId == event.userId
                                                : !event.authorLogin.empty() && message.authorLogin == event.authorLogin;
                case ChatEvent::ClearChannel: return true;
                default: return false;
            }
        });
        return removed != 0;
    }
}
