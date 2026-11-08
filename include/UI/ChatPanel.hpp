#pragma once

#include "custom-types/shared/macros.hpp"

#include "GlobalNamespace/VRController.hpp"
#include "HMUI/ImageView.hpp"
#include "UnityEngine/EventSystems/IEventSystemHandler.hpp"
#include "UnityEngine/EventSystems/IPointerDownHandler.hpp"
#include "UnityEngine/EventSystems/IPointerUpHandler.hpp"
#include "UnityEngine/EventSystems/PointerEventData.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "Chat/ChatMessage.hpp"
#include "UI/ChatRow.hpp"
#include "UI/RequestList.hpp"
#include "UI/ViewerCountView.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"

#include <functional>
#include <vector>

// Floating chat panel. The newest messages stack upward from the bottom as
// ChatRows; older ones overflow the top and are clipped by a RectMask2D. Lives
// on a DontDestroyOnLoad floating screen so it survives soft restarts and songs.
// The lock, move-mode and requests buttons sit in a column outside
// its left edge; the requests button swaps the feed for the request queue.
//
// Moving: while unlocked, pressing anywhere on the backdrop grabs the panel.
// The press reaches this component by bubbling up from the backdrop graphic;
// BSML's 3D grab handle never received pointer hits on this screen.
DECLARE_CLASS_CODEGEN_INTERFACES(StandaloneStreamChat, ChatPanel, UnityEngine::MonoBehaviour,
                                 UnityEngine::EventSystems::IPointerDownHandler*, UnityEngine::EventSystems::IPointerUpHandler*,
                                 UnityEngine::EventSystems::IEventSystemHandler*) {
    DECLARE_INSTANCE_FIELD(BSML::FloatingScreen*, _screen);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _background);
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _rows);  // ChatRows, oldest first
    // Set when every row must be rebuilt (a setting changed), not just new ones added.
    DECLARE_INSTANCE_FIELD(bool, _rerenderRows);
    DECLARE_INSTANCE_FIELD(int, _emoteSetVersion);  // ThirdPartyEmotes::Version() the rows were built with
    // BSML's font material bends with the canvas; the flat copy has that
    // turned off, since curved text keeps stale bend data at radius 0.
    DECLARE_INSTANCE_FIELD(UnityEngine::Material*, _curvedTextMaterial);
    DECLARE_INSTANCE_FIELD(UnityEngine::Material*, _flatTextMaterial);
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _content);
    DECLARE_INSTANCE_FIELD(UnityEngine::RectTransform*, _viewerStrip);
    DECLARE_INSTANCE_FIELD(ViewerCountView*, _twitchViewers);
    DECLARE_INSTANCE_FIELD(ViewerCountView*, _youTubeViewers);
    DECLARE_INSTANCE_FIELD(ViewerCountView*, _kickViewers);
    DECLARE_INSTANCE_FIELD(float, _nextViewerRefresh);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _lockIcon);
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _moveModeIcon);
    // Requests mode swaps the feed for the request list (the third side button).
    DECLARE_INSTANCE_FIELD(HMUI::ImageView*, _requestsIcon);
    DECLARE_INSTANCE_FIELD(RequestList*, _requestList);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, _requestsLabel);  // "Requests (N)" in the top band
    DECLARE_INSTANCE_FIELD(bool, _requestsMode);
    DECLARE_INSTANCE_FIELD(bool, _unreadRequests);  // requests came in while the feed was showing
    DECLARE_INSTANCE_FIELD(int, _requestsLabelVersion);
    // Set while dragging: the controller and the panel's pose relative to it.
    DECLARE_INSTANCE_FIELD(UnityW<GlobalNamespace::VRController>, _grabController);
    DECLARE_INSTANCE_FIELD(UnityEngine::Vector3, _grabLocalPosition);
    DECLARE_INSTANCE_FIELD(UnityEngine::Quaternion, _grabLocalRotation);
    // The pose when the drag began, for the move-mode axis lock.
    DECLARE_INSTANCE_FIELD(UnityEngine::Vector3, _grabStartPosition);
    DECLARE_INSTANCE_FIELD(UnityEngine::Quaternion, _grabStartRotation);
    DECLARE_INSTANCE_FIELD(int, _lastVersion);

    DECLARE_INSTANCE_METHOD(void, Update);
    DECLARE_OVERRIDE_METHOD_MATCH(void, OnPointerDown, &::UnityEngine::EventSystems::IPointerDownHandler::OnPointerDown,
                                  UnityEngine::EventSystems::PointerEventData* eventData);
    DECLARE_OVERRIDE_METHOD_MATCH(void, OnPointerUp, &::UnityEngine::EventSystems::IPointerUpHandler::OnPointerUp,
                                  UnityEngine::EventSystems::PointerEventData* eventData);

   public:
    /// Creates the panel the first time the main menu is shown.
    static void EnsureCreated();
    /// Re-applies visibility, size, backdrop, curve and text settings from the config.
    static void ApplyConfig();
    /// Moves the panel back to its default position and saves that.
    static void Recenter();

   private:
    /// The text material for the current curve setting.
    UnityEngine::Material* TextMaterial();
    /// Brings the rows in line with the newest messages, reusing the oldest
    /// row for each new one so only new text gets laid out.
    void SyncRows(std::vector<Chat::ChatMessage> const& messages);
    void RenderRow(ChatRow* row, Chat::ChatMessage const& message);
    /// For a few frames after a row renders, grows it if its drawn text came
    /// out taller than the height TMP gave the layout, so it can't spill
    /// over the row below.
    void FitRowHeights();
    /// Shows/hides the strip and its platforms and fills in the counts; runs
    /// about once a second and on every config change.
    void RefreshViewerStrip();
    /// `slot` 0 is the top of the column.
    HMUI::ImageView* CreateSideButton(StringW name, int slot, std::function<void()> onClick);
    void ApplyLockState();
    void ToggleLock();
    void ToggleMoveMode();
    void ToggleRequestsMode();
    /// The requests button shows the mode it switches to.
    void UpdateRequestsButton();
    void FollowController();
    void ApplyMoveConstraint();
    void EndGrab();
};
