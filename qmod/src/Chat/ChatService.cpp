#include "Chat/ChatService.hpp"
#include "Config.hpp"

#include <algorithm>

namespace SimpleBSStreamChat::Chat {
    ChatService::ChatService(MessageHandler onMessage, StatusHandler onStatus)
        : _onMessage(std::move(onMessage)), _onStatus(std::move(onStatus)) {}

    ChatService::~ChatService() { Stop(); }

    void ChatService::Start() {
        if (_thread.joinable())
            return;
        _stopping = false;
        _thread = std::thread(&ChatService::Run, this);
    }

    void ChatService::RequestStop() { _stopping = true; }

    void ChatService::Stop() {
        _stopping = true;
        if (_thread.joinable())
            _thread.join();
    }

    bool ChatService::ViewerPollDue() {
        if (!GetConfig().showViewerCounts) {
            _viewers = -1;
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        if (now < _nextViewerPoll)
            return false;
        _nextViewerPoll = now + std::chrono::seconds(30);
        return true;
    }

    bool ChatService::SleepFor(std::chrono::milliseconds duration) const {
        constexpr std::chrono::milliseconds slice(250);
        while (duration.count() > 0 && !_stopping) {
            auto step = std::min(duration, slice);
            std::this_thread::sleep_for(step);
            duration -= step;
        }
        return !_stopping;
    }
}
