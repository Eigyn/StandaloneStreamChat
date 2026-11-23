#pragma once

#include "custom-types/shared/macros.hpp"

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"

// The settings screen from the main menu button: the tabbed settings, and the
// Panel and Positions pages that the Appearance tab's buttons open over them.
// The menu's Back button closes an open page first.

// The tabbed settings (SettingsBsml).
DECLARE_CLASS_CODEGEN(StandaloneStreamChat, SettingsViewController, HMUI::ViewController) {
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
};

// The panel's look (PanelBsml).
DECLARE_CLASS_CODEGEN(StandaloneStreamChat, PanelViewController, HMUI::ViewController) {
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
};

// Capturing the menu and song positions (PositionsBsml). While it's open, dragging the panel doesn't save.
DECLARE_CLASS_CODEGEN(StandaloneStreamChat, PositionsViewController, HMUI::ViewController) {
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidDeactivate, &HMUI::ViewController::DidDeactivate, bool removedFromHierarchy, bool screenSystemDisabling);
};

DECLARE_CLASS_CODEGEN(StandaloneStreamChat, SettingsFlowCoordinator, HMUI::FlowCoordinator) {
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, _settingsView);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, _panelView);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, _positionsView);
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::FlowCoordinator::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, BackButtonWasPressed, &HMUI::FlowCoordinator::BackButtonWasPressed, HMUI::ViewController* topViewController);

   public:
    /// Opens the Panel page, or the Positions page, over the tabs.
    static void OpenPage(bool positions);
};
