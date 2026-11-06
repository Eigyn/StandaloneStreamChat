#pragma once

#include "custom-types/shared/macros.hpp"
#include "bsml/shared/macros.hpp"

#include "UnityEngine/Color.hpp"
#include "UnityEngine/MonoBehaviour.hpp"

// Binding target for SettingsBsml. Each property reads and writes the config
// directly (the generated backing fields stay unused) and applies the change
// right away. Lives on the settings view controller's GameObject.
DECLARE_CLASS_CODEGEN(SimpleBSStreamChat, SettingsHost, UnityEngine::MonoBehaviour) {
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

    // Requests
    DECLARE_BSML_PROPERTY(bool, enableSongRequests);
    DECLARE_BSML_PROPERTY(bool, allowDuplicates);
    DECLARE_BSML_PROPERTY(bool, persistRequestQueue);
    DECLARE_BSML_PROPERTY(int, requestCooldown);  // integer-only slider

    // General
    DECLARE_BSML_PROPERTY(bool, showWindow);
    DECLARE_BSML_PROPERTY(bool, highlightMessages);
    DECLARE_BSML_PROPERTY(bool, platformIcons);
    DECLARE_BSML_PROPERTY(bool, emojiImages);
    DECLARE_BSML_PROPERTY(bool, animatedEmotes);
    DECLARE_BSML_PROPERTY(bool, showViewerCounts);
    DECLARE_BSML_PROPERTY(bool, showSystem);
    DECLARE_INSTANCE_METHOD(void, recenterWindow);
};
