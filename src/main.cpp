#include "logging.hpp"
#include "Config.hpp"
#include "Chat/ChatManager.hpp"
#include "Requests/SongRequestManager.hpp"
#include "UI/ChatPanel.hpp"
#include "UI/Settings.hpp"

#include "scotland2/shared/loader.hpp"
#ifdef SSC_BS_1_40
#include "beatsaber-hook/shared/utils/hooking.hpp"
#else
#include "beatsaber-hook/shared/hooking.hpp"
#endif
#include "custom-types/shared/register.hpp"
#include "bsml/shared/BSML.hpp"

#include "GlobalNamespace/MainMenuViewController.hpp"

#include <algorithm>

#define MOD_EXPORT_FUNC extern "C" __attribute__((visibility("default")))

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace {
    struct StopWorkers {
        StandaloneStreamChat::Chat::ChatManager& chat;
        StandaloneStreamChat::Requests::SongRequestManager& requests;
        ~StopWorkers() {
            chat.Stop();
            requests.Stop();
        }
    };

    // 0.10.0 changed the mod ID, so MBF installs it beside an older
    // SimpleBSStreamChat instead of upgrading it. Both would connect and
    // draw a panel, so say which one to remove.
    void WarnIfOldVersionLoaded() {
        static bool checked = false;
        if (checked)
            return;
        checked = true;
        auto loaded = modloader::get_loaded();
        if (std::none_of(loaded.begin(), loaded.end(), [](modloader::ModData const& mod) { return mod.info.id == "SimpleBSStreamChat"; }))
            return;
        WARNING("The old SimpleBSStreamChat mod is loaded alongside this one");
        StandaloneStreamChat::Chat::ChatManager::Instance().AddSystemMessage(
            "The old SimpleBSStreamChat mod is still installed. Remove it in ModsBeforeFriday so chat doesn't run twice.", true);
    }
}

// The floating screen needs BSML's menu pieces, which exist once the main menu is up.
MAKE_HOOK_MATCH(MainMenuViewController_DidActivate, &GlobalNamespace::MainMenuViewController::DidActivate, void,
    GlobalNamespace::MainMenuViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
    MainMenuViewController_DidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);
    if (firstActivation) {
        StandaloneStreamChat::ChatPanel::EnsureCreated();
        WarnIfOldVersionLoaded();
    }
    StandaloneStreamChat::Requests::SongRequestManager::Instance().MenuActivated();
}

// Called very early by scotland2, before il2cpp is usable.
MOD_EXPORT_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
    Paper::Logger::RegisterFileContextId(MOD_ID);
    INFO("Setup complete, version {}", VERSION);
}

// Called on the main thread once il2cpp and the libraries we depend on are loaded.
MOD_EXPORT_FUNC void late_load() noexcept {
    StandaloneStreamChat::LoadConfig();

    custom_types::Register::AutoRegister();
    BSML::Init();
    BSML::Register::RegisterMainMenu("StandaloneStreamChat", "StandaloneStreamChat", "Twitch + YouTube + Kick stream chat settings",
        &StandaloneStreamChat::UI::SettingsDidActivate);

    // A named logger: beatsaber-hook 6 (1.40.8) takes it by non-const reference.
    static auto hookLogger = Paper::ConstLoggerContext(MOD_ID);
    INSTALL_HOOK(hookLogger, MainMenuViewController_DidActivate);

    // Connect right away, so chat is live by the time the menu shows.
    auto& requests = StandaloneStreamChat::Requests::SongRequestManager::Instance();
    auto& chat = StandaloneStreamChat::Chat::ChatManager::Instance();
    static StopWorkers stopWorkers{chat, requests};
    BSML::Events::onGameDidRestart += [] { StandaloneStreamChat::Requests::SongRequestManager::Instance().MenuRestarting(); };
    requests.Start();
    chat.Start();
    INFO("Late load complete");
}
