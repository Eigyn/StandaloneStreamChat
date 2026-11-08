#include "UI/ChatPanel.hpp"
#include "Chat/ChatManager.hpp"
#include "Chat/ThirdPartyEmotes.hpp"
#include "Requests/SongRequestManager.hpp"
#include "Config.hpp"
#include "logging.hpp"

#include "UI/EmoteAtlas.hpp"
#include "UI/HexColor.hpp"
#include "UI/Icons.hpp"
#include "UI/PlatformTag.hpp"
#include "UI/RichText.hpp"
#include "UI/ViewerCountView.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/delegates.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"

#include "HMUI/CurvedCanvasSettings.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/UI/CanvasScaler.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/Vector4.hpp"
#include "UnityEngine/EventSystems/BaseInputModule.hpp"
#include "UnityEngine/EventSystems/EventSystem.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Time.hpp"
#include "VRUIControls/VRInputModule.hpp"
#include "VRUIControls/VRPointer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutRebuilder.hpp"
#include "UnityEngine/UI/LayoutUtility.hpp"
#include "UnityEngine/UI/RectMask2D.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "UnityEngine/Events/UnityAction.hpp"
#include "UnityEngine/Vector3.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "Compat.hpp"
#include "TMPro/FontStyles.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <span>

DEFINE_TYPE(StandaloneStreamChat, ChatPanel);

namespace {
    using namespace StandaloneStreamChat;
    using Chat::ChatMessage;
    using Chat::ChatPlatform;

    // Only the newest messages can fit on the panel; cap the rows kept.
    constexpr size_t MaxRenderedMessages = 40;
    constexpr float RowSpacing = 0.6f;

    using UI::Escape;

    UnityW<ChatPanel> instance;

    // Name colors come from IRC tags; only accept plain #RRGGBB.
    std::string SafeColor(ChatMessage const& m) {
        auto const& c = m.color;
        bool valid = c.size() == 7 && c[0] == '#';
        for (size_t i = 1; valid && i < c.size(); i++)
            valid = std::isxdigit(static_cast<unsigned char>(c[i]));
        return valid ? c : Chat::NameColorFor(m.author);
    }

    std::string_view IconName(ChatPlatform platform) {
        switch (platform) {
            case ChatPlatform::Twitch:
                return "twitch";
            case ChatPlatform::YouTube:
                return "youtube";
            case ChatPlatform::Kick:
                return "kick";
            case ChatPlatform::System:
                break;
        }
        return "";
    }

    // Appends chat text, escaped, with any BTTV/FFZ/7TV emote words (whole,
    // space-separated, exact case) swapped for their images once loaded.
    void AppendText(std::string& line, std::string_view text, Chat::ThirdPartyEmotes::EmoteMap const& extra, bool& waiting) {
        if (extra.empty()) {
            line += Escape(text);
            return;
        }
        auto& atlas = UI::EmoteAtlas::Instance();
        size_t written = 0;  // text before this has been appended
        for (size_t start = 0; start < text.size();) {
            if (text[start] == ' ') {
                start++;
                continue;
            }
            size_t end = std::min(text.find(' ', start), text.size());
            auto word = text.substr(start, end - start);
            if (auto it = extra.find(std::string(word)); it != extra.end()) {
                std::string tag;
                auto status = atlas.Append(tag, {std::string(word), it->second.key, it->second.url, it->second.animatedUrl});
                if (status == UI::EmoteAtlas::Status::Ready) {
                    if (start > written)
                        line += Escape(text.substr(written, start - written));
                    line += tag;
                    written = end;
                } else {
                    waiting |= status == UI::EmoteAtlas::Status::Loading;
                }
            }
            start = end;
        }
        if (written < text.size())
            line += Escape(text.substr(written));
    }

    // A message's rich text for its row. Chat text is escaped; icons and
    // emotes are sprite tags from the emote atlas. `waiting` comes back true
    // when an emote image is still downloading (its name shows meanwhile).
    std::string RowText(ChatMessage const& m, bool& waiting) {
        waiting = false;
        if (m.platform == ChatPlatform::System)
            return "<color=#AAAAAA><i>" + Escape(m.text) + "</i></color>";

        auto const& config = GetConfig();
        auto& atlas = UI::EmoteAtlas::Instance();
        std::string line;
        if (config.showPlatformIcons) {
            // [T]/[Y]/[K] and [B]/[M] text tags stand in for any icon that failed to load.
            auto platform = atlas.IconTag(IconName(m.platform));
            line += platform.empty() ? std::string(UI::PlatformTag(m.platform)) : platform + " ";
            if (!m.badges.empty()) {
                auto badges = Chat::ThirdPartyEmotes::Instance().Badges();
                for (auto const& badge : m.badges) {
                    auto url = badge.imageUrl;
                    if (url.empty() && m.platform == ChatPlatform::Twitch) {
                        auto found = badges->find(badge.id + "/" + badge.version);
                        if (found != badges->end()) url = found->second;
                    }
                    if (!url.empty()) {
                        auto key = "badge:" + std::to_string(static_cast<int>(m.platform)) + ":" + Chat::StableKey(url);
                        auto status = atlas.Append(line, {badge.label, key, url});
                        if (status == UI::EmoteAtlas::Status::Ready) { line += " "; continue; }
                        waiting |= status == UI::EmoteAtlas::Status::Loading;
                    }
                    auto builtin = badge.id == "broadcaster" || badge.id == "OWNER" ? "broadcaster"
                                 : badge.id == "moderator" || badge.id == "MODERATOR" ? "moderator" : "";
                    auto tag = atlas.IconTag(builtin);
                    line += tag.empty() ? "[" + Escape(badge.label.empty() ? badge.id : badge.label) + "] " : tag + " ";
                }
            } else if (m.isBroadcaster || m.isModerator) {
                auto badge = atlas.IconTag(m.isBroadcaster ? "broadcaster" : "moderator");
                if (!badge.empty())
                    line += badge + " ";
                else
                    line += m.isBroadcaster ? "<color=#E91916>[B]</color> " : "<color=#00AD03>[M]</color> ";
            }
        }

        auto color = SafeColor(m);
        line += "<color=" + color + "><b>" + Escape(m.author) + "</b></color>";
        line += m.isAction ? " <color=" + color + "><i>" : ": ";
        if (m.isDeleted)
            return line + "<color=#AAAAAA><i>[message deleted]</i></color>";
        if (m.bits)
            line += "<color=#BA84FF><b>" + std::to_string(m.bits) + " Bits</b></color> ";
        static Chat::ThirdPartyEmotes::EmoteMap const none;
        auto extraSet = config.emojiImages ? Chat::ThirdPartyEmotes::Instance().Snapshot() : nullptr;
        auto const& extra = extraSet ? *extraSet : none;
        if (m.segments.empty()) {
            AppendText(line, m.text, extra, waiting);
        } else {
            for (auto const& segment : m.segments) {
                if (!segment.IsEmote()) {
                    AppendText(line, segment.text, extra, waiting);
                    continue;
                }
                if (config.emojiImages) {
                    auto status = atlas.Append(line, segment);
                    if (status == UI::EmoteAtlas::Status::Ready)
                        continue;
                    waiting |= status == UI::EmoteAtlas::Status::Loading;
                }
                line += Escape(segment.text);
            }
        }
        if (m.isAction)
            line += "</i></color>";
        return line;
    }

    float NormalizeAngle(float degrees) { return degrees > 180.0f ? degrees - 360.0f : degrees; }

    // BSML's floating screens draw their canvas at this many pixels per unit.
    constexpr float FallbackReferencePixelsPerUnit = 10.0f;

    // Read from the canvas scaler BSML configures, not the canvas: the scaler
    // only hands its value to the canvas on its next update, so right after
    // the screen is made the canvas still has Unity's default of 100, which
    // drew the corners a tenth of their size until something re-applied them.
    float ReferencePixelsPerUnit(HMUI::ImageView* image) {
        auto scaler = image->GetComponentInParent<UnityEngine::UI::CanvasScaler*>();
        float reference = scaler ? scaler->get_referencePixelsPerUnit() : 0.0f;
        return reference > 0.0f ? reference : FallbackReferencePixelsPerUnit;
    }

    // The corner size BSML's own backdrop sprite draws, in panel units, so
    // the Corners setting starts out matching the panel's old look.
    float BsmlCornerRadius(HMUI::ImageView* background) {
        constexpr float Fallback = 3.0f;
        auto sprite = background->get_sprite();
        if (!sprite || sprite->get_pixelsPerUnit() <= 0.0f)
            return Fallback;
        float pixelsPerUnit = sprite->get_pixelsPerUnit() / ReferencePixelsPerUnit(background) * background->get_pixelsPerUnitMultiplier();
        float radius = pixelsPerUnit > 0.0f ? sprite->get_border().x / pixelsPerUnit : 0.0f;
        INFO("Panel backdrop {}: border {} px at {} px/unit = {} unit corners", static_cast<std::string>(sprite->get_name()), sprite->get_border().x,
             pixelsPerUnit, radius);
        if (radius <= 0.0f)
            return Fallback;
        return std::clamp(std::round(radius * 2.0f) / 2.0f, 0.5f, 10.0f);
    }

    void SavePanelTransform(UnityEngine::Transform* transform) {
        auto& config = GetConfig();
        auto position = transform->get_position();
        config.positionX = position.x;
        config.positionY = position.y;
        config.positionZ = position.z;
        auto euler = transform->get_eulerAngles();
        config.rotationX = NormalizeAngle(euler.x);
        config.rotationY = NormalizeAngle(euler.y);
        SaveConfig();
    }

    // Side button column, in panel units.
    constexpr float SideButtonSize = 7.0f;
    constexpr float SideButtonGap = 1.2f;
    constexpr float SideButtonTop = -2.4f;

    // Viewer strip in a band across the top; the feed starts below it.
    constexpr float ViewerStripTopInset = 2.3f;
    constexpr float ViewerStripHeight = 8.0f;
    constexpr float ViewerStripRightInset = 5.0f;
    constexpr float ContentTopGap = 1.0f;
    constexpr float ContentInset = 2.0f;  // feed margin when there's no strip
    constexpr ChatPlatform ViewerPlatforms[] = {ChatPlatform::Twitch, ChatPlatform::YouTube, ChatPlatform::Kick};

    // "Requests (N)" sits at the left of the top band, clear of the counts.
    constexpr float RequestsLabelLeftInset = 3.0f;
    constexpr float RequestsLabelFontSize = 3.6f;
    UnityEngine::Color const RequestsModeTint(1.0f, 0.82f, 0.29f, 1.0f);

    // A platform gets a count only if it would actually connect.
    bool PlatformActive(Config const& config, ChatPlatform platform) {
        switch (platform) {
            case ChatPlatform::Twitch:
                return config.enableTwitch && !config.twitchChannel.empty();
            case ChatPlatform::YouTube:
                return config.enableYouTube && !config.youTubeChannel.empty();
            case ChatPlatform::Kick:
                return config.enableKick && !config.kickChannel.empty();
            case ChatPlatform::System:
                break;
        }
        return false;
    }
}

namespace StandaloneStreamChat {
    void ChatPanel::EnsureCreated() {
        if (instance)
            return;

        // No BSML handle: the panel moves itself (see OnPointerDown), and the
        // handle's collider would only stop the pointer reaching the backdrop.
        auto const& config = GetConfig();
        auto screen = BSML::Lite::CreateFloatingScreen(
            {config.panelWidth, config.panelHeight},
            {config.positionX, config.positionY, config.positionZ},
            {config.rotationX, config.rotationY, 0.0f},
            0.0f, true, false);
        auto screenGo = screen->get_gameObject();
        screenGo->set_name("StandaloneStreamChatPanel");
        UnityEngine::Object::DontDestroyOnLoad(screenGo);

        // BSML clips the whole screen to its rect, which would hide (and block
        // clicks to) the button column outside the left edge. The feed has its
        // own mask (ChatContent below), so the screen-wide one isn't needed.
        if (auto screenMask = screenGo->GetComponent<UnityEngine::UI::RectMask2D*>())
            screenMask->set_enabled(false);

        // BSML's backdrop is the only ImageView so far. Its stock material
        // mostly ignores vertex color, so swap in NoGlow to make the Color
        // setting show. It is also the grab surface:
        // ApplyLockState makes it catch the pointer only while unlocked.
        auto background = screenGo->GetComponentInChildren<HMUI::ImageView*>(true);
        if (background) {
            background->set_material(BSML::Helpers::GetUINoGlowMat());
            // From here on the panel draws its own rounded backdrop (Corners
            // setting); the first time, start it at BSML's rounding.
            if (GetConfig().cornerRadius < 0.0f) {
                GetConfig().cornerRadius = BsmlCornerRadius(background);
                SaveConfig();
            }
        }

        // Clip region for the feed; its top edge moves down when the viewer
        // strip is shown (RefreshViewerStrip).
        auto maskGo = UnityEngine::GameObject::New_ctor("ChatContent");
        auto maskRect = maskGo->AddComponent<UnityEngine::RectTransform*>();
        maskRect->SetParent(screen->get_transform(), false);
        maskRect->set_anchorMin({0.0f, 0.0f});
        maskRect->set_anchorMax({1.0f, 1.0f});
        maskRect->set_offsetMin({3.0f, ContentInset});
        maskRect->set_offsetMax({-3.0f, -ContentInset});
        maskGo->AddComponent<UnityEngine::UI::RectMask2D*>();

        // Viewer strip: [icon count] per platform, packed right in the top band.
        auto stripGo = UnityEngine::GameObject::New_ctor("ViewerCounts");
        auto strip = stripGo->AddComponent<UnityEngine::RectTransform*>();
        strip->SetParent(screen->get_transform(), false);
        strip->set_anchorMin({0.0f, 1.0f});
        strip->set_anchorMax({1.0f, 1.0f});
        strip->set_pivot({0.5f, 1.0f});
        strip->set_offsetMin({3.0f, -(ViewerStripTopInset + ViewerStripHeight)});
        strip->set_offsetMax({-ViewerStripRightInset, -ViewerStripTopInset});
        auto stripLayout = stripGo->AddComponent<UnityEngine::UI::HorizontalLayoutGroup*>();
        stripLayout->set_childAlignment(UnityEngine::TextAnchor::MiddleRight);
        stripLayout->set_childControlWidth(true);
        stripLayout->set_childControlHeight(true);
        stripLayout->set_childForceExpandWidth(false);
        stripLayout->set_childForceExpandHeight(false);
        stripLayout->set_spacing(3.0f);  // the gap between platforms

        // The feed: rows stacked upward from the bottom edge, growing past the
        // top of the mask as messages arrive.
        auto rowsGo = UnityEngine::GameObject::New_ctor("Rows");
        auto rows = rowsGo->AddComponent<UnityEngine::RectTransform*>();
        rows->SetParent(maskRect, false);
        rows->set_anchorMin({0.0f, 0.0f});
        rows->set_anchorMax({1.0f, 0.0f});
        rows->set_pivot({0.5f, 0.0f});
        rows->set_offsetMin({0.0f, 0.0f});
        rows->set_offsetMax({0.0f, 0.0f});
        auto rowsLayout = rowsGo->AddComponent<UnityEngine::UI::VerticalLayoutGroup*>();
        rowsLayout->set_childAlignment(UnityEngine::TextAnchor::LowerLeft);
        rowsLayout->set_childControlWidth(true);
        rowsLayout->set_childControlHeight(true);
        rowsLayout->set_childForceExpandWidth(true);
        rowsLayout->set_childForceExpandHeight(false);
        rowsLayout->set_spacing(RowSpacing);
        rowsGo->AddComponent<UnityEngine::UI::ContentSizeFitter*>()->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::PreferredSize);

        auto panel = screenGo->AddComponent<ChatPanel*>();
        panel->_screen = screen;
        panel->_background = background;
        panel->_rows = rows;
        panel->_rerenderRows = true;
        panel->_content = maskRect;
        panel->_viewerStrip = strip;
        panel->_twitchViewers = ViewerCountView::Create(strip, ChatPlatform::Twitch);
        panel->_youTubeViewers = ViewerCountView::Create(strip, ChatPlatform::YouTube);
        panel->_kickViewers = ViewerCountView::Create(strip, ChatPlatform::Kick);
        panel->_lastVersion = -1;
        instance = panel;

        // Buttons look `instance` up when clicked, so a recreated panel never
        // runs a stale one.
        panel->_lockIcon = panel->CreateSideButton("LockButton", 0, [] {
            if (instance)
                instance->ToggleLock();
        });
        panel->_moveModeIcon = panel->CreateSideButton("MoveModeButton", 1, [] {
            if (instance)
                instance->ToggleMoveMode();
        });
        panel->_requestsIcon = panel->CreateSideButton("RequestsButton", 2, [] {
            if (instance)
                instance->ToggleRequestsMode();
        });

        auto screenRect = screen->get_transform().cast<UnityEngine::RectTransform>();
        panel->_requestList = RequestList::Create(maskRect, screenRect, background);
        auto label = BSML::Lite::CreateText(screen->get_transform(), "", TMPro::FontStyles::Normal, RequestsLabelFontSize);
        label->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
        SetWordWrap(label, false);
        label->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        label->set_raycastTarget(false);
        auto labelRect = label->get_rectTransform();
        labelRect->set_anchorMin({0.0f, 1.0f});
        labelRect->set_anchorMax({1.0f, 1.0f});
        labelRect->set_pivot({0.5f, 1.0f});
        labelRect->set_offsetMin({RequestsLabelLeftInset, -(ViewerStripTopInset + ViewerStripHeight)});
        labelRect->set_offsetMax({-ViewerStripRightInset, -ViewerStripTopInset});
        label->get_gameObject()->SetActive(false);
        panel->_requestsLabel = label;
        panel->_requestsMode = false;
        panel->_unreadRequests = Requests::SongRequestManager::Instance().Count() > 0;
        panel->_requestsLabelVersion = -1;
        panel->UpdateRequestsButton();

        panel->ApplyLockState();
        ApplyConfig();
        INFO("Chat panel created");
    }

    HMUI::ImageView* ChatPanel::CreateSideButton(StringW name, int slot, std::function<void()> onClick) {
        // Anchored to the panel's top-left corner, right edge one gap outside it.
        auto go = UnityEngine::GameObject::New_ctor(name);
        auto rect = go->AddComponent<UnityEngine::RectTransform*>();
        rect->SetParent(_screen->get_transform(), false);
        rect->set_anchorMin({0.0f, 1.0f});
        rect->set_anchorMax({0.0f, 1.0f});
        rect->set_pivot({1.0f, 1.0f});
        rect->set_anchoredPosition({-SideButtonGap, SideButtonTop - slot * (SideButtonSize + SideButtonGap)});
        rect->set_sizeDelta({SideButtonSize, SideButtonSize});

        auto icon = go->AddComponent<HMUI::ImageView*>();
        icon->set_material(BSML::Helpers::GetUINoGlowMat());
        icon->set_preserveAspect(true);
        icon->set_raycastTarget(true);

        auto button = go->AddComponent<UnityEngine::UI::Button*>();
        button->set_targetGraphic(icon);
        button->get_onClick()->AddListener(BSML::MakeUnityAction(std::move(onClick)));
        return icon;
    }

    // Gold open padlock = movable, dim closed padlock =
    // pinned; the move-mode arrows dim while locked.
    void ChatPanel::ApplyLockState() {
        auto const& config = GetConfig();
        // Unlocked, the backdrop catches the pointer and a press grabs the
        // panel; locked, the pointer passes straight through it.
        if (_background)
            _background->set_raycastTarget(config.panelUnlocked);
        if (!config.panelUnlocked)
            EndGrab();

        _lockIcon->set_sprite(config.panelUnlocked ? UI::Icons::Unlock() : UI::Icons::Lock());
        _lockIcon->set_color(config.panelUnlocked ? UnityEngine::Color(1.0f, 0.85f, 0.3f, 0.95f) : UnityEngine::Color(1.0f, 1.0f, 1.0f, 0.5f));
        _moveModeIcon->set_sprite(UI::Icons::Move(config.panelMoveMode));
        _moveModeIcon->set_color(UnityEngine::Color(0.35f, 0.85f, 1.0f, config.panelUnlocked ? 0.9f : 0.45f));
    }

    void ChatPanel::ToggleLock() {
        auto& config = GetConfig();
        config.panelUnlocked = !config.panelUnlocked;
        SaveConfig();
        ApplyLockState();
    }

    void ChatPanel::ToggleMoveMode() {
        auto& config = GetConfig();
        config.panelMoveMode = (config.panelMoveMode + 1) % 4;
        SaveConfig();
        ApplyLockState();
    }

    // The feed freezes while the request list shows and catches up after.
    void ChatPanel::ToggleRequestsMode() {
        _requestsMode = !_requestsMode;
        if (_requestsMode)
            _unreadRequests = false;
        _rows->get_gameObject()->SetActive(!_requestsMode);
        _requestList->get_gameObject()->SetActive(_requestsMode);
        _requestsLabel->get_gameObject()->SetActive(_requestsMode);
        _requestsLabelVersion = -1;
        _lastVersion = -1;
        _rerenderRows = true;
        if (_requestsMode) UI::EmoteAtlas::Instance().RetainRows({});
        UpdateRequestsButton();
        RefreshViewerStrip();  // the label needs the top band even without counts
    }

    // The request-list icon (marked when there are unseen requests) in chat
    // mode; the gold chat icon in Requests mode.
    void ChatPanel::UpdateRequestsButton() {
        _requestsIcon->set_sprite(_requestsMode ? UI::Icons::ChatMode() : UI::Icons::Requests(_unreadRequests));
        _requestsIcon->set_color(_requestsMode ? RequestsModeTint : UnityEngine::Color(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // Reaches us by bubbling up from the backdrop (the buttons handle their own
    // presses), so any press on the panel area while unlocked starts a drag.
    // Same grab math as BSML's FloatingScreenHandle.
    void ChatPanel::OnPointerDown(UnityEngine::EventSystems::PointerEventData*) {
        if (!GetConfig().panelUnlocked)
            return;
        auto eventSystem = UnityEngine::EventSystems::EventSystem::get_current();
        if (!eventSystem)
            return;
        auto inputModule = TryCast<VRUIControls::VRInputModule>(eventSystem->get_currentInputModule());
        if (!inputModule)
            return;
        auto pointer = inputModule->_vrPointer;
        auto controller = pointer ? pointer->_lastSelectedVrController : nullptr;
        if (!controller)
            return;

        auto anchor = controller->_viewAnchorTransform;
        auto transform = _screen->get_transform();
        _grabController = controller;
        _grabLocalPosition = anchor->InverseTransformPoint(transform->get_position());
        _grabLocalRotation = UnityEngine::Quaternion::op_Multiply(UnityEngine::Quaternion::Inverse(anchor->get_rotation()), transform->get_rotation());
        _grabStartPosition = transform->get_position();
        _grabStartRotation = transform->get_rotation();
    }

    void ChatPanel::OnPointerUp(UnityEngine::EventSystems::PointerEventData*) { EndGrab(); }

    void ChatPanel::EndGrab() {
        if (!_grabController)
            return;
        _grabController = nullptr;
        ApplyMoveConstraint();
        SavePanelTransform(_screen->get_transform());
    }

    // Eases toward where the controller is carrying the panel; the thumbstick
    // pushes it away or pulls it closer, as with BSML's handle.
    void ChatPanel::FollowController() {
        auto anchor = _grabController->_viewAnchorTransform;
        if (!anchor) {
            EndGrab();  // the controller went away mid-drag (scene change)
            return;
        }
        float dt = UnityEngine::Time::get_unscaledDeltaTime();
        _grabLocalPosition = UnityEngine::Vector3::op_Subtraction(
            _grabLocalPosition, UnityEngine::Vector3::op_Multiply(UnityEngine::Vector3::get_forward(), -_grabController->get_thumbstick().y * dt));
        auto targetPosition = anchor->TransformPoint(_grabLocalPosition);
        auto targetRotation = UnityEngine::Quaternion::op_Multiply(anchor->get_rotation(), _grabLocalRotation);

        auto transform = _screen->get_transform();
        transform->SetPositionAndRotation(UnityEngine::Vector3::Lerp(transform->get_position(), targetPosition, 10.0f * dt),
                                          UnityEngine::Quaternion::Lerp(transform->get_rotation(), targetRotation, 5.0f * dt));
        ApplyMoveConstraint();
    }

    void ChatPanel::ApplyMoveConstraint() {
        int mode = GetConfig().panelMoveMode;
        if (mode == 0)
            return;
        auto transform = _screen->get_transform();
        auto position = transform->get_position();
        auto const& start = _grabStartPosition;
        if (mode == 1)
            position = {position.x, start.y, start.z};
        else if (mode == 2)
            position = {start.x, position.y, start.z};
        else
            position = {start.x, start.y, position.z};
        // Keep the starting angle too, so sliding along an axis doesn't also tilt the panel.
        transform->SetPositionAndRotation(position, _grabStartRotation);
    }

    void ChatPanel::ApplyConfig() {
        if (!instance)
            return;
        auto const& config = GetConfig();
        auto panel = instance.ptr();
        UI::EmoteAtlas::Instance().ConfigureAnimations(config.animatedEmotes && config.emojiImages);
        if (!config.showPanel) UI::EmoteAtlas::Instance().RetainRows({});

        panel->_screen->set_ScreenSize({config.panelWidth, config.panelHeight});
        if (panel->_background) {
            auto tint = UI::ColorFromHex(config.backgroundColor, UnityEngine::Color(0.0f, 0.0f, 0.0f, 1.0f));
            tint.a = std::clamp(config.backgroundOpacity, 0.0f, 1.0f);
            panel->_background->set_color(tint);

            // Square corners are a plain white pixel; rounded ones a sliced sprite.
            auto rounded = UI::Icons::RoundedPanel(std::max(0.0f, config.cornerRadius), ReferencePixelsPerUnit(panel->_background));
            panel->_background->set_sprite(rounded ? rounded : BSML::Utilities::ImageResources::GetWhitePixel());
            panel->_background->set_type(rounded ? UnityEngine::UI::Image::Type::Sliced : UnityEngine::UI::Image::Type::Simple);
            panel->_background->set_pixelsPerUnitMultiplier(1.0f);
            panel->_background->set_preserveAspect(false);
        }

        bool curved = config.curveAmount > 0.0f;
        if (auto canvas = panel->_screen->GetComponentInChildren<HMUI::CurvedCanvasSettings*>(true))
            canvas->SetRadius(curved ? 1200.0f / config.curveAmount : 0.0f);

        // Rebuild every row next frame: font, curve, highlight and icon
        // settings all change how a row is drawn.
        panel->_rerenderRows = true;
        panel->_lastVersion = -1;
        auto textMaterial = panel->TextMaterial();
        panel->_requestList->ApplyConfig(textMaterial);
        UnityEngine::Material* labelMaterial = panel->_requestsLabel->get_fontSharedMaterial();
        if (textMaterial && labelMaterial != textMaterial)
            panel->_requestsLabel->set_fontSharedMaterial(textMaterial);
        panel->RefreshViewerStrip();
        if (!config.showPanel)
            panel->EndGrab();  // a hidden panel never gets the release
        panel->get_gameObject()->SetActive(config.showPanel);
    }

    void ChatPanel::RefreshViewerStrip() {
        _nextViewerRefresh = UnityEngine::Time::get_unscaledTime() + 1.0f;
        auto const& config = GetConfig();
        bool anyActive = false;
        for (auto platform : ViewerPlatforms)
            anyActive |= PlatformActive(config, platform);
        bool show = config.showViewerCounts && anyActive;

        _viewerStrip->get_gameObject()->SetActive(show);
        // Requests mode keeps the band for its label even without counts.
        float top = show || _requestsMode ? ViewerStripTopInset + ViewerStripHeight + ContentTopGap : ContentInset;
        _content->set_offsetMax({-3.0f, -top});

        if (show) {
            auto& chat = Chat::ChatManager::Instance();
            auto textMaterial = TextMaterial();
            for (auto [platform, view] : {std::pair{ChatPlatform::Twitch, _twitchViewers}, std::pair{ChatPlatform::YouTube, _youTubeViewers},
                                          std::pair{ChatPlatform::Kick, _kickViewers}}) {
                bool active = PlatformActive(config, platform);
                view->get_gameObject()->SetActive(active);
                if (active)
                    view->Show(chat.Viewers(platform), config.showPlatformIcons, config.viewerCountSize, textMaterial);
            }
        }

        // "Requests (N)" stops short of the counts.
        if (_requestsMode) {
            float rightInset = ViewerStripRightInset;
            if (show) {
                UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(_viewerStrip);
                rightInset += UnityEngine::UI::LayoutUtility::GetPreferredWidth(_viewerStrip) + 1.0f;
            }
            _requestsLabel->get_rectTransform()->set_offsetMax({-rightInset, -ViewerStripTopInset});
        }
    }

    UnityEngine::Material* ChatPanel::TextMaterial() {
        if (!_curvedTextMaterial)
            _curvedTextMaterial = BSML::Helpers::GetMainUIFontMaterial();
        if (!_flatTextMaterial && _curvedTextMaterial) {
            _flatTextMaterial = UnityEngine::Material::New_ctor(_curvedTextMaterial);
            _flatTextMaterial->set_name(_curvedTextMaterial->get_name() + " (StandaloneStreamChat Flat)");
            _flatTextMaterial->DisableKeyword("CURVED");
        }
        return GetConfig().curveAmount > 0.0f ? _curvedTextMaterial : _flatTextMaterial;
    }

    void ChatPanel::SyncRows(std::vector<ChatMessage> const& messages) {
        size_t first = messages.size() > MaxRenderedMessages ? messages.size() - MaxRenderedMessages : 0;
        std::span<ChatMessage const> target(messages.begin() + first, messages.end());
        std::vector<uint64_t> rowIds;
        for (auto const& message : target) rowIds.push_back(message.sequence);
        UI::EmoteAtlas::Instance().RetainRows(rowIds);

        std::vector<ChatRow*> active, spare;
        for (int i = 0, count = _rows->get_childCount(); i < count; i++) {
            auto row = _rows->GetChild(i)->GetComponent<ChatRow*>();
            (row->get_gameObject()->get_activeSelf() ? active : spare).push_back(row);
        }

        // Usually the rows already show a run of `target` and only messages
        // after it are new: keep those rows and recycle the ones scrolled away.
        // Anything else (a setting changed, a channel's messages cleared)
        // rebuilds every row.
        size_t dropped = active.size();
        if (!_rerenderRows && !active.empty() && !target.empty()) {
            auto firstKept = std::find_if(active.begin(), active.end(),
                                          [&](ChatRow* row) { return row->_sequence == static_cast<int64_t>(target.front().sequence); });
            size_t start = firstKept - active.begin();
            size_t kept = active.size() - start;
            bool matches = kept <= target.size();
            for (size_t i = 0; matches && i < kept; i++)
                matches = active[start + i]->_sequence == static_cast<int64_t>(target[i].sequence) &&
                          active[start + i]->_revision == static_cast<int64_t>(target[i].revision);
            if (matches)
                dropped = start;
        }
        _rerenderRows = false;

        std::deque<ChatRow*> reusable(active.begin(), active.begin() + dropped);
        for (size_t i = active.size() - dropped; i < target.size(); i++) {
            ChatRow* row;
            if (!reusable.empty()) {
                row = reusable.front();
                reusable.pop_front();
            } else if (!spare.empty()) {
                row = spare.back();
                spare.pop_back();
                row->get_gameObject()->SetActive(true);
            } else {
                row = ChatRow::Create(_rows);
            }
            row->get_transform()->SetAsLastSibling();
            RenderRow(row, target[i]);
        }
        for (auto row : reusable)
            row->get_gameObject()->SetActive(false);
        UI::EmoteAtlas::Instance().RetainRows(rowIds);
    }

    void ChatPanel::RenderRow(ChatRow* row, ChatMessage const& message) {
        auto const& config = GetConfig();
        bool waiting = false;
        auto& atlas = UI::EmoteAtlas::Instance();
        atlas.BeginRow(message.sequence);
        auto text = RowText(message, waiting);
        atlas.EndRow();

        auto label = row->_text;
        label->set_spriteAsset(UI::EmoteAtlas::Instance().SpriteAsset());
        UnityEngine::Material* current = label->get_fontSharedMaterial();
        if (auto material = TextMaterial(); material && current != material)
            label->set_fontSharedMaterial(material);
        label->set_fontSize(config.fontSize);
        label->set_text(text);

        // Highlight Messages: a dark band behind the whole message.
        row->_background->set_enabled(config.highlightMessages || message.isHighlighted || message.isMention);
        auto tint = message.isMention ? UnityEngine::Color(0.55f, 0.12f, 0.20f, 0.35f)
                  : message.isHighlighted ? UnityEngine::Color(0.35f, 0.16f, 0.55f, 0.35f)
                                          : UnityEngine::Color(0.0f, 0.0f, 0.0f, std::clamp(config.highlightOpacity, 0.0f, 1.0f));
        row->_background->set_color(tint);
        row->_sequence = static_cast<int64_t>(message.sequence);
        row->_revision = static_cast<int64_t>(message.revision);
        row->_waitingForEmotes = waiting;
    }

    void ChatPanel::Recenter() {
        auto& config = GetConfig();
        config.positionX = DefaultPositionX;
        config.positionY = DefaultPositionY;
        config.positionZ = DefaultPositionZ;
        config.rotationX = DefaultRotationX;
        config.rotationY = DefaultRotationY;
        SaveConfig();
        if (!instance)
            return;
        instance->_screen->set_ScreenPosition({config.positionX, config.positionY, config.positionZ});
        instance->_screen->set_ScreenRotation(UnityEngine::Quaternion::Euler(config.rotationX, config.rotationY, 0.0f));
    }

    void ChatPanel::Update() {
        if (_grabController)
            FollowController();
        if (UnityEngine::Time::get_unscaledTime() >= _nextViewerRefresh)
            RefreshViewerStrip();

        auto& requests = Requests::SongRequestManager::Instance();
        if (requests.TakeAdded() && !_requestsMode) {
            _unreadRequests = true;
            UpdateRequestsButton();
        }
        // Back to the feed once an accepted map opens.
        if (requests.TakeOpened() && _requestsMode)
            ToggleRequestsMode();
        if (_requestsMode) {
            // The list updates itself; only the count in the label is ours.
            if (int version = requests.Version(); version != _requestsLabelVersion) {
                _requestsLabelVersion = version;
                _requestsLabel->set_text(fmt::format("Requests ({})", requests.Count()));
            }
            return;
        }

        // A newly loaded BTTV/FFZ/7TV set can turn words in every row into emotes.
        auto& extraEmotes = Chat::ThirdPartyEmotes::Instance();
        auto& chat = Chat::ChatManager::Instance();
        extraEmotes.EnsureLoaded(GetConfig().twitchChannel, GetConfig().emojiImages, chat.ChannelId(ChatPlatform::Twitch), GetConfig().showPlatformIcons);
        if (extraEmotes.Version() != _emoteSetVersion) {
            _emoteSetVersion = extraEmotes.Version();
            _rerenderRows = true;
            _lastVersion = -1;
        }

        auto version = chat.Version();
        std::vector<ChatMessage> messages;
        if (version != _lastVersion) {
            messages = chat.Snapshot();
            _lastVersion = version;
            SyncRows(messages);
        }
        bool newSprites = UI::EmoteAtlas::Instance().Pump();
        if (newSprites) {
            if (messages.empty()) messages = chat.Snapshot();
            // Swap emote names for the images that just arrived.
            for (int i = 0, count = _rows->get_childCount(); i < count; i++) {
                auto row = _rows->GetChild(i)->GetComponent<ChatRow*>();
                if (!row->_waitingForEmotes || !row->get_gameObject()->get_activeSelf())
                    continue;
                auto it = std::find_if(messages.begin(), messages.end(),
                                       [row](ChatMessage const& m) { return static_cast<int64_t>(m.sequence) == row->_sequence; });
                if (it != messages.end())
                    RenderRow(row, *it);
            }
        }
    }
}
