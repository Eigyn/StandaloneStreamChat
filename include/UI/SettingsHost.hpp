#pragma once

#include "custom-types/shared/macros.hpp"
#include "bsml/shared/macros.hpp"

#include "bsml/shared/BSML/Components/CustomListTableData.hpp"
#include "bsml/shared/BSML/Components/Keyboard/ModalKeyboard.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"

#include "HMUI/TableView.hpp"
#include "System/Collections/Generic/List_1.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/MonoBehaviour.hpp"

#include <array>
#include <string_view>

// Binding target for SettingsBsml. Each property reads and writes the config
// directly (the generated backing fields stay unused) and applies the change
// right away. Lives on the settings view controller's GameObject.
DECLARE_CLASS_CODEGEN(StandaloneStreamChat, SettingsHost, UnityEngine::MonoBehaviour) {
    // Connections
    DECLARE_BSML_PROPERTY(StringW, twitchChannel);
    DECLARE_BSML_PROPERTY(bool, enableTwitch);
    DECLARE_BSML_PROPERTY(StringW, youTubeChannel);
    DECLARE_BSML_PROPERTY(bool, enableYouTube);
    DECLARE_BSML_PROPERTY(StringW, kickChannel);
    DECLARE_BSML_PROPERTY(bool, enableKick);

    // Appearance
    DECLARE_BSML_PROPERTY(float, curveAmount);
    DECLARE_BSML_PROPERTY(float, cornerRadius);
    DECLARE_BSML_PROPERTY(float, panelWidth);
    DECLARE_BSML_PROPERTY(float, panelHeight);
    DECLARE_BSML_PROPERTY(UnityEngine::Color, backgroundColor);
    DECLARE_BSML_PROPERTY(float, fontSize);
    DECLARE_BSML_PROPERTY(float, viewerCountSize);
    DECLARE_BSML_PROPERTY(float, backgroundOpacity);
    DECLARE_BSML_PROPERTY(float, highlightOpacity);
    /// Every Panel page value back to its default, controls included.
    DECLARE_INSTANCE_METHOD(void, resetAppearance);
    DECLARE_INSTANCE_METHOD(void, openPanel);
    DECLARE_INSTANCE_METHOD(void, openPositions);

    // Positions
    DECLARE_BSML_PROPERTY(bool, separatePlayingPosition);
    DECLARE_INSTANCE_METHOD(void, captureMenuPosition);
    DECLARE_INSTANCE_METHOD(void, capturePlayingPosition);
    DECLARE_INSTANCE_METHOD(void, clearPositions);

    // Requests
    DECLARE_BSML_PROPERTY(bool, enableSongRequests);
    // The Who Can Request choices, in Requests::WhoCanRequest order. Filled before parsing.
    DECLARE_INSTANCE_FIELD(ListW<System::Object*>, whoCanRequestOptions);
    DECLARE_BSML_PROPERTY(StringW, whoCanRequest);
    DECLARE_BSML_PROPERTY(bool, allowDuplicates);
    DECLARE_BSML_PROPERTY(bool, persistRequestQueue);
    DECLARE_BSML_PROPERTY(int, maxQueueSize);
    // Sliders; all integer-only but Max NPS.
    DECLARE_BSML_PROPERTY(int, requestCooldown);
    DECLARE_BSML_PROPERTY(int, maxPerViewer);
    DECLARE_BSML_PROPERTY(int, minRating);
    DECLARE_BSML_PROPERTY(int, maxLength);
    DECLARE_BSML_PROPERTY(float, maxNps);

    // Filters: a list window and a button bar for users and for words. One
    // keyboard and one Remove dialog serve both; _filterWords says which.
    DECLARE_INSTANCE_FIELD(BSML::CustomListTableData*, userFilterList);
    DECLARE_INSTANCE_FIELD(BSML::CustomListTableData*, wordFilterList);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::Button*, userModeButton);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::Button*, wordModeButton);
    DECLARE_INSTANCE_FIELD(BSML::ModalKeyboard*, filterKeyboard);
    DECLARE_INSTANCE_FIELD(BSML::ModalView*, removeModal);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, removeText);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::Button*, removeSelectedButton);
    DECLARE_INSTANCE_FIELD(bool, _filterWords);
    DECLARE_INSTANCE_FIELD(int, _selectedUser);  // -1 = none
    DECLARE_INSTANCE_FIELD(int, _selectedWord);
    DECLARE_INSTANCE_METHOD(void, userSelected, HMUI::TableView* table, int index);
    DECLARE_INSTANCE_METHOD(void, wordSelected, HMUI::TableView* table, int index);
    DECLARE_INSTANCE_METHOD(void, addUser);
    DECLARE_INSTANCE_METHOD(void, addWord);
    DECLARE_INSTANCE_METHOD(void, toggleUserMode);
    DECLARE_INSTANCE_METHOD(void, toggleWordMode);
    DECLARE_INSTANCE_METHOD(void, removeUser);
    DECLARE_INSTANCE_METHOD(void, removeWord);
    DECLARE_INSTANCE_METHOD(void, removeSelected);
    DECLARE_INSTANCE_METHOD(void, removeAll);
    DECLARE_INSTANCE_METHOD(void, cancelRemove);
    DECLARE_BSML_PROPERTY(bool, hideCommands);

    // General
    DECLARE_BSML_PROPERTY(bool, showWindow);
    DECLARE_BSML_PROPERTY(bool, highlightMessages);
    DECLARE_BSML_PROPERTY(bool, platformIcons);
    DECLARE_BSML_PROPERTY(bool, emojiImages);
    DECLARE_BSML_PROPERTY(bool, animatedEmotes);
    DECLARE_BSML_PROPERTY(bool, showViewerCounts);
    DECLARE_BSML_PROPERTY(bool, showSystem);
    DECLARE_INSTANCE_METHOD(void, recenterWindow);

   public:
    static constexpr std::array<std::string_view, 3> WhoCanRequestNames{"Everyone", "Mods", "Broadcaster"};
    /// After parsing: fills the filter windows. Nothing on views without them.
    void SetUpFilters();

   private:
    void RefreshFilterWindow(bool words);
    void OpenKeyboard(bool words);
    void AddFilterEntry(StringW text);
    void ToggleFilterMode(bool words);
    void OpenRemove(bool words);
};
