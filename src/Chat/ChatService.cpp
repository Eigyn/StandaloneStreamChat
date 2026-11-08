#include "Chat/ChatService.hpp"
#include "Net/HttpClient.hpp"
#include "Config.hpp"
#include "logging.hpp"

#include <algorithm>
#include <exception>
#include <memory>

namespace SimpleBSStreamChat::Chat {
    ChatService::ChatService(MessageHandler onMessage, StatusHandler onStatus)
        : _onMessage(std::move(onMessage)), _onStatus(std::move(onStatus)) {}

    ChatService::~ChatService() { Stop(); }

    void ChatService::Start() {
        if (_thread.joinable())
            return;
        _stopping = false;
        _thread = std::thread(&ChatService::Run, this);
        _viewerThread = std::thread(&ChatService::RunViewers, this);
    }

    void ChatService::RequestStop() { _stopping = true; }

    void ChatService::Stop() {
        _stopping = true;
        if (_thread.joinable())
            _thread.join();
        if (_viewerThread.joinable())
            _viewerThread.join();
    }

    void ChatService::RunViewers() {
        using namespace std::chrono_literals;
        // Its own connection: HttpClient isn't thread-safe, and the chat
        // thread's client is busy with chat.
        std::unique_ptr<Net::HttpClient> http;
        while (!_stopping) {
            if (!GetConfig().showViewerCounts) {
                _viewers = -1;
                SleepFor(1s);
                continue;
            }
            try {
                if (!http)
                    http = std::make_unique<Net::HttpClient>(_stopping);
                if (auto count = FetchViewers(*http))
                    _viewers = *count;
            } catch (std::exception const& ex) {
                if (_stopping)
                    break;
                DEBUG("Viewer count failed: {}", ex.what());
            }
            // Wait 30 s, but notice the toggle (or a stop) within a second.
            for (int i = 0; i < 30 && !_stopping && GetConfig().showViewerCounts; i++)
                SleepFor(1s);
        }
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
