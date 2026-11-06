#include "UI/RequestRow.hpp"
#include "UI/Icons.hpp"
#include "UI/RichText.hpp"
#include "Requests/SongRequestManager.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/delegates.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"

#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "Compat.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Events/UnityAction.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"

#include <algorithm>
#include <functional>

DEFINE_TYPE(SimpleBSStreamChat, RequestRow);

namespace {
    using namespace SimpleBSStreamChat;
    using Requests::RequestStatus;
    using Requests::SongRequest;

    // PC colors.
    UnityEngine::Color const AcceptTint(0.45f, 0.92f, 0.45f, 1.0f);
    UnityEngine::Color const DenyTint(1.0f, 0.42f, 0.42f, 1.0f);
    UnityEngine::Color const RingTint(1.0f, 0.82f, 0.29f, 0.95f);
    UnityEngine::Color const White(1.0f, 1.0f, 1.0f, 1.0f);

    UnityEngine::GameObject* NewChild(UnityEngine::Transform* parent, StringW name) {
        auto go = UnityEngine::GameObject::New_ctor(name);
        go->AddComponent<UnityEngine::RectTransform*>()->SetParent(parent, false);
        return go;
    }

    HMUI::ImageView* AddImage(UnityEngine::GameObject* go, UnityEngine::Sprite* sprite, UnityEngine::Color color, bool raycastTarget) {
        auto image = go->AddComponent<HMUI::ImageView*>();
        image->set_material(BSML::Helpers::GetUINoGlowMat());
        image->set_sprite(sprite);
        image->set_color(color);
        image->set_preserveAspect(true);
        image->set_raycastTarget(raycastTarget);
        return image;
    }

    UnityEngine::UI::LayoutElement* LayoutOf(UnityEngine::GameObject* go) {
        auto layout = go->GetComponent<UnityEngine::UI::LayoutElement*>();
        return layout ? layout : go->AddComponent<UnityEngine::UI::LayoutElement*>();
    }

    UnityEngine::UI::Button* MakeButton(UnityEngine::Transform* parent, StringW name, UnityEngine::Sprite* sprite, UnityEngine::Color tint,
                                        HMUI::ImageView*& icon, UnityEngine::UI::LayoutElement*& layout, std::function<void()> onClick) {
        auto go = NewChild(parent, name);
        icon = AddImage(go, sprite, tint, true);
        layout = LayoutOf(go);
        layout->set_flexibleWidth(0.0f);
        auto button = go->AddComponent<UnityEngine::UI::Button*>();
        button->set_targetGraphic(icon);
        button->get_onClick()->AddListener(BSML::MakeUnityAction(std::move(onClick)));
        return button;
    }

    void SetEnabled(UnityEngine::UI::Button* button, HMUI::ImageView* icon, UnityEngine::Color tint, bool enabled) {
        button->set_interactable(enabled);
        icon->set_color(enabled ? tint : UnityEngine::Color(tint.r, tint.g, tint.b, 0.35f));
    }

    bool Busy(RequestStatus status) {
        return status == RequestStatus::Preparing || status == RequestStatus::Downloading || status == RequestStatus::Installing ||
               status == RequestStatus::Selecting;
    }

    // Busy steps without a percentage, shown with a spinning ring.
    bool Spins(int status) {
        return status == static_cast<int>(RequestStatus::Preparing) || status == static_cast<int>(RequestStatus::Installing) ||
               status == static_cast<int>(RequestStatus::Selecting);
    }

    bool Acceptable(int status) {
        return status == static_cast<int>(RequestStatus::Ready) || status == static_cast<int>(RequestStatus::Installed) ||
               status == static_cast<int>(RequestStatus::Failed);
    }

    // The PC's row text: "#1 requester" over the map line or install status.
    std::string Format(SongRequest const& request, int position) {
        auto text = fmt::format("<color=#FFD24A>#{}</color> ", position) + UI::Escape(request.requester) + "\n";
        auto detail = request.detail.empty() ? std::string() : " " + UI::Escape(request.detail);
        switch (request.status) {
            case RequestStatus::FetchingInfo:
                return text + "<color=#999999><i>loading...</i> · <color=#7FD4FF>" + request.key + "</color></color>";
            case RequestStatus::Preparing:
                return text + "<color=#FFD24A>Checking installed map</color>";
            case RequestStatus::Downloading:
                return text + "<color=#FFD24A>Downloading" + detail + "</color>";
            case RequestStatus::Installing:
                text += "<color=#FFD24A>Installing</color>";
                if (!request.detail.empty())
                    text += " <color=#999999>(" + UI::Escape(request.detail) + ")</color>";
                return text;
            case RequestStatus::Selecting:
                return text + "<color=#FFD24A>Selecting" + detail + "</color>";
            default:
                break;
        }

        text += "<color=#DDDDDD>" + UI::Escape(request.songName) + "</color><color=#999999>";
        if (!request.songAuthor.empty())
            text += " · " + UI::Escape(request.songAuthor);
        if (!request.mapper.empty())
            text += " · " + UI::Escape(request.mapper);
        text += " · </color><color=#7FD4FF>" + request.key + "</color>";
        if (request.status == RequestStatus::NotFound)
            text += " <color=#FF6B6B>[not found]</color>";
        else if (request.status == RequestStatus::Failed)
            text += " <color=#FF6B6B>[failed" + detail + "]</color>";
        else if (request.status == RequestStatus::Installed)
            text += request.detail.empty() ? " <color=#7FD4FF>[installed - select]</color>"
                                          : " <color=#FFD24A>[installed: " + UI::Escape(request.detail) + "]</color>";
        else if (!request.detail.empty())
            text += " <color=#FFD24A>[" + UI::Escape(request.detail) + "]</color>";
        return text;
    }
}

namespace SimpleBSStreamChat {
    RequestRow* RequestRow::Create(UnityEngine::Transform* parent) {
        auto go = UnityEngine::GameObject::New_ctor("RequestRow");
        auto rect = go->AddComponent<UnityEngine::RectTransform*>();
        rect->SetParent(parent, false);
        rect->set_anchorMin({0.0f, 1.0f});
        rect->set_anchorMax({1.0f, 1.0f});
        rect->set_pivot({0.5f, 1.0f});

        // A faint band; presses on it fall through to the panel's grab surface.
        auto background = AddImage(go, BSML::Utilities::ImageResources::GetWhitePixel(), UnityEngine::Color(1.0f, 1.0f, 1.0f, 0.06f), false);
        background->set_preserveAspect(false);

        auto layout = go->AddComponent<UnityEngine::UI::HorizontalLayoutGroup*>();
        layout->set_padding(UnityEngine::RectOffset::New_ctor(2, 2, 1, 1));
        layout->set_spacing(1.2f);
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(false);
        layout->set_childForceExpandHeight(true);
        layout->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);

        auto row = go->AddComponent<RequestRow*>();
        row->_rect = rect;
        row->_background = background;
        row->_requestId = -1;
        row->_status = -1;
        row->_fill = 0.0f;

        auto iconGo = NewChild(rect, "PlatformIcon");
        row->_platformIcon = AddImage(iconGo, nullptr, White, false);
        row->_platformIconLayout = LayoutOf(iconGo);

        auto text = BSML::Lite::CreateText(rect, "", TMPro::FontStyles::Normal, 3.4f);
        text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
        SetWordWrap(text, false);
        text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        text->set_richText(true);
        text->set_raycastTarget(false);
        auto textLayout = LayoutOf(text->get_gameObject());
        textLayout->set_flexibleWidth(1.0f);
        textLayout->set_minWidth(0.0f);  // takes all the squeeze, ellipsizing
        row->_text = text;

        // The buttons read the row's current request when pressed.
        UnityW<RequestRow> self(row);
        row->_accept = MakeButton(rect, "Accept", UI::Icons::Accept(), AcceptTint, row->_acceptIcon, row->_acceptLayout, [self] {
            if (self)
                Requests::SongRequestManager::Instance().Accept(self->_requestId);
        });
        row->_deny = MakeButton(rect, "Deny", UI::Icons::Deny(), DenyTint, row->_denyIcon, row->_denyLayout, [self] {
            if (self)
                Requests::SongRequestManager::Instance().Deny(self->_requestId);
        });

        auto ringGo = NewChild(rect, "Progress");
        auto ring = AddImage(ringGo, UI::Icons::Ring(), RingTint, false);
        ring->set_type(UnityEngine::UI::Image::Type::Filled);
        ring->set_fillMethod(UnityEngine::UI::Image::FillMethod::Radial360);
        ring->set_fillOrigin(2);  // top
        ring->set_fillClockwise(true);
        ring->set_fillAmount(0.0f);
        row->_progress = ring;
        row->_progressLayout = LayoutOf(ringGo);
        row->_progressLayout->set_flexibleWidth(0.0f);
        ringGo->SetActive(false);
        return row;
    }

    void RequestRow::Show(SongRequest const& request, int position, float height, bool canAccept, float fontSize, UnityEngine::Material* textMaterial) {
        _rect->set_sizeDelta({0.0f, height});
        // Pin min to preferred on the fixed parts so a long name can't squeeze
        // the icon or buttons; only the text shrinks (and ellipsizes), so every
        // row's buttons line up at any panel width.
        float control = height * 0.55f;
        for (auto layout : {_platformIconLayout, _acceptLayout, _denyLayout}) {
            layout->set_preferredWidth(control);
            layout->set_minWidth(control);
        }
        _platformIconLayout->set_preferredHeight(control);
        float ring = height * 1.22f;
        _progressLayout->set_preferredWidth(ring);
        _progressLayout->set_minWidth(ring);

        auto platform = UI::Icons::Platform(request.platform);
        _platformIcon->set_sprite(platform);
        _platformIcon->get_gameObject()->SetActive(platform != nullptr);

        UnityEngine::Material* current = _text->get_fontSharedMaterial();
        if (textMaterial && current != textMaterial)
            _text->set_fontSharedMaterial(textMaterial);
        _text->set_fontSize(fontSize);
        _text->set_text(Format(request, position));

        _requestId = request.id;
        _status = static_cast<int>(request.status);
        _fill = request.progress;
        bool busy = Busy(request.status);
        _accept->get_gameObject()->SetActive(!busy);
        _deny->get_gameObject()->SetActive(!busy);
        _progress->get_gameObject()->SetActive(busy);
        SetCanAccept(canAccept);
        SetEnabled(_deny, _denyIcon, DenyTint, !busy);

        if (request.status == RequestStatus::Downloading) {
            _progress->set_fillAmount(std::clamp(std::max(0.04f, _fill), 0.0f, 1.0f));
            _progress->get_transform()->set_localRotation(UnityEngine::Quaternion::get_identity());
        } else if (Spins(_status)) {
            _progress->set_fillAmount(0.72f);
            Animate();
        }
    }

    void RequestRow::SetCanAccept(bool canAccept) { SetEnabled(_accept, _acceptIcon, AcceptTint, canAccept && Acceptable(_status)); }

    void RequestRow::Animate() {
        if (!Spins(_status))
            return;
        _progress->get_transform()->set_localRotation(UnityEngine::Quaternion::Euler(0.0f, 0.0f, -UnityEngine::Time::get_unscaledTime() * 240.0f));
    }
}
