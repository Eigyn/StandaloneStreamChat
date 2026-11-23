#include "UI/SettingsFlowCoordinator.hpp"
#include "UI/ChatPanel.hpp"
#include "UI/Settings.hpp"
#include "UI/SettingsBsml.hpp"

#include "bsml/shared/Helpers/creation.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"

#include "UnityEngine/GameObject.hpp"

DEFINE_TYPE(StandaloneStreamChat, SettingsViewController);
DEFINE_TYPE(StandaloneStreamChat, PanelViewController);
DEFINE_TYPE(StandaloneStreamChat, PositionsViewController);
DEFINE_TYPE(StandaloneStreamChat, SettingsFlowCoordinator);

namespace {
    UnityW<StandaloneStreamChat::SettingsFlowCoordinator> current;
    constexpr auto Title = "StandaloneStreamChat";
}

namespace StandaloneStreamChat {
    void SettingsViewController::DidActivate(bool firstActivation, bool, bool) {
        if (firstActivation)
            UI::BuildSettingsView(this, UI::SettingsBsml);
    }

    void PanelViewController::DidActivate(bool firstActivation, bool, bool) {
        if (firstActivation)
            UI::BuildSettingsView(this, UI::PanelBsml);
    }

    void PositionsViewController::DidActivate(bool firstActivation, bool, bool) {
        if (firstActivation) {
            UI::BuildSettingsView(this, UI::PositionsBsml);
        } else {
            // A drag in the pause menu may have turned Separate Playing Position on since.
            for (auto toggle : GetComponentsInChildren<BSML::ToggleSetting*>(true))
                toggle->ReceiveValue();
        }
        ChatPanel::SetPositioning(true);
    }

    void PositionsViewController::DidDeactivate(bool, bool) { ChatPanel::SetPositioning(false); }

    void SettingsFlowCoordinator::DidActivate(bool firstActivation, bool, bool) {
        current = this;
        if (!firstActivation)
            return;
        SetTitle(Title, HMUI::ViewController_AnimationType::None);
        set_showBackButton(true);
        _settingsView = BSML::Helpers::CreateViewController<SettingsViewController*>();
        _panelView = BSML::Helpers::CreateViewController<PanelViewController*>();
        _positionsView = BSML::Helpers::CreateViewController<PositionsViewController*>();
        ProvideInitialViewControllers(_settingsView, nullptr, nullptr, nullptr, nullptr);
    }

    void SettingsFlowCoordinator::BackButtonWasPressed(HMUI::ViewController* topViewController) {
        if (topViewController != _settingsView) {
            SetTitle(Title, HMUI::ViewController_AnimationType::In);
            DismissViewController(topViewController, HMUI::ViewController_AnimationDirection::Horizontal, nullptr, false);
        } else if (auto parent = _parentFlowCoordinator) {
            parent->DismissFlowCoordinator(this, HMUI::ViewController_AnimationDirection::Horizontal, nullptr, false);
        }
    }

    void SettingsFlowCoordinator::OpenPage(bool positions) {
        if (!current || current->get_topViewController().ptr() != current->_settingsView)
            return;
        current->SetTitle(positions ? "Positions" : "Panel", HMUI::ViewController_AnimationType::In);
        current->PresentViewController(positions ? current->_positionsView : current->_panelView, nullptr,
                                       HMUI::ViewController_AnimationDirection::Horizontal, false);
    }
}
