#pragma once

#include "Chat/ChatMessage.hpp"

#include "custom-types/shared/macros.hpp"

#include "HMUI/ImageView.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"

// One platform's [icon count] pair in the panel's viewer strip.
DECLARE_CLASS_CODEGEN(StandaloneStreamChat, ViewerCountView, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _icon);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::LayoutElement*, _iconLayout);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, _count);
    DECLARE_INSTANCE_FIELD(int, _platform);

   public:
    static ViewerCountView* Create(UnityEngine::Transform* parent, Chat::ChatPlatform platform);
    /// `viewers` -1 shows a dash. Without icons the platform's [T]/[Y]/[K]
    /// text tag stands in. `size` is both the font size and
    /// the icon's edge length.
    void Show(int viewers, bool useIcon, float size, UnityEngine::Material* textMaterial);
};
