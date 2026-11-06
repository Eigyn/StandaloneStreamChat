#include "UI/SettingsHost.hpp"
#include "UI/ChatPanel.hpp"
#include "UI/HexColor.hpp"
#include "Chat/ChatManager.hpp"
#include "Requests/SongRequestManager.hpp"
#include "Config.hpp"

#include "bsml/shared/BSML/MainThreadScheduler.hpp"

#include <algorithm>
#include <array>
#include <cctype>

DEFINE_TYPE(SimpleBSStreamChat, SettingsHost);

namespace {
    using namespace SimpleBSStreamChat;
    using Chat::ChatPlatform;

    std::array<int, 4> channelEditGenerations{};
    int saveGeneration = 0;

    std::string Trim(std::string value) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    // Sliders report every step while dragged; write the file once they settle.
    void ScheduleSave() {
        int current = ++saveGeneration;
        BSML::MainThreadScheduler::ScheduleAfterTime(0.5f, [current] {
            if (current == saveGeneration)
                SaveConfig();
        });
    }

    // Reconnects once the value settles, in case the field reports keystrokes.
    void SetChannel(ChatPlatform platform, std::string Config::*channel, StringW value) {
        auto trimmed = Trim(static_cast<std::string>(value));
        if (GetConfig().*channel == trimmed)
            return;
        GetConfig().*channel = std::move(trimmed);
        auto& generation = channelEditGenerations[static_cast<size_t>(platform)];
        int current = ++generation;
        BSML::MainThreadScheduler::ScheduleAfterTime(1.0f, [platform, &generation, current] {
            if (current != generation)
                return;
            SaveConfig();
            Chat::ChatManager::Instance().RestartPlatform(platform, true);
        });
    }

    void SetEnabled(ChatPlatform platform, bool Config::*enabled, bool value) {
        if (GetConfig().*enabled == value)
            return;
        GetConfig().*enabled = value;
        SaveConfig();
        Chat::ChatManager::Instance().RestartPlatform(platform, false);
    }

    template <typename T>
    void SetAppearance(T Config::*field, T value) {
        GetConfig().*field = value;
        ScheduleSave();
        ChatPanel::ApplyConfig();
    }
}

namespace SimpleBSStreamChat {
    StringW SettingsHost::get_twitchChannel() { return GetConfig().twitchChannel; }
    void SettingsHost::set_twitchChannel(StringW value) { SetChannel(ChatPlatform::Twitch, &Config::twitchChannel, value); }
    bool SettingsHost::get_enableTwitch() { return GetConfig().enableTwitch; }
    void SettingsHost::set_enableTwitch(bool value) { SetEnabled(ChatPlatform::Twitch, &Config::enableTwitch, value); }

    StringW SettingsHost::get_youTubeChannel() { return GetConfig().youTubeChannel; }
    void SettingsHost::set_youTubeChannel(StringW value) { SetChannel(ChatPlatform::YouTube, &Config::youTubeChannel, value); }
    bool SettingsHost::get_enableYouTube() { return GetConfig().enableYouTube; }
    void SettingsHost::set_enableYouTube(bool value) { SetEnabled(ChatPlatform::YouTube, &Config::enableYouTube, value); }

    StringW SettingsHost::get_kickChannel() { return GetConfig().kickChannel; }
    void SettingsHost::set_kickChannel(StringW value) { SetChannel(ChatPlatform::Kick, &Config::kickChannel, value); }
    bool SettingsHost::get_enableKick() { return GetConfig().enableKick; }
    void SettingsHost::set_enableKick(bool value) { SetEnabled(ChatPlatform::Kick, &Config::enableKick, value); }

    float SettingsHost::get_curveAmount() { return GetConfig().curveAmount; }
    void SettingsHost::set_curveAmount(float value) { SetAppearance(&Config::curveAmount, value); }
    // Unset (below 0) only until the panel first appears, which is before
    // the settings can be opened.
    float SettingsHost::get_cornerRadius() { return std::max(0.0f, GetConfig().cornerRadius); }
    void SettingsHost::set_cornerRadius(float value) { SetAppearance(&Config::cornerRadius, value); }
    float SettingsHost::get_panelWidth() { return GetConfig().panelWidth; }
    void SettingsHost::set_panelWidth(float value) { SetAppearance(&Config::panelWidth, value); }
    float SettingsHost::get_panelHeight() { return GetConfig().panelHeight; }
    void SettingsHost::set_panelHeight(float value) { SetAppearance(&Config::panelHeight, value); }
    UnityEngine::Color SettingsHost::get_backgroundColor() {
        return UI::ColorFromHex(GetConfig().backgroundColor, UnityEngine::Color(0.0f, 0.0f, 0.0f, 1.0f));
    }
    void SettingsHost::set_backgroundColor(UnityEngine::Color value) { SetAppearance(&Config::backgroundColor, UI::HexFromColor(value)); }
    float SettingsHost::get_fontSize() { return GetConfig().fontSize; }
    void SettingsHost::set_fontSize(float value) { SetAppearance(&Config::fontSize, value); }
    float SettingsHost::get_viewerCountSize() { return GetConfig().viewerCountSize; }
    void SettingsHost::set_viewerCountSize(float value) { SetAppearance(&Config::viewerCountSize, value); }
    float SettingsHost::get_backgroundOpacity() { return GetConfig().backgroundOpacity; }
    void SettingsHost::set_backgroundOpacity(float value) { SetAppearance(&Config::backgroundOpacity, value); }
    float SettingsHost::get_highlightOpacity() { return GetConfig().highlightOpacity; }
    void SettingsHost::set_highlightOpacity(float value) { SetAppearance(&Config::highlightOpacity, value); }

    bool SettingsHost::get_enableSongRequests() { return GetConfig().enableSongRequests; }
    void SettingsHost::set_enableSongRequests(bool value) {
        GetConfig().enableSongRequests = value;
        SaveConfig();
    }
    bool SettingsHost::get_allowDuplicates() { return GetConfig().allowDuplicates; }
    void SettingsHost::set_allowDuplicates(bool value) {
        GetConfig().allowDuplicates = value;
        SaveConfig();
    }
    bool SettingsHost::get_persistRequestQueue() { return GetConfig().persistRequestQueue; }
    void SettingsHost::set_persistRequestQueue(bool value) {
        GetConfig().persistRequestQueue = value;
        SaveConfig();
        Requests::SongRequestManager::Instance().PersistSettingChanged();
    }
    int SettingsHost::get_requestCooldown() { return GetConfig().perUserCooldownSeconds; }
    void SettingsHost::set_requestCooldown(int value) {
        GetConfig().perUserCooldownSeconds = value;
        ScheduleSave();
    }

    bool SettingsHost::get_showWindow() { return GetConfig().showPanel; }
    void SettingsHost::set_showWindow(bool value) { SetAppearance(&Config::showPanel, value); }
    bool SettingsHost::get_highlightMessages() { return GetConfig().highlightMessages; }
    void SettingsHost::set_highlightMessages(bool value) { SetAppearance(&Config::highlightMessages, value); }
    bool SettingsHost::get_emojiImages() { return GetConfig().emojiImages; }
    void SettingsHost::set_emojiImages(bool value) { SetAppearance(&Config::emojiImages, value); }
    bool SettingsHost::get_animatedEmotes() { return GetConfig().animatedEmotes; }
    void SettingsHost::set_animatedEmotes(bool value) { SetAppearance(&Config::animatedEmotes, value); }
    bool SettingsHost::get_platformIcons() { return GetConfig().showPlatformIcons; }
    void SettingsHost::set_platformIcons(bool value) { SetAppearance(&Config::showPlatformIcons, value); }
    bool SettingsHost::get_showViewerCounts() { return GetConfig().showViewerCounts; }
    void SettingsHost::set_showViewerCounts(bool value) { SetAppearance(&Config::showViewerCounts, value); }
    bool SettingsHost::get_showSystem() { return GetConfig().showSystemMessages; }
    void SettingsHost::set_showSystem(bool value) {
        GetConfig().showSystemMessages = value;
        SaveConfig();
    }

    void SettingsHost::recenterWindow() { ChatPanel::Recenter(); }
}
