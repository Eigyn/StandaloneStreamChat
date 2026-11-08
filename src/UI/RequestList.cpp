#include "UI/RequestList.hpp"
#include "UI/HexColor.hpp"
#include "UI/Icons.hpp"
#include "UI/RequestRow.hpp"
#include "Requests/SongRequestManager.hpp"
#include "Songs/MenuState.hpp"
#include "Config.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/delegates.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"

#include "TMPro/FontStyles.hpp"
#include "TMPro/TMP_FontAsset.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "Compat.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Events/UnityAction.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/TextCore/FaceInfo.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Image.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

DEFINE_TYPE(StandaloneStreamChat, RequestList);

namespace {
    using namespace StandaloneStreamChat;

    // In panel units.
    constexpr float RowSpacing = 0.8f;
    constexpr float SidecarWidth = 10.0f;
    constexpr float SidecarGap = 3.0f;
    constexpr float SidecarButtonHeight = 7.0f;
    constexpr float SidecarPadding = 0.6f;
    constexpr float TrackInset = 3.0f;
    constexpr float HandleWidth = 1.2f;  // a thin centered indicator bar
    constexpr float MinimumHandleHeight = 2.5f;
    constexpr float ArrowSize = 3.2f;
    UnityEngine::Color const HandleTint(0.95f, 0.82f, 0.29f, 0.95f);
    // Accepting from the results screen crashed the game (seen on the old PC
    // version), so Accept is off there and this says why.
    constexpr char const* ResultsNote = "Song requests can't be accepted from the results screen";
    constexpr float ResultsNoteScale = 0.85f;  // of the chat font size

    UnityEngine::RectTransform* NewRect(UnityEngine::Transform* parent, StringW name) {
        auto go = UnityEngine::GameObject::New_ctor(name);
        auto rect = go->AddComponent<UnityEngine::RectTransform*>();
        rect->SetParent(parent, false);
        return rect;
    }

    void Stretch(UnityEngine::RectTransform* rect) {
        rect->set_anchorMin({0.0f, 0.0f});
        rect->set_anchorMax({1.0f, 1.0f});
        rect->set_offsetMin({0.0f, 0.0f});
        rect->set_offsetMax({0.0f, 0.0f});
    }

    HMUI::ImageView* AddImage(UnityEngine::RectTransform* rect, UnityEngine::Sprite* sprite, UnityEngine::Color color, bool raycastTarget) {
        auto image = rect->get_gameObject()->AddComponent<HMUI::ImageView*>();
        image->set_material(BSML::Helpers::GetUINoGlowMat());
        image->set_sprite(sprite);
        image->set_color(color);
        image->set_raycastTarget(raycastTarget);
        return image;
    }

    UnityEngine::UI::Button* MakeArrow(UnityEngine::RectTransform* strip, StringW name, bool up, HMUI::ImageView*& icon, std::function<void()> onClick) {
        auto rect = NewRect(strip, name);
        float edge = up ? 1.0f : 0.0f;
        rect->set_anchorMin({0.5f, edge});
        rect->set_anchorMax({0.5f, edge});
        rect->set_pivot({0.5f, edge});
        rect->set_anchoredPosition({0.0f, up ? -SidecarPadding : SidecarPadding});
        rect->set_sizeDelta({SidecarWidth - 1.0f, SidecarButtonHeight});
        // Invisible, but still the press target (raycasts ignore alpha); the
        // arrow's own brightness shows whether it can scroll.
        auto hitArea = AddImage(rect, BSML::Utilities::ImageResources::GetWhitePixel(), UnityEngine::Color(1.0f, 1.0f, 1.0f, 0.0f), true);

        auto arrow = NewRect(rect, "Arrow");
        arrow->set_sizeDelta({ArrowSize, ArrowSize});
        if (!up)
            arrow->set_localRotation(UnityEngine::Quaternion::Euler(0.0f, 0.0f, 180.0f));
        icon = AddImage(arrow, UI::Icons::ArrowUp(), UnityEngine::Color(1.0f, 1.0f, 1.0f, 1.0f), false);
        icon->set_preserveAspect(true);

        auto button = rect->get_gameObject()->AddComponent<UnityEngine::UI::Button*>();
        button->set_targetGraphic(hitArea);
        button->get_onClick()->AddListener(BSML::MakeUnityAction(std::move(onClick)));
        return button;
    }

    void SetArrow(UnityEngine::UI::Button* button, HMUI::ImageView* icon, bool enabled) {
        button->set_interactable(enabled);
        icon->set_color(UnityEngine::Color(1.0f, 1.0f, 1.0f, enabled ? 1.0f : 0.3f));
    }
}

namespace StandaloneStreamChat {
    RequestList* RequestList::Create(UnityEngine::RectTransform* content, UnityEngine::RectTransform* screen, HMUI::ImageView* backdrop) {
        auto rect = NewRect(content, "RequestsContent");
        Stretch(rect);
        // Inactive until the panel switches to Requests mode, and so that
        // OnEnable doesn't run before the fields below are set.
        rect->get_gameObject()->SetActive(false);
        auto list = rect->get_gameObject()->AddComponent<RequestList*>();
        list->_rect = rect;
        list->_lastVersion = -1;
        list->_first = 0;
        list->_topId = -1;
        list->_visibleRows = 1;
        list->_rowHeight = 1.0f;
        list->_layoutHeight = -1.0f;
        list->_canAccept = false;
        list->_nextAcceptCheck = 0.0f;

        auto rows = NewRect(rect, "Rows");
        Stretch(rows);
        list->_rows = rows;

        auto empty = BSML::Lite::CreateText(rect, "No requests yet", TMPro::FontStyles::Italic, 3.4f);
        empty->set_alignment(TMPro::TextAlignmentOptions::Center);
        empty->set_raycastTarget(false);
        Stretch(empty->get_rectTransform());
        list->_emptyLabel = empty;

        // Sized and shown by Fit/Rebuild; shrinks to stay on one line.
        auto note = BSML::Lite::CreateText(rect, ResultsNote, TMPro::FontStyles::Italic, 3.0f);
        note->set_alignment(TMPro::TextAlignmentOptions::Center);
        SetWordWrap(note, false);
        note->set_enableAutoSizing(true);
        note->set_fontSizeMin(1.5f);
        note->set_color(UnityEngine::Color(0.73f, 0.73f, 0.73f, 1.0f));
        note->set_raycastTarget(false);
        auto noteRect = note->get_rectTransform();
        noteRect->set_anchorMin({0.0f, 0.0f});
        noteRect->set_anchorMax({1.0f, 0.0f});
        noteRect->set_pivot({0.5f, 0.0f});
        note->get_gameObject()->SetActive(false);
        list->_resultsNote = note;
        list->_onResults = false;

        // The scroll strip, centered on the panel just outside its right edge.
        auto strip = NewRect(screen, "RequestScrollStrip");
        strip->set_anchorMin({1.0f, 0.5f});
        strip->set_anchorMax({1.0f, 0.5f});
        strip->set_pivot({0.0f, 0.5f});
        strip->set_anchoredPosition({SidecarGap, 0.0f});
        UnityEngine::Sprite* backdropSprite = nullptr;
        if (backdrop)
            backdropSprite = backdrop->get_sprite();
        if (!backdropSprite)
            backdropSprite = BSML::Utilities::ImageResources::GetWhitePixel();
        auto stripBackground = AddImage(strip, backdropSprite, UnityEngine::Color(0.0f, 0.0f, 0.0f, 0.5f), false);
        if (backdrop)
            stripBackground->set_type(backdrop->get_type());
        list->_sidecar = strip;
        list->_sidecarBackground = stripBackground;
        list->_backdrop = backdrop;

        UnityW<RequestList> self(list);
        list->_scrollUp = MakeArrow(strip, "ScrollUp", true, list->_scrollUpIcon, [self]() mutable {
            if (self)
                self->Scroll(-1);
        });
        list->_scrollDown = MakeArrow(strip, "ScrollDown", false, list->_scrollDownIcon, [self]() mutable {
            if (self)
                self->Scroll(1);
        });

        auto track = NewRect(strip, "ScrollTrack");
        track->set_anchorMin({0.0f, 0.0f});
        track->set_anchorMax({1.0f, 1.0f});
        track->set_offsetMin({TrackInset, SidecarButtonHeight + SidecarPadding});
        track->set_offsetMax({-TrackInset, -(SidecarButtonHeight + SidecarPadding)});
        AddImage(track, BSML::Utilities::ImageResources::GetWhitePixel(), UnityEngine::Color(1.0f, 1.0f, 1.0f, 0.16f), false);
        list->_scrollTrack = track;

        // Sized and placed by hand (UpdateScrollbar) so it always stays inside the track.
        auto handle = NewRect(track, "ScrollHandle");
        handle->set_anchorMin({0.5f, 1.0f});
        handle->set_anchorMax({0.5f, 1.0f});
        handle->set_pivot({0.5f, 1.0f});
        handle->set_sizeDelta({HandleWidth, MinimumHandleHeight});
        AddImage(handle, BSML::Utilities::ImageResources::GetWhitePixel(), HandleTint, false);
        list->_scrollHandle = handle;

        strip->get_gameObject()->SetActive(false);
        return list;
    }

    void RequestList::ApplyConfig(UnityEngine::Material* textMaterial) {
        auto const& config = GetConfig();
        _textMaterial = textMaterial;
        for (auto text : {_emptyLabel, _resultsNote}) {
            UnityEngine::Material* current = text->get_fontSharedMaterial();
            if (textMaterial && current != textMaterial)
                text->set_fontSharedMaterial(textMaterial);
        }
        _emptyLabel->set_fontSize(config.fontSize);
        _resultsNote->set_fontSizeMax(config.fontSize * ResultsNoteScale);

        auto tint = UI::ColorFromHex(config.backgroundColor, UnityEngine::Color(0.0f, 0.0f, 0.0f, 1.0f));
        tint.a = std::clamp(config.backgroundOpacity * 0.9f, 0.0f, 1.0f);
        _sidecarBackground->set_color(tint);
        if (_backdrop) {
            _sidecarBackground->set_sprite(_backdrop->get_sprite());
            _sidecarBackground->set_type(_backdrop->get_type());
        }

        // Font size sets the row height; refit and redraw on the next frame.
        _layoutHeight = -1.0f;
        _lastVersion = -1;
    }

    void RequestList::OnEnable() {
        _lastVersion = -1;
        _nextAcceptCheck = 0.0f;
    }

    void RequestList::OnDisable() {
        if (_sidecar)
            _sidecar->get_gameObject()->SetActive(false);
    }

    void RequestList::Update() {
        if (std::abs(_rect->get_rect().height - _layoutHeight) > 0.01f) {
            Fit();
            _lastVersion = -1;
        }

        // Match the manager's guard, including its single active acceptance.
        float now = UnityEngine::Time::get_unscaledTime();
        if (now >= _nextAcceptCheck) {
            _nextAcceptCheck = now + 0.25f;
            if (bool onResults = Songs::MenuState::ResultsShowing(); onResults != _onResults) {
                _onResults = onResults;
                Fit();
                _lastVersion = -1;
            }
            auto& manager = Requests::SongRequestManager::Instance();
            manager.RefreshInstalledRequests();
            bool canAccept = manager.CanAccept();
            if (canAccept != _canAccept) {
                _canAccept = canAccept;
                for (int i = 0, count = _rows->get_childCount(); i < count; i++) {
                    auto row = _rows->GetChild(i)->GetComponent<RequestRow*>();
                    if (row->get_gameObject()->get_activeSelf())
                        row->SetCanAccept(canAccept);
                }
            }
        }

        auto version = Requests::SongRequestManager::Instance().Version();
        if (version != _lastVersion) {
            _lastVersion = version;
            Rebuild();
        }
        for (int i = 0, count = _rows->get_childCount(); i < count; i++) {
            auto row = _rows->GetChild(i)->GetComponent<RequestRow*>();
            if (row->get_gameObject()->get_activeSelf())
                row->Animate();
        }
    }

    // As many whole rows as fit: two lines of text plus padding each, so no
    // row is ever cut off at the edge.
    void RequestList::Fit() {
        float lineRatio = 1.2f;
        if (auto font = _emptyLabel->get_font()) {
            auto face = font->get_faceInfo();
            if (face.get_pointSize() > 0.0f)
                lineRatio = face.get_lineHeight() / face.get_pointSize();
        }
        float fontSize = GetConfig().fontSize;
        _rowHeight = std::max(1.0f, fontSize * lineRatio * 2.0f + 3.0f);
        // The results note takes a line at the bottom, so rows never sit under it.
        float noteHeight = _onResults ? fontSize * ResultsNoteScale * lineRatio + 1.0f : 0.0f;
        _resultsNote->get_rectTransform()->set_offsetMin({0.0f, 0.0f});
        _resultsNote->get_rectTransform()->set_offsetMax({0.0f, noteHeight});
        float available = std::max(1.0f, _rect->get_rect().height - (_onResults ? noteHeight + RowSpacing : 0.0f));
        _layoutHeight = _rect->get_rect().height;
        _visibleRows = std::max(1, static_cast<int>(std::floor((available + RowSpacing) / (_rowHeight + RowSpacing))));
        _sidecar->set_sizeDelta({SidecarWidth, available});
    }

    void RequestList::Rebuild() {
        auto requests = Requests::SongRequestManager::Instance().Snapshot();
        int count = static_cast<int>(requests.size());
        // Keep the request that was at the top there as others come and go.
        if (_topId >= 0) {
            auto it = std::find_if(requests.begin(), requests.end(), [this](auto const& r) { return r.id == _topId; });
            if (it != requests.end())
                _first = static_cast<int>(it - requests.begin());
        }
        _first = std::clamp(_first, 0, std::max(0, count - _visibleRows));
        _topId = _first < count ? requests[_first].id : -1;

        while (_rows->get_childCount() < _visibleRows)
            RequestRow::Create(_rows);
        auto const& config = GetConfig();
        float stride = _rowHeight + RowSpacing;
        for (int i = 0, rows = _rows->get_childCount(); i < rows; i++) {
            auto row = _rows->GetChild(i)->GetComponent<RequestRow*>();
            int index = _first + i;
            bool shown = i < _visibleRows && index < count;
            if (shown) {
                row->_rect->set_anchoredPosition({0.0f, -i * stride});
                row->Show(requests[index], index + 1, _rowHeight, _canAccept, config.fontSize, _textMaterial);
            }
            if (row->get_gameObject()->get_activeSelf() != shown)
                row->get_gameObject()->SetActive(shown);
        }
        _emptyLabel->get_gameObject()->SetActive(count == 0);
        _resultsNote->get_gameObject()->SetActive(_onResults && count > 0);
        UpdateScrollbar(count);
    }

    void RequestList::Scroll(int rows) {
        _first += rows;
        _topId = -1;
        Rebuild();
    }

    // Sizes the handle to the visible share of the queue and slides it down
    // the track by the scroll position, always fully inside the track.
    void RequestList::UpdateScrollbar(int count) {
        bool overflows = count > _visibleRows;
        bool show = overflows && get_isActiveAndEnabled();
        if (_sidecar->get_gameObject()->get_activeSelf() != show)
            _sidecar->get_gameObject()->SetActive(show);
        if (!overflows)
            return;

        int lastFirst = count - _visibleRows;
        SetArrow(_scrollUp, _scrollUpIcon, _first > 0);
        SetArrow(_scrollDown, _scrollDownIcon, _first < lastFirst);

        float track = _scrollTrack->get_rect().height;
        if (track <= 0.0f)
            return;
        float handle = std::clamp(track * static_cast<float>(_visibleRows) / static_cast<float>(count), std::min(MinimumHandleHeight, track), track);
        _scrollHandle->set_sizeDelta({HandleWidth, handle});
        float scrolled = lastFirst == 0 ? 0.0f : static_cast<float>(_first) / static_cast<float>(lastFirst);
        _scrollHandle->set_anchoredPosition({0.0f, -scrolled * (track - handle)});
    }
}
