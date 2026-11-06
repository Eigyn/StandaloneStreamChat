#include "logging.hpp"
#include "Config.hpp"
#include "Chat/ChatManager.hpp"
#include "Requests/SongRequestManager.hpp"
#include "UI/ChatPanel.hpp"
#include "UI/Settings.hpp"

#include "scotland2/shared/loader.hpp"
#ifdef SBSC_BS_1_40
#include "beatsaber-hook/shared/utils/hooking.hpp"
#else
#include "beatsaber-hook/shared/hooking.hpp"
#endif
#include "custom-types/shared/register.hpp"
#include "bsml/shared/BSML.hpp"

#include "GlobalNamespace/MainMenuViewController.hpp"

#define MOD_EXPORT_FUNC extern "C" __attribute__((visibility("default")))

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

// The floating screen needs BSML's menu pieces, which exist once the main menu is up.
MAKE_HOOK_MATCH(MainMenuViewController_DidActivate, &GlobalNamespace::MainMenuViewController::DidActivate, void,
    GlobalNamespace::MainMenuViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
    MainMenuViewController_DidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);
    if (firstActivation)
        SimpleBSStreamChat::ChatPanel::EnsureCreated();
}

// Called very early by scotland2, before il2cpp is usable.
MOD_EXPORT_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
    Paper::Logger::RegisterFileContextId(MOD_ID);
    INFO("Setup complete, version {}", VERSION);
}

// Called on the main thread once il2cpp and the libraries we depend on are loaded.
MOD_EXPORT_FUNC void late_load() noexcept {
    SimpleBSStreamChat::LoadConfig();

    custom_types::Register::AutoRegister();
    BSML::Init();
    BSML::Register::RegisterMainMenu("Stream Chat", "SimpleBSStreamChat", "Twitch + YouTube + Kick stream chat settings",
        &SimpleBSStreamChat::UI::SettingsDidActivate);

    // A named logger: beatsaber-hook 6 (1.40.8) takes it by non-const reference.
    static auto hookLogger = Paper::ConstLoggerContext(MOD_ID);
    INSTALL_HOOK(hookLogger, MainMenuViewController_DidActivate);

    // Connect right away, like the PC build, so chat is live by the time the menu shows.
    SimpleBSStreamChat::Requests::SongRequestManager::Instance().Start();
    SimpleBSStreamChat::Chat::ChatManager::Instance().Start();
    INFO("Late load complete");
}
