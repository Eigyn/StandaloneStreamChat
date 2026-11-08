#pragma once

#include "custom-types/shared/macros.hpp"

#include "HMUI/ImageView.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Transform.hpp"

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

   public:
    static ChatRow* Create(UnityEngine::Transform* parent);
};
