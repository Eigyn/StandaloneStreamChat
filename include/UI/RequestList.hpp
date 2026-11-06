#pragma once

#include "custom-types/shared/macros.hpp"

#include "HMUI/ImageView.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/UI/Button.hpp"

// The panel's Requests mode: a whole number of fixed-height
// rows filling the feed area, scrolled one request at a time by the arrows
// on a strip outside the panel's right edge (shown only when the queue
// overflows), with a bar marking where the view is.
DECLARE_CLASS_CODEGEN(SimpleBSStreamChat, RequestList, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _rect);
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _rows);  // RequestRows, top to bottom
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, _emptyLabel);
    // Under the rows on the results screen, saying why Accept is off there.
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, _resultsNote);
    DECLARE_INSTANCE_FIELD(bool, _onResults);
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _sidecar);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _sidecarBackground);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _backdrop);  // the panel's, whose look the strip copies
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::Button*, _scrollUp);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _scrollUpIcon);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::Button*, _scrollDown);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _scrollDownIcon);
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _scrollTrack);
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _scrollHandle);
    DECLARE_INSTANCE_FIELD(UnityEngine::Material*, _textMaterial);
    DECLARE_INSTANCE_FIELD(int, _lastVersion);
    DECLARE_INSTANCE_FIELD(int, _first);      // queue index in the top row
    DECLARE_INSTANCE_FIELD(int64_t, _topId);  // and its request, kept at the top as others come and go
    DECLARE_INSTANCE_FIELD(int, _visibleRows);
    DECLARE_INSTANCE_FIELD(float, _rowHeight);
    DECLARE_INSTANCE_FIELD(float, _layoutHeight);  // the height the rows were fitted to
    DECLARE_INSTANCE_FIELD(bool, _canAccept);
    DECLARE_INSTANCE_FIELD(float, _nextAcceptCheck);

    DECLARE_INSTANCE_METHOD(void, Update);
    DECLARE_INSTANCE_METHOD(void, OnEnable);
    DECLARE_INSTANCE_METHOD(void, OnDisable);

   public:
    /// Fills `content` (the feed's clip area); the scroll strip goes on `screen`.
    static RequestList* Create(UnityEngine::RectTransform* content, UnityEngine::RectTransform* screen, HMUI::ImageView* backdrop);
    /// Re-reads font, size and backdrop settings, copying the panel backdrop's
    /// sprite (corner rounding). `textMaterial` matches the curve.
    void ApplyConfig(UnityEngine::Material* textMaterial);

   private:
    void Fit();
    void Rebuild();
    void Scroll(int rows);
    void UpdateScrollbar(int count);
};
