#include "UI/Settings.hpp"
#include "UI/SettingsBsml.hpp"
#include "UI/SettingsHost.hpp"
#include "Compat.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/Settings/ColorSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/StringSetting.hpp"
#include "bsml/shared/Helpers/utilities.hpp"

#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Image.hpp"

namespace {
    void FormatEditPicker(UnityEngine::RectTransform* picker, UnityEngine::UI::Button* editButton) {
        if (!picker || !editButton)
            return;

        // Quest BSML clones a two-button numeric picker for string/color settings.
        // Hide the unused decrement button and make the edit button one field.
        for (auto button : picker->GetComponentsInChildren<UnityEngine::UI::Button*>(true)) {
            if (button != editButton)
                button->get_gameObject()->SetActive(false);
        }

        auto editTransform = editButton->get_transform();
        auto editRect = editTransform->GetComponent<UnityEngine::RectTransform*>();
        editRect->set_anchorMin({0.0f, 0.0f});
        editRect->set_anchorMax({1.0f, 1.0f});
        editRect->set_offsetMin({0.0f, 0.0f});
        editRect->set_offsetMax({0.0f, 0.0f});

        if (auto background = editTransform->Find("BG")) {
            if (auto image = background->GetComponent<UnityEngine::UI::Image*>()) {
                if (auto sprite = BSML::Utilities::FindSpriteCached("RoundRect10Thin"))
                    image->set_sprite(sprite);
            }
        }

        auto icon = editTransform->Find("EditIcon");
        if (!icon)
            icon = editTransform->Find("Icon");
        if (icon) {
            auto iconRect = icon->GetComponent<UnityEngine::RectTransform*>();
            iconRect->set_anchorMin({1.0f, 0.5f});
            iconRect->set_anchorMax({1.0f, 0.5f});
            iconRect->set_anchoredPosition({-4.0f, 0.0f});
            iconRect->set_sizeDelta({4.0f, 4.0f});
        }
    }

    void FormatChannelInput(BSML::StringSetting* setting) {
        if (!setting->boundingBox || !setting->editButton || !setting->text)
            return;

        FormatEditPicker(setting->boundingBox, setting->editButton);

        auto text = setting->text;
        text->set_alignment(TMPro::TextAlignmentOptions::MidlineRight);
        text->set_richText(false);
        text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        text->set_raycastTarget(false);
        SimpleBSStreamChat::SetWordWrap(text, false);
        auto textRect = text->get_rectTransform();
        textRect->set_anchorMin({0.0f, 0.0f});
        textRect->set_anchorMax({1.0f, 1.0f});
        textRect->set_offsetMin({2.0f, 0.0f});
        textRect->set_offsetMax({-8.0f, 0.0f});
        textRect->SetAsLastSibling();
    }

    void FormatColorInput(BSML::ColorSetting* setting) {
        if (!setting->editButton)
            return;

        auto picker = setting->editButton->get_transform()->get_parent()->GetComponent<UnityEngine::RectTransform*>();
        if (!picker)
            return;

        auto pickerSize = picker->get_sizeDelta();
        pickerSize.x = 16.0f;
        picker->set_sizeDelta(pickerSize);
        FormatEditPicker(picker, setting->editButton);
        if (auto swatch = setting->colorImage) {
            auto swatchRect = swatch->get_rectTransform();
            swatchRect->set_anchorMin({0.0f, 0.5f});
            swatchRect->set_anchorMax({0.0f, 0.5f});
            swatchRect->set_anchoredPosition({4.5f, 0.0f});
            swatchRect->set_sizeDelta({5.0f, 5.0f});
            swatch->set_raycastTarget(false);
            swatchRect->SetAsLastSibling();
        }
    }
}

namespace SimpleBSStreamChat::UI {
    void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool, bool) {
        if (!firstActivation)
            return;

        // The host is a component on the view, so it lives exactly as long as the controls bound to it.
        auto host = self->get_gameObject()->AddComponent<SettingsHost*>();
        BSML::parse_and_construct(SettingsBsml, self->get_transform(), host);
        for (auto setting : self->GetComponentsInChildren<BSML::StringSetting*>(true))
            FormatChannelInput(setting);
        for (auto setting : self->GetComponentsInChildren<BSML::ColorSetting*>(true))
            FormatColorInput(setting);
    }
}
