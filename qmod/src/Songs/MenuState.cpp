#include "Songs/MenuState.hpp"

#include "bsml/shared/Helpers/getters.hpp"

#include "GlobalNamespace/LevelSelectionNavigationController.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/MainMenuViewController.hpp"
#include "GlobalNamespace/PartyFreePlayFlowCoordinator.hpp"
#include "GlobalNamespace/ResultsViewController.hpp"
#include "GlobalNamespace/SoloFreePlayFlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"

namespace SimpleBSStreamChat::Songs::MenuState {
    namespace {
        // The menu's flow coordinator when the menu is live and settled.
        GlobalNamespace::MainFlowCoordinator* SettledMenu() {
            // GameCore is only loaded while a song (or multiplayer match) plays.
            if (UnityEngine::SceneManagement::SceneManager::GetSceneByName("GameCore").get_isLoaded())
                return nullptr;
            auto main = BSML::Helpers::GetMainFlowCoordinator();
            if (!main || main->get_isInTransition() || !main->get_gameObject()->get_activeInHierarchy())
                return nullptr;
            return main;
        }

        bool Shown(UnityW<GlobalNamespace::ResultsViewController> results) { return results && results->get_isActivated(); }
    }

    bool InMenu() { return SettledMenu() != nullptr; }

    bool ResultsShowing() {
        auto main = BSML::Helpers::GetMainFlowCoordinator();
        if (!main)
            return false;
        auto solo = main->_soloFreePlayFlowCoordinator;
        auto party = main->_partyFreePlayFlowCoordinator;
        return (solo && Shown(solo->_resultsViewController)) || (party && Shown(party->_resultsViewController));
    }

    bool CanAccept() { return CanOpenSong(); }

    bool AtMainMenuRoot() {
        auto main = SettledMenu();
        if (!main || main->get_childFlowCoordinator())
            return false;
        // Not under a dialog or the options screen.
        auto top = main->get_topViewController();
        return top && top.ptr() == static_cast<HMUI::ViewController*>(main->_mainMenuViewController.ptr());
    }

    bool SongListShowing() {
        auto main = SettledMenu();
        if (!main)
            return false;
        auto child = main->get_childFlowCoordinator();
        auto solo = main->_soloFreePlayFlowCoordinator;
        if (!child || !solo || child.ptr() != static_cast<HMUI::FlowCoordinator*>(solo.ptr()) || solo->get_isInTransition())
            return false;
        // The results, practice and search screens are pushed over the list.
        auto top = solo->get_topViewController();
        return top && top.ptr() == static_cast<HMUI::ViewController*>(solo->levelSelectionNavigationController.ptr());
    }

    bool CanOpenSong() { return AtMainMenuRoot() || SongListShowing(); }
}
