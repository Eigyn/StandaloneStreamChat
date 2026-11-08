#pragma once

#include "HMUI/ViewController.hpp"

namespace StandaloneStreamChat::UI {
    /// Builds the Mods > StandaloneStreamChat settings screen.
    void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
}
