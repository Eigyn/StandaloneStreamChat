#include "UI/SettingsHost.hpp"
#include "UI/ChatPanel.hpp"
#include "UI/HexColor.hpp"
#include "UI/SettingsFlowCoordinator.hpp"
#include "Chat/ChatFilter.hpp"
#include "Chat/ChatManager.hpp"
#include "Requests/SongRequestManager.hpp"
#include "Config.hpp"

#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "bsml/shared/BSML/Components/Settings/ColorSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"

#include "UnityEngine/GameObject.hpp"

#include <algorithm>
#include <array>
#include <cctype>

DEFINE_TYPE(StandaloneStreamChat, SettingsHost);

namespace {
    using namespace StandaloneStreamChat;
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

    template <typename T, typename V>
    void SetAppearance(T Config::*field, V value) {
        GetConfig().*field = value;
        ScheduleSave();
        ChatPanel::ApplyConfig();
    }

    // Request limits; the request threads read them as they need them.
    template <typename T, typename V>
    void SetLimit(T Config::*field, V value) {
        GetConfig().*field = value;
        ScheduleSave();
    }

    std::vector<std::string>& FilterEntries(bool words) { return words ? GetConfig().filterWords : GetConfig().filterUsers; }
    bool& FilterAllows(bool words) { return words ? GetConfig().filterWordsAllow : GetConfig().filterUsersAllow; }

    // Also takes what's already in the feed out if it's now filtered.
    void FiltersChanged() {
        ApplyFilterConfig();
        Chat::ChatManager::Instance().ApplyFilters();
        SaveConfig();
    }
}

namespace StandaloneStreamChat {
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
    // Below 0 (set in the file) only until the panel first appears, which is
    // before the settings can be opened.
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

    // The values a fresh config starts with, for the Appearance tab's settings only.
    void SettingsHost::resetAppearance() {
        Config const defaults{};
        auto& config = GetConfig();
        config.curveAmount = defaults.curveAmount;
        config.cornerRadius = defaults.cornerRadius;
        config.panelWidth = defaults.panelWidth;
        config.panelHeight = defaults.panelHeight;
        config.backgroundColor = defaults.backgroundColor;
        config.fontSize = defaults.fontSize;
        config.viewerCountSize = defaults.viewerCountSize;
        config.backgroundOpacity = defaults.backgroundOpacity;
        config.highlightOpacity = defaults.highlightOpacity;
        SaveConfig();
        ChatPanel::ApplyConfig();
        // Each control shows its own copy of its value; have them all read the config again.
        for (auto slider : get_gameObject()->GetComponentsInChildren<BSML::SliderSetting*>(true))
            slider->ReceiveValue();
        for (auto color : get_gameObject()->GetComponentsInChildren<BSML::ColorSetting*>(true))
            color->ReceiveValue();
    }

    void SettingsHost::openPanel() { SettingsFlowCoordinator::OpenPage(false); }
    void SettingsHost::openPositions() { SettingsFlowCoordinator::OpenPage(true); }

    bool SettingsHost::get_separatePlayingPosition() { return GetConfig().separatePlayingPosition; }
    void SettingsHost::set_separatePlayingPosition(bool value) {
        GetConfig().separatePlayingPosition = value;
        SaveConfig();
    }
    void SettingsHost::captureMenuPosition() { ChatPanel::CapturePosition(false); }
    void SettingsHost::capturePlayingPosition() {
        ChatPanel::CapturePosition(true);
        // That turned Separate Playing Position on.
        for (auto toggle : get_gameObject()->GetComponentsInChildren<BSML::ToggleSetting*>(true))
            toggle->ReceiveValue();
    }

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
    void SettingsHost::set_requestCooldown(int value) { SetLimit(&Config::perUserCooldownSeconds, value); }
    int SettingsHost::get_maxQueueSize() { return GetConfig().maxQueueSize; }
    void SettingsHost::set_maxQueueSize(int value) { SetLimit(&Config::maxQueueSize, value); }
    int SettingsHost::get_maxPerViewer() { return GetConfig().maxRequestsPerUser; }
    void SettingsHost::set_maxPerViewer(int value) { SetLimit(&Config::maxRequestsPerUser, value); }
    int SettingsHost::get_minRating() { return GetConfig().minRatingPercent; }
    void SettingsHost::set_minRating(int value) { SetLimit(&Config::minRatingPercent, value); }
    int SettingsHost::get_maxLength() { return GetConfig().maxLengthMinutes; }
    void SettingsHost::set_maxLength(int value) { SetLimit(&Config::maxLengthMinutes, value); }
    float SettingsHost::get_maxNps() { return GetConfig().maxNps; }
    void SettingsHost::set_maxNps(float value) { SetLimit(&Config::maxNps, value); }

    // The list setting finds the current choice among its options, so hand back the option object itself.
    StringW SettingsHost::get_whoCanRequest() {
        auto who = static_cast<size_t>(std::clamp(GetConfig().whoCanRequest.Get(), 0, static_cast<int>(WhoCanRequestNames.size()) - 1));
        if (whoCanRequestOptions && who < whoCanRequestOptions.size())
            return StringW(reinterpret_cast<Il2CppString*>(whoCanRequestOptions[who]));
        return StringW(WhoCanRequestNames[who]);
    }
    void SettingsHost::set_whoCanRequest(StringW value) {
        auto found = std::find(WhoCanRequestNames.begin(), WhoCanRequestNames.end(), static_cast<std::string>(value));
        if (found == WhoCanRequestNames.end())
            return;
        GetConfig().whoCanRequest = static_cast<int>(found - WhoCanRequestNames.begin());
        SaveConfig();
    }

    bool SettingsHost::get_hideCommands() { return GetConfig().hideCommands; }
    void SettingsHost::set_hideCommands(bool value) {
        GetConfig().hideCommands = value;
        FiltersChanged();
    }

    void SettingsHost::SetUpFilters() {
        if (!userFilterList || !wordFilterList)
            return;
        if (filterKeyboard) {
            UnityW<SettingsHost> self = this;
            filterKeyboard->onEnter = [self](StringW text) mutable {
                if (self)
                    self->AddFilterEntry(text);
            };
        }
        RefreshFilterWindow(false);
        RefreshFilterWindow(true);
    }

    void SettingsHost::RefreshFilterWindow(bool words) {
        auto list = words ? wordFilterList : userFilterList;
        if (!list || !list->tableView)
            return;
        auto data = list->data;
        data.clear();
        for (auto const& entry : FilterEntries(words))
            data.push_back(BSML::CustomCellInfo::construct(StringW(entry)));
        list->tableView->ReloadData();
        list->tableView->ClearSelection();
        (words ? _selectedWord : _selectedUser) = -1;
        if (auto button = words ? wordModeButton : userModeButton) {
            if (auto label = button->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                label->set_text(FilterAllows(words) ? "Allowlist" : "Blocklist");
        }
    }

    void SettingsHost::userSelected(HMUI::TableView*, int index) { _selectedUser = index; }
    void SettingsHost::wordSelected(HMUI::TableView*, int index) { _selectedWord = index; }

    void SettingsHost::addUser() { OpenKeyboard(false); }
    void SettingsHost::addWord() { OpenKeyboard(true); }
    void SettingsHost::OpenKeyboard(bool words) {
        _filterWords = words;
        if (filterKeyboard && filterKeyboard->modalView)
            filterKeyboard->modalView->Show();
    }

    // One entry per Add, commas and all.
    void SettingsHost::AddFilterEntry(StringW text) {
        auto entry = Chat::NormalizeFilterEntry(static_cast<std::string>(text));
        auto& entries = FilterEntries(_filterWords);
        if (entry.empty() || std::find(entries.begin(), entries.end(), entry) != entries.end())
            return;
        entries.push_back(std::move(entry));
        FiltersChanged();
        RefreshFilterWindow(_filterWords);
    }

    void SettingsHost::toggleUserMode() { ToggleFilterMode(false); }
    void SettingsHost::toggleWordMode() { ToggleFilterMode(true); }
    void SettingsHost::ToggleFilterMode(bool words) {
        FilterAllows(words) = !FilterAllows(words);
        FiltersChanged();
        RefreshFilterWindow(words);
    }

    void SettingsHost::removeUser() { OpenRemove(false); }
    void SettingsHost::removeWord() { OpenRemove(true); }
    void SettingsHost::OpenRemove(bool words) {
        if (!removeModal)
            return;
        _filterWords = words;
        auto const& entries = FilterEntries(words);
        int selected = words ? _selectedWord : _selectedUser;
        bool picked = selected >= 0 && static_cast<size_t>(selected) < entries.size();
        if (removeText)
            removeText->set_text(picked ? "Remove \"" + entries[selected] + "\"?"
                                 : entries.empty() ? std::string("This list is empty.")
                                                   : std::string("Pick an entry in the list first, or remove them all."));
        if (removeSelectedButton)
            removeSelectedButton->set_interactable(picked);
        removeModal->Show();
    }

    void SettingsHost::removeSelected() {
        auto& entries = FilterEntries(_filterWords);
        int selected = _filterWords ? _selectedWord : _selectedUser;
        if (selected >= 0 && static_cast<size_t>(selected) < entries.size()) {
            entries.erase(entries.begin() + selected);
            FiltersChanged();
            RefreshFilterWindow(_filterWords);
        }
        removeModal->Hide();
    }

    void SettingsHost::removeAll() {
        FilterEntries(_filterWords).clear();
        FiltersChanged();
        RefreshFilterWindow(_filterWords);
        removeModal->Hide();
    }

    void SettingsHost::cancelRemove() { removeModal->Hide(); }

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
