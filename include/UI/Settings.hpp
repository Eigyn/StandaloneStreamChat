#pragma once

#include "HMUI/ViewController.hpp"

#include <string_view>

namespace StandaloneStreamChat::UI {
    /// Builds one settings screen from `bsml` on `view`, with a SettingsHost
    /// as its binding target. Once, on the view's first activation.
    void BuildSettingsView(HMUI::ViewController* view, std::string_view bsml);
}
