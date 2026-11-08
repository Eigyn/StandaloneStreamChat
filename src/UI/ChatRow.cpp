#include "UI/ChatRow.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"

#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "Compat.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"

DEFINE_TYPE(StandaloneStreamChat, ChatRow);

namespace StandaloneStreamChat {
    ChatRow* ChatRow::Create(UnityEngine::Transform* parent) {
        auto go = UnityEngine::GameObject::New_ctor("ChatRow");
        auto rect = go->AddComponent<UnityEngine::RectTransform*>();
        rect->SetParent(parent, false);

        auto background = go->AddComponent<HMUI::ImageView*>();
        background->set_sprite(BSML::Utilities::ImageResources::GetWhitePixel());
        background->set_material(BSML::Helpers::GetUINoGlowMat());
        background->set_raycastTarget(false);  // presses go through to the panel's grab surface

        // The text fills the row (less a unit of padding each side) and the row
        // takes the text's wrapped height.
        auto layout = go->AddComponent<UnityEngine::UI::HorizontalLayoutGroup*>();
        layout->set_padding(UnityEngine::RectOffset::New_ctor(1, 1, 0, 0));
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(true);
        layout->set_childForceExpandHeight(false);

        auto text = BSML::Lite::CreateText(rect, "", TMPro::FontStyles::Normal, 3.4f);
        text->set_alignment(TMPro::TextAlignmentOptions::TopLeft);
        SetWordWrap(text, true);
        text->set_richText(true);
        text->set_raycastTarget(false);

        // Unset (-1) until the drawn text turns out taller than the layout height.
        auto fit = go->AddComponent<UnityEngine::UI::LayoutElement*>();

        auto row = go->AddComponent<ChatRow*>();
        row->_background = background;
        row->_text = text;
        row->_sequence = -1;
        row->_revision = -1;
        row->_waitingForEmotes = false;
        row->_fit = fit;
        row->_heightChecks = 0;
        return row;
    }
}
