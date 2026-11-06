#include "UI/ViewerCountView.hpp"
#include "UI/Icons.hpp"
#include "UI/PlatformTag.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/getters.hpp"

#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "Compat.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"

DEFINE_TYPE(SimpleBSStreamChat, ViewerCountView);

namespace {
    // "5,605"; an en dash while unknown or offline.
    std::string FormatViewers(int viewers) {
        if (viewers < 0)
            return "–";
        auto digits = std::to_string(viewers);
        std::string out;
        for (size_t i = 0; i < digits.size(); i++) {
            if (i > 0 && (digits.size() - i) % 3 == 0)
                out.push_back(',');
            out.push_back(digits[i]);
        }
        return out;
    }
}

namespace SimpleBSStreamChat {
    ViewerCountView* ViewerCountView::Create(UnityEngine::Transform* parent, Chat::ChatPlatform platform) {
        auto go = UnityEngine::GameObject::New_ctor("ViewerCount");
        go->AddComponent<UnityEngine::RectTransform*>()->SetParent(parent, false);
        auto layout = go->AddComponent<UnityEngine::UI::HorizontalLayoutGroup*>();
        layout->set_childAlignment(UnityEngine::TextAnchor::MiddleRight);
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(false);
        layout->set_childForceExpandHeight(false);
        layout->set_spacing(1.0f);

        // ImageView rather than a TMP sprite so the icon bends with a curved panel.
        auto iconGo = UnityEngine::GameObject::New_ctor("Icon");
        iconGo->AddComponent<UnityEngine::RectTransform*>()->SetParent(go->get_transform(), false);
        auto icon = iconGo->AddComponent<HMUI::ImageView*>();
        icon->set_material(BSML::Helpers::GetUINoGlowMat());
        icon->set_preserveAspect(true);
        icon->set_raycastTarget(false);

        auto count = BSML::Lite::CreateText(go->get_transform(), "", TMPro::FontStyles::Normal, 2.9f);
        count->set_alignment(TMPro::TextAlignmentOptions::Left);
        SetWordWrap(count, false);
        count->set_richText(true);
        count->set_raycastTarget(false);

        auto view = go->AddComponent<ViewerCountView*>();
        view->_icon = icon;
        view->_iconLayout = iconGo->AddComponent<UnityEngine::UI::LayoutElement*>();
        view->_count = count;
        view->_platform = static_cast<int>(platform);
        return view;
    }

    void ViewerCountView::Show(int viewers, bool useIcon, float size, UnityEngine::Material* textMaterial) {
        auto platform = static_cast<Chat::ChatPlatform>(_platform);
        auto sprite = useIcon ? UI::Icons::Platform(platform) : nullptr;
        _icon->get_gameObject()->SetActive(sprite != nullptr);
        if (sprite) {
            _icon->set_sprite(sprite);
            _iconLayout->set_preferredWidth(size);
            _iconLayout->set_preferredHeight(size);
        }

        if (textMaterial)
            _count->set_fontSharedMaterial(textMaterial);
        _count->set_fontSize(size);
        auto text = FormatViewers(viewers);
        _count->set_text(sprite ? text : std::string(UI::PlatformTag(platform)) + text);
    }
}
