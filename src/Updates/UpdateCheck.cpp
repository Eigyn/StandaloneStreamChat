#include "Updates/UpdateCheck.hpp"
#include "Chat/ChatManager.hpp"
#include "Net/HttpClient.hpp"
#include "logging.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <thread>

namespace StandaloneStreamChat::Updates {
    namespace {
        using namespace std::chrono_literals;

        // Waits after the menu first shows, so the notice lands after the
        // connection lines; then retries a failed request twice.
        constexpr std::chrono::seconds StartDelay = 5s;
        constexpr std::chrono::seconds RetryDelays[] = {60s, 300s};

        class Checker {
           public:
            static Checker& Instance() {
                static Checker checker;
                return checker;
            }

            ~Checker() {
                _stopping = true;
                if (_thread.joinable())
                    _thread.join();
            }

            void Start() {
                if (!_thread.joinable())
                    _thread = std::thread([this] { Run(); });
            }

           private:
            Checker() = default;

            bool Sleep(std::chrono::seconds duration) const {
                for (auto end = std::chrono::steady_clock::now() + duration; std::chrono::steady_clock::now() < end;) {
                    if (_stopping)
                        return false;
                    std::this_thread::sleep_for(100ms);
                }
                return !_stopping;
            }

            void Run() {
                auto current = ParseVersion(VERSION);
                if (!current) {
                    WARNING("Update check: can't read this build's version {}", VERSION);
                    return;
                }
                if (!Sleep(StartDelay))
                    return;
                auto url = "https://api.github.com/repos/" + std::string(Repository) + "/releases/latest";
                for (size_t attempt = 0;; attempt++) {
                    try {
                        Net::HttpClient http(_stopping);
                        auto response = http.Get(url, {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"});
                        if (response.status == 404) {
                            INFO("Update check: no release published yet");
                            return;
                        }
                        if (response.Ok()) {
                            Report(*current, response.body);
                            return;
                        }
                        // 403 and 429 are GitHub's rate limit, which lifts within the hour.
                        WARNING("Update check: GitHub answered HTTP {}", response.status);
                    } catch (std::exception const& ex) {
                        if (_stopping)
                            return;
                        WARNING("Update check failed: {}", ex.what());
                    }
                    if (attempt >= std::size(RetryDelays) || !Sleep(RetryDelays[attempt]))
                        return;
                }
            }

            void Report(Version const& current, std::string const& reply) {
                auto tag = ReleaseTag(reply);
                auto latest = tag ? ParseVersion(*tag) : std::nullopt;
                if (!latest) {
                    WARNING("Update check: no usable release in GitHub's reply");
                    return;
                }
                if (!IsNewer(*latest, current)) {
                    INFO("Update check: {} is the latest release; this is {}", *tag, VERSION);
                    return;
                }
                INFO("Update check: {} is available; this is {}", *tag, VERSION);
                // Shown even with Show System Messages off: it isn't connection noise. Green to stand out without reading as an error.
                Chat::ChatManager::Instance().AddSystemMessage(NoticeText(*tag), true, "#7FE08A");
            }

            std::atomic<bool> _stopping{false};
            std::thread _thread;
        };
    }

    void CheckForUpdate() { Checker::Instance().Start(); }
}
