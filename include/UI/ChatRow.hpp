#pragma once

#include "custom-types/shared/macros.hpp"

#include "HMUI/ImageView.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"

// One chat message in the panel feed: a full-width backdrop (Highlight
// Messages) behind word-wrapped text, sized to fit by the feed's layout.
DECLARE_CLASS_CODEGEN(StandaloneStreamChat, ChatRow, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _background);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, _text);
    // ChatMessage::sequence of the message shown.
    DECLARE_INSTANCE_FIELD(int64_t, _sequence);
    DECLARE_INSTANCE_FIELD(int64_t, _revision);
    // An emote in it is still downloading, so it shows the emote's name for now.
    DECLARE_INSTANCE_FIELD(bool, _waitingForEmotes);
    // Holds the row open to its drawn text when that comes out taller than
    // the height TMP reported to the layout (see ChatPanel::FitRowHeights).
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::LayoutElement*, _fit);
    // Frames left to compare the drawn text with the row after a render.
    DECLARE_INSTANCE_FIELD(int, _heightChecks);

   public:
    static ChatRow* Create(UnityEngine::Transform* parent);
};
