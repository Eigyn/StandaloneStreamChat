#pragma once

#include "HMUI/ViewController.hpp"

namespace SimpleBSStreamChat::UI {
    /// Builds the Mods > SimpleBSStreamChat settings screen.
    void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
}
