#pragma once

#include "custom-types/shared/macros.hpp"

#include "HMUI/ImageView.hpp"
#include "Requests/SongRequest.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"

// One request in the request list: platform icon,
// "#position requester" over "song · artist · mapper · key [status]", then
// Accept and Deny, which give way to a progress ring while the map installs.
DECLARE_CLASS_CODEGEN(StandaloneStreamChat, RequestRow, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _rect);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _background);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _platformIcon);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::LayoutElement*, _platformIconLayout);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, _text);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::Button*, _accept);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _acceptIcon);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::LayoutElement*, _acceptLayout);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::Button*, _deny);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _denyIcon);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::LayoutElement*, _denyLayout);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _progress);
    DECLARE_INSTANCE_FIELD(UnityEngine::UI::LayoutElement*, _progressLayout);
    // What the row shows: SongRequest::id, its RequestStatus and download progress.
    DECLARE_INSTANCE_FIELD(int64_t, _requestId);
    DECLARE_INSTANCE_FIELD(int, _status);
    DECLARE_INSTANCE_FIELD(float, _fill);

   public:
    static RequestRow* Create(UnityEngine::Transform* parent);
    /// Shows `request` as queue entry `position` (1-based), `height` units tall.
    void Show(Requests::SongRequest const& request, int position, float height, bool canAccept, float fontSize, UnityEngine::Material* textMaterial);
    /// Accept is live on the main menu/Solo list with no other acceptance active.
    void SetCanAccept(bool canAccept);
    /// Spins the ring while the menus head to the song list or SongCore loads
    /// the map. Every frame.
    void Animate();
};
