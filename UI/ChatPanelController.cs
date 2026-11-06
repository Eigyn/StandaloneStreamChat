using System.Collections.Generic;
using System.Text;
using System.Threading;
using BeatSaberMarkupLanguage;
using BeatSaberMarkupLanguage.FloatingScreen;
using BeatSaberMarkupLanguage.MenuButtons;
using BeatSaberMarkupLanguage.Util;
using SimpleBSStreamChat.Chat;
using SimpleBSStreamChat.Chat.Emotes;
using SimpleBSStreamChat.Chat.Requests;
using TMPro;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.SceneManagement;
using UnityEngine.UI;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.UI
{
    /// <summary>
    /// Floating chat panel built from pooled fixed-height rows on a uniform
    /// grid. Each message is word-wrapped once by an off-screen measuring
    /// text, then every wrapped line becomes a real row of its own — own
    /// background, own slot, text vertically centered — so a message that
    /// needs two lines occupies two actual rows. The visible-row budget is
    /// derived from the panel's local height, and rows that don't fully fit
    /// the panel are hidden whole
    /// instead of rendering cut off or overlapping the viewer counts.
    /// </summary>
    public class ChatPanelController : MonoBehaviour
    {
        private class ChatRow
        {
            public GameObject Root = null!;
            public RectTransform Rect = null!;
            public HMUI.ImageView Background = null!;
            public TextMeshProUGUI Text = null!;
            public CanvasGroup Group = null!;
            public LayoutElement Layout = null!;
            public HMUI.ImageView PlatformIcon = null!;
            public LayoutElement PlatformIconLayout = null!;
            public HMUI.ImageView BadgeIcon = null!;
            public LayoutElement BadgeIconLayout = null!;
        }

        private class WrappedMessage
        {
            public ChatMessage Message = null!;
            public float FontSize;
            public float MeasureWidth;
            public bool CanSplit;
            public bool ShowTextTags;
            public bool EmojiImages;
            public bool CustomEmotes;
            public int ThirdPartyVersion;
            public int IconCount;
            public string[] Lines = null!;
        }

        private readonly struct PendingChatRow
        {
            public PendingChatRow(string text, Sprite? platformIcon, Sprite? badgeIcon, ChatMessage source)
            {
                Text = text;
                PlatformIcon = platformIcon;
                BadgeIcon = badgeIcon;
                Source = source;
            }

            public string Text { get; }
            public Sprite? PlatformIcon { get; }
            public Sprite? BadgeIcon { get; }
            public ChatMessage Source { get; }   // owning message, for visible-emote marking
        }

        // Gap between row children (icons/text); also baked into the width
        // reserved in the measurer so wrap points match the rendered rows.
        private const float IconSpacing = 0.6f;
        private const float ChatRowSpacing = 0.6f;
        private const float CornerButtonSize = 7f;
        private const float CornerButtonX = 2f;
        private const float CornerButtonY = -2.4f;
        private const float CornerButtonGap = 1.2f;
        private const float ViewerStripRightInset = 5f;
        private const float ViewerStripTopInset = 2.3f;
        private const float ViewerStripHeight = 8f;
        private const float ContentTopGap = 1f;
        internal const float ContentBottomInset = 3f;
        internal const float ContentTopInset = ViewerStripTopInset + ViewerStripHeight + ContentTopGap;
        private const float StatusLabelLeftInset = 35f;
        private const float StatusLabelRightInset = 5f;
        // Top-bar "Requests (N)" / status pop-in text; sized to read clearly
        // (a touch above the chat font), not the tiny counter size.
        private const float StatusLabelFontSize = 3.6f;
        private const float StatusFlashDuration = 1.8f;
        private const float StatusFadeDuration = 0.25f;
        private const int TrimFramesAfterRebuild = 3;
        private const int MoveModeFree = 0;
        private const int MoveModeX = 1;
        private const int MoveModeY = 2;
        private const int MoveModeZ = 3;
        private const int MoveModeCount = 4;
        private static readonly Vector3 FloorMarkerPosition = Vector3.zero;

        // One platform's [icon count] pair in the viewer strip.
        private class ViewerGroup
        {
            public HMUI.ImageView Icon = null!;
            public LayoutElement IconLayout = null!;
            public TextMeshProUGUI Text = null!;
        }

        private ChatManager? _chatManager;
        private PluginConfig? _config;
        private IPALogger? _log;

        private FloatingScreen? _screen;
        // The FloatingScreen canvas is deliberately wider than the visual
        // panel when requests are available. That keeps the right-side
        // scrollbar within both the root RectMask2D and curved raycaster.
        private RectTransform? _panelRoot;
        private HMUI.ImageView? _screenBackground;
        private RectTransform? _maskRect;
        private RectTransform? _rowContainer;
        private RectTransform? _viewerStrip;
        private ViewerGroup? _viewerTwitch;
        private ViewerGroup? _viewerYouTube;
        private ViewerGroup? _viewerKick;
        private GameObject? _viewerTwitchGap;
        private GameObject? _viewerKickGap;
        private HMUI.ImageView? _lockIcon;
        private HMUI.ImageView? _moveModeIcon;
        // Hidden (active but transparent) TMP that word-wraps messages.
        private TextMeshProUGUI? _measureText;
        private readonly List<ChatRow> _rows = new List<ChatRow>();
        private readonly List<PendingChatRow> _pendingRows = new List<PendingChatRow>();
        private readonly List<ChatMessage> _snapshot = new List<ChatMessage>();
        private readonly Dictionary<ChatMessage, WrappedMessage> _wrapCache = new Dictionary<ChatMessage, WrappedMessage>();
        private readonly StringBuilder _builder = new StringBuilder(2048);
        private readonly Vector3[] _corners = new Vector3[4];
        private int _lastVersion = -1;
        private int _lastThirdPartyVersion = -1;
        private int _lastRenderedRowBudget = -1;
        private int _trimFramesRemaining;
        private float _rowUnit;
        private ChatIcons? _icons;
        private EmoteAtlas? _emotes;
        private ThirdPartyEmoteCatalog? _thirdPartyEmotes;
        private float _nextViewerRefresh;
        private string? _lastViewerLine;
        private float _nextCreateAttempt;
        private MenuButton? _menuButton;
        private SettingsFlowCoordinator? _settingsFlow;
        private SongRequestManager? _requestManager;
        private RequestListView? _requestList;
        private HMUI.ImageView? _requestsModeIcon;
        private TextMeshProUGUI? _requestsModeLabel;
        private bool _requestsMode;
        private bool _hasUnreadRequests;
        private int _requestAddedSignal;
        private MapOverlayController? _overlay;
        private HMUI.ImageView? _overlayToggleIcon;
        private TextMeshProUGUI? _overlayToggleLabel;
        private Material? _curvedTextMaterial;
        private Material? _flatTextMaterial;
        private TextMeshProUGUI? _statusLabel;
        private CanvasGroup? _statusLabelGroup;
        private GameObject? _statusRoot;
        private string? _persistentStatus;
        private string? _flashStatus;
        private float _flashStatusUntil;
        private int _lastRequestLabelVersion = -1;
        private float _lastAppearanceWidth = float.NaN;
        private float _lastAppearanceHeight = float.NaN;
        private float _lastAppearanceFontSize = float.NaN;
        private bool _visibilityApplied;
        private bool _panelGrabbed;
        private Vector3 _grabStartPosition;

        public void Initialize(ChatManager chatManager, PluginConfig config, IPALogger log,
            SongRequestManager? requestManager, MapOverlayController? overlay)
        {
            _chatManager = chatManager;
            _config = config;
            _log = log;
            _requestManager = requestManager;
            _overlay = overlay;
            if (_requestManager != null)
            {
                // Idempotent: unsubscribe first so a second Initialize can't
                // double-subscribe (removing an absent handler is a safe no-op).
                _requestManager.Added -= HandleRequestAdded;
                _requestManager.Added += HandleRequestAdded;
                _requestManager.Accepted -= HandleRequestAccepted;
                _requestManager.Accepted += HandleRequestAccepted;
                // Restore happens before the panel is initialized, so seed the
                // unread state from the queue instead of relying on Added.
                _hasUnreadRequests = _requestManager.HasRequests;
            }
            // BSML's button list lives in the menu container, which a soft
            // restart rebuilds empty while this panel survives (DontDestroyOnLoad),
            // so re-register on every menu init, not just the first CreatePanel.
            MainMenuAwaiter.MainMenuInitializing -= RegisterMenuButton;
            MainMenuAwaiter.MainMenuInitializing += RegisterMenuButton;
            MigrateDefaultPanelPosition();
            MigrateBackgroundDefaults();
        }

        // v0.8.0 shipped the fog-gray default before the backdrop switched
        // to a literal-color material; an untouched gray means the user
        // never picked a color, so move them to the new black default.
        private void MigrateBackgroundDefaults()
        {
            var config = _config;
            if (config == null)
                return;
            var c = config.BackgroundColor;
            if (Approximately(c.r, 0.7450981f) && Approximately(c.g, 0.7450981f) &&
                Approximately(c.b, 0.7450981f))
            {
                config.BackgroundColor = Color.black;
                if (Approximately(config.BackgroundOpacity, 1f))
                    config.BackgroundOpacity = 0.6f;
                _log?.Info("Migrated window background to the new black default.");
            }
        }

        private void OnDestroy()
        {
            MainMenuAwaiter.MainMenuInitializing -= RegisterMenuButton;
            if (_menuButton != null)
            {
                try { MenuButtons.Instance.UnregisterButton(_menuButton); }
                catch { /* BSML may already be torn down */ }
            }
            if (_screen != null)
                Destroy(_screen.gameObject);
            if (_requestManager != null)
            {
                _requestManager.Added -= HandleRequestAdded;
                _requestManager.Accepted -= HandleRequestAccepted;
            }
            if (_flatTextMaterial != null)
                Destroy(_flatTextMaterial);
            _thirdPartyEmotes?.Dispose();
        }

        private void Update()
        {
            if (_config == null || _chatManager == null)
                return;

            // Create the panel from Update with retries: on cold boot the
            // BSML pieces throw "Tried getting X too early" until the menu is up.
            if (_screen == null)
            {
                if (Time.unscaledTime < _nextCreateAttempt)
                    return;
                if (SceneManager.GetActiveScene().name != "MainMenu")
                    return;
                _nextCreateAttempt = Time.unscaledTime + 2f;
                try
                {
                    CreatePanel();
                }
                catch (System.Exception ex)
                {
                    _log?.Debug($"Panel creation not possible yet ({ex.GetType().Name}), retrying in 2s");
                    return;
                }
            }

            _requestManager?.Tick();
            if (Interlocked.Exchange(ref _requestAddedSignal, 0) != 0 && !_requestsMode)
            {
                _hasUnreadRequests = true;
                UpdateRequestsModeButton();
            }
            UpdateRequestStatusLabel();
            UpdateStatusLabel();
            _thirdPartyEmotes?.EnsureStarted(_config, _log);

            // Refresh the viewer strip in both chat and requests mode so the
            // counters stay live while the streamer manages the request queue.
            if (_viewerStrip != null && Time.unscaledTime >= _nextViewerRefresh)
            {
                _nextViewerRefresh = Time.unscaledTime + 1f;
                UpdateViewerText();
            }

            // Requests mode: drive the request list and freeze the chat feed
            // (it resumes, and rebuilds if new messages arrived, on toggle back).
            if (_requestsMode)
            {
                UpdateRequestListGeometry();
                _requestList?.Tick();
                return;
            }

            if (_rowContainer == null)
                return;

            var thirdPartyVersion = _thirdPartyEmotes?.Version ?? 0;
            if (thirdPartyVersion != _lastThirdPartyVersion)
            {
                _lastThirdPartyVersion = thirdPartyVersion;
                _wrapCache.Clear();
                _lastVersion = -1;
            }

            // New emote sprites finished downloading: re-render so messages
            // showing them as plain text pick up the images. Animation updates
            // only touch atlas pixels, so they do not force a row rebuild.
            var animateEmotes = _screen != null && _screen.gameObject.activeInHierarchy &&
                _config.EmojiImages;
            if (_emotes != null && _emotes.Pump(animateEmotes))
            {
                _wrapCache.Clear();
                _lastVersion = -1;
            }
            var version = _chatManager.Version;
            if (version != _lastVersion)
            {
                _lastVersion = version;
                RebuildRows();
            }
            else
            {
                UpdateRowMetrics();
                if (UpdateVisibleRowBudget() != _lastRenderedRowBudget)
                {
                    RebuildRows();
                    return;
                }
                // TMP can re-wrap a frame late (fallback glyphs, curve/size
                // changes), so keep trimming for a few frames after rebuilds.
                if (_trimFramesRemaining > 0)
                {
                    _trimFramesRemaining--;
                    TrimRows();
                }
            }
        }

        private void LateUpdate()
        {
            if (_panelGrabbed)
                ApplyMoveConstraints();
        }

        private void CreatePanel()
        {
            var config = _config!;
            ClampPanelSize(config);
            var position = new Vector3(config.PositionX, config.PositionY, config.PositionZ);
            var rotation = GetPanelRotation(position);

            _screen = FloatingScreen.CreateFloatingScreen(
                GetScreenCanvasSize(config),
                true, position, rotation, 0f, true);
            _screen.gameObject.name = "SimpleBSStreamChatPanel";
            DontDestroyOnLoad(_screen.gameObject);
            _screen.HandleGrabbed += OnHandleGrabbed;
            _screen.HandleReleased += OnHandleReleased;

            var panelRootGo = new GameObject("PanelRoot", typeof(RectTransform));
            _panelRoot = (RectTransform)panelRootGo.transform;
            _panelRoot.SetParent(_screen.transform, false);
            _panelRoot.anchorMin = new Vector2(0.5f, 0.5f);
            _panelRoot.anchorMax = new Vector2(0.5f, 0.5f);
            _panelRoot.pivot = new Vector2(0.5f, 0.5f);
            _panelRoot.anchoredPosition = Vector2.zero;
            _panelRoot.sizeDelta = new Vector2(config.PanelWidth, config.PanelHeight);
            // BSML's hasBackground child; tinted from config in ApplyAppearance.
            // Swap off the stock fog material — it mostly ignores vertex
            // color, so Window Color would have no visible effect on it.
            _screenBackground = _screen.transform.Find("Background")?.GetComponent<HMUI.ImageView>();
            if (_screenBackground != null)
            {
                var backgroundRect = _screenBackground.rectTransform;
                backgroundRect.SetParent(_panelRoot, false);
                backgroundRect.anchorMin = Vector2.zero;
                backgroundRect.anchorMax = Vector2.one;
                backgroundRect.offsetMin = Vector2.zero;
                backgroundRect.offsetMax = Vector2.zero;
                _screenBackground.material = Utilities.ImageResources.NoGlowMat;
                // Don't let the backdrop swallow pointer rays: with the grab
                // handle moved behind the panel (see OffsetGrabHandleBehindPanel),
                // an empty-area click must fall through to the handle to drag.
                _screenBackground.raycastTarget = false;
            }

            // Clip region for the rows; top band reserved for viewer counts.
            var maskGo = new GameObject("ChatContent", typeof(RectTransform), typeof(RectMask2D));
            _maskRect = (RectTransform)maskGo.transform;
            _maskRect.SetParent(_panelRoot, false);
            _maskRect.anchorMin = Vector2.zero;
            _maskRect.anchorMax = Vector2.one;
            _maskRect.offsetMin = new Vector2(3f, ContentBottomInset);
            _maskRect.offsetMax = new Vector2(-3f, -ContentTopInset);

            // Bottom-anchored stack that grows upward; oldest row on top.
            var containerGo = new GameObject("Rows", typeof(RectTransform));
            _rowContainer = (RectTransform)containerGo.transform;
            _rowContainer.SetParent(_maskRect, false);
            _rowContainer.anchorMin = new Vector2(0f, 0f);
            _rowContainer.anchorMax = new Vector2(1f, 0f);
            _rowContainer.pivot = new Vector2(0.5f, 0f);
            _rowContainer.offsetMin = Vector2.zero;
            _rowContainer.offsetMax = new Vector2(0f, 0f);
            var layout = containerGo.AddComponent<VerticalLayoutGroup>();
            layout.childControlHeight = true;
            layout.childControlWidth = true;
            layout.childForceExpandHeight = false;
            layout.childForceExpandWidth = true;
            layout.spacing = ChatRowSpacing;
            layout.childAlignment = TextAnchor.LowerLeft;
            var fitter = containerGo.AddComponent<ContentSizeFitter>();
            fitter.verticalFit = ContentSizeFitter.FitMode.PreferredSize;

            if (_emotes == null)
            {
                try
                {
                    _emotes = new EmoteAtlas(_log);
                }
                catch (System.Exception ex)
                {
                    _log?.Warn($"Emote support unavailable: {ex}");
                }
            }
            _thirdPartyEmotes ??= new ThirdPartyEmoteCatalog();

            // Word-wraps message strings so RebuildRows can split them into
            // one row per line. It must stay ACTIVE: TMP only learns its
            // wrap width from rect-dimension callbacks, which Unity doesn't
            // deliver to inactive objects (a disabled measurer wraps at
            // width 0 — one character per line). It stays invisible instead:
            // fully transparent and parked far above the mask, where
            // RectMask2D culls it. X-stretch anchors keep its wrap width in
            // lockstep with the rows' text width (mask minus 1+1 padding).
            _measureText = BeatSaberUI.CreateText(_maskRect, "", Vector2.zero, Vector2.zero);
            _measureText.alignment = TextAlignmentOptions.Left;
            _measureText.enableWordWrapping = true;
            _measureText.richText = true;
            _measureText.raycastTarget = false;
            _measureText.color = Color.clear;
            // Same sprite source as the rows so wrap measurement matches.
            if (_emotes != null)
                _measureText.spriteAsset = _emotes.SpriteAsset;
            ApplyTextMaterial(_measureText, config.CurveAmount > 0f);
            var measureRect = _measureText.rectTransform;
            measureRect.anchorMin = new Vector2(0f, 1f);
            measureRect.anchorMax = new Vector2(1f, 1f);
            measureRect.pivot = new Vector2(0.5f, 0f);
            measureRect.sizeDelta = new Vector2(-2f, 10f);
            measureRect.anchoredPosition = new Vector2(0f, 500f);

            if (_icons == null)
                _icons = ChatIcons.Load(_log);

            // Viewer-count strip: [icon count] per platform, packed right
            // inside the reserved top band.
            var stripGo = new GameObject("ViewerCounts", typeof(RectTransform));
            _viewerStrip = (RectTransform)stripGo.transform;
            _viewerStrip.SetParent(_panelRoot, false);
            _viewerStrip.anchorMin = new Vector2(0f, 1f);
            _viewerStrip.anchorMax = Vector2.one;
            _viewerStrip.pivot = new Vector2(0.5f, 1f);
            _viewerStrip.offsetMin = new Vector2(3f, -(ViewerStripTopInset + ViewerStripHeight));
            _viewerStrip.offsetMax = new Vector2(-ViewerStripRightInset, -ViewerStripTopInset);
            var stripLayout = stripGo.AddComponent<HorizontalLayoutGroup>();
            stripLayout.childAlignment = TextAnchor.MiddleRight;
            stripLayout.childControlWidth = true;
            stripLayout.childControlHeight = true;
            stripLayout.childForceExpandWidth = false;
            stripLayout.childForceExpandHeight = false;
            stripLayout.spacing = 1f;
            _viewerTwitch = CreateViewerGroup(_viewerStrip);
            _viewerTwitchGap = new GameObject("TwitchGap", typeof(RectTransform));
            _viewerTwitchGap.transform.SetParent(_viewerStrip, false);
            _viewerTwitchGap.AddComponent<LayoutElement>().preferredWidth = 2f;
            _viewerYouTube = CreateViewerGroup(_viewerStrip);
            _viewerKickGap = new GameObject("KickGap", typeof(RectTransform));
            _viewerKickGap.transform.SetParent(_viewerStrip, false);
            _viewerKickGap.AddComponent<LayoutElement>().preferredWidth = 2f;
            _viewerKick = CreateViewerGroup(_viewerStrip);

            CreateStatusLabel();

            // Padlock button top-left: click toggles whether the panel can
            // be grabbed and moved. The BSML screen already carries an
            // injected VRGraphicRaycaster, so a plain Button just works.
            var lockGo = new GameObject("LockButton", typeof(RectTransform));
            var lockRect = (RectTransform)lockGo.transform;
            lockRect.SetParent(_panelRoot, false);
            lockRect.anchorMin = new Vector2(0f, 1f);
            lockRect.anchorMax = new Vector2(0f, 1f);
            lockRect.pivot = new Vector2(0f, 1f);
            lockRect.anchoredPosition = new Vector2(CornerButtonX, CornerButtonY);
            lockRect.sizeDelta = new Vector2(CornerButtonSize, CornerButtonSize);
            _lockIcon = lockGo.AddComponent<HMUI.ImageView>();
            _lockIcon.material = Utilities.ImageResources.NoGlowMat;
            _lockIcon.preserveAspect = true;
            _lockIcon.raycastTarget = true;
            var lockButton = lockGo.AddComponent<Button>();
            lockButton.targetGraphic = _lockIcon;
            lockButton.onClick.AddListener(ToggleLock);

            _moveModeIcon = CreateCornerIconButton("MoveModeButton",
                new Vector2(CornerButtonX + CornerButtonSize + CornerButtonGap, CornerButtonY),
                ToggleMoveMode);

            // Third corner button flips the panel between Chat and Requests mode.
            if (_requestManager != null)
            {
                var modePos = new Vector2(CornerButtonX + 2f * (CornerButtonSize + CornerButtonGap), CornerButtonY);
                if (_icons?.ModeChat != null && _icons?.ModeRequests != null)
                    _requestsModeIcon = CreateCornerIconButton("RequestsModeButton", modePos, ToggleRequestsMode);
                else
                    _requestsModeLabel = CreateCornerTextButton("RequestsModeButton", modePos, "REQ", ToggleRequestsMode);
                _requestList = new RequestListView(_maskRect, (RectTransform)_screen.transform,
                    _screenBackground?.sprite, _requestManager, config, _log, StyleRequestText, _icons);
                _requestList.SetActive(false);
                UpdateRequestsModeButton();
            }

            // Overlay toggle: the next corner slot after the (optional) requests button.
            if (_overlay != null)
            {
                var overlaySlot = _requestManager != null ? 3 : 2;
                var overlayPos = new Vector2(CornerButtonX + overlaySlot * (CornerButtonSize + CornerButtonGap), CornerButtonY);
                if (_icons?.OverlayOn != null && _icons?.OverlayOff != null)
                    _overlayToggleIcon = CreateCornerIconButton("OverlayToggleButton", overlayPos, ToggleOverlay);
                else
                    _overlayToggleLabel = CreateCornerTextButton("OverlayToggleButton", overlayPos, "OVL", ToggleOverlay);
                UpdateOverlayToggleButton();
            }

            ApplyPinState();
            ApplyVisibility();
            ApplyAppearance();
            RegisterMenuButton();
            _lastVersion = -1; // force first refresh
            _log?.Info("Chat panel created.");
        }

        private HMUI.ImageView CreateCornerIconButton(string name, Vector2 anchoredPosition, UnityAction onClick)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_panelRoot ?? _screen!.transform, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.anchoredPosition = anchoredPosition;
            rect.sizeDelta = new Vector2(CornerButtonSize, CornerButtonSize);

            var icon = go.AddComponent<HMUI.ImageView>();
            icon.material = Utilities.ImageResources.NoGlowMat;
            icon.preserveAspect = true;
            icon.raycastTarget = true;

            var button = go.AddComponent<Button>();
            button.targetGraphic = icon;
            button.onClick.AddListener(onClick);

            return icon;
        }

        // Text-labelled sibling of the icon corner buttons (no art asset needed).
        private TextMeshProUGUI CreateCornerTextButton(string name, Vector2 anchoredPosition,
            string label, UnityAction onClick)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_panelRoot ?? _screen!.transform, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.anchoredPosition = anchoredPosition;
            rect.sizeDelta = new Vector2(CornerButtonSize, CornerButtonSize);

            var bg = go.AddComponent<HMUI.ImageView>();
            bg.material = Utilities.ImageResources.NoGlowMat;
            bg.sprite = Utilities.ImageResources.WhitePixel;
            bg.color = new Color(1f, 1f, 1f, 0.16f);
            bg.raycastTarget = true;

            var text = BeatSaberUI.CreateText(rect, label, Vector2.zero, Vector2.zero);
            text.alignment = TextAlignmentOptions.Center;
            text.enableWordWrapping = false;
            text.raycastTarget = false;
            text.fontSize = 2.6f;
            var textRect = text.rectTransform;
            textRect.anchorMin = Vector2.zero;
            textRect.anchorMax = Vector2.one;
            textRect.offsetMin = Vector2.zero;
            textRect.offsetMax = Vector2.zero;

            var button = go.AddComponent<Button>();
            button.targetGraphic = bg;
            button.onClick.AddListener(onClick);
            return text;
        }

        // Styles a request-list text like the chat rows: current font size, the
        // curve-aware material, and the shared emoji sprite atlas.
        private void StyleRequestText(TextMeshProUGUI text)
        {
            if (_config == null)
                return;
            text.fontSize = _config.FontSize;
            ApplyTextMaterial(text, _config.CurveAmount > 0f);
            if (_emotes != null)
                text.spriteAsset = _emotes.SpriteAsset;
            // Curved TMP caches its mesh; re-dirty so it (re)bends to the panel.
            text.SetAllDirty();
        }

        private void CreateStatusLabel()
        {
            var go = new GameObject("TopBarStatus", typeof(RectTransform));
            _statusRoot = go;
            var rect = (RectTransform)go.transform;
            rect.SetParent(_panelRoot!, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.offsetMin = new Vector2(StatusLabelLeftInset,
                -(ViewerStripTopInset + ViewerStripHeight));
            rect.offsetMax = new Vector2(-StatusLabelRightInset, -ViewerStripTopInset);

            _statusLabel = BeatSaberUI.CreateText(rect, "", Vector2.zero, Vector2.zero);
            _statusLabel.alignment = TextAlignmentOptions.Center;
            _statusLabel.enableWordWrapping = false;
            _statusLabel.overflowMode = TextOverflowModes.Ellipsis;
            _statusLabel.raycastTarget = false;
            _statusLabel.fontSize = StatusLabelFontSize;
            ApplyTextMaterial(_statusLabel, _config != null && _config.CurveAmount > 0f);
            var textRect = _statusLabel.rectTransform;
            textRect.anchorMin = Vector2.zero;
            textRect.anchorMax = Vector2.one;
            textRect.offsetMin = Vector2.zero;
            textRect.offsetMax = Vector2.zero;
            _statusLabelGroup = go.AddComponent<CanvasGroup>();
            go.SetActive(false);
        }

        /// <summary>Sets the steady top-bar label, currently used by Requests mode.</summary>
        public void SetPersistentLabel(string? text)
        {
            _persistentStatus = string.IsNullOrWhiteSpace(text) ? null : text;
            UpdateStatusLabel();
        }

        /// <summary>Shows brief, terse feedback without displacing a persistent label.</summary>
        public void FlashStatus(string text)
        {
            if (string.IsNullOrWhiteSpace(text))
                return;
            _flashStatus = text;
            _flashStatusUntil = Time.unscaledTime + StatusFlashDuration;
            UpdateStatusLabel();
        }

        private void UpdateRequestStatusLabel()
        {
            if (!_requestsMode || _requestManager == null)
                return;
            var version = _requestManager.Version;
            if (version == _lastRequestLabelVersion)
                return;
            _lastRequestLabelVersion = version;
            SetPersistentLabel($"Requests ({_requestManager.Count})");
        }

        private void UpdateStatusLabel()
        {
            if (_statusLabel == null || _statusLabelGroup == null || _statusRoot == null)
                return;

            var now = Time.unscaledTime;
            var flashActive = !string.IsNullOrEmpty(_flashStatus) && now < _flashStatusUntil;
            if (!flashActive)
                _flashStatus = null;
            var content = flashActive ? _flashStatus : _persistentStatus;
            var visible = !string.IsNullOrEmpty(content) && UpdateStatusLabelLayout();
            if (_statusRoot.activeSelf != visible)
                _statusRoot.SetActive(visible);
            if (!visible)
                return;

            if (_statusLabel.text != content)
            {
                _statusLabel.text = content;
                _statusLabel.SetAllDirty();
            }
            var remaining = _flashStatusUntil - now;
            _statusLabelGroup.alpha = flashActive && remaining < StatusFadeDuration
                ? Mathf.Clamp01(remaining / StatusFadeDuration)
                : 1f;
        }

        private bool UpdateStatusLabelLayout()
        {
            if (_statusLabel == null || _statusRoot == null || _config == null)
                return false;

            var rightInset = StatusLabelRightInset;
            if (_viewerStrip != null && _viewerStrip.gameObject.activeInHierarchy)
            {
                LayoutRebuilder.ForceRebuildLayoutImmediate(_viewerStrip);
                var viewerWidth = LayoutUtility.GetPreferredWidth(_viewerStrip);
                rightInset = Mathf.Max(rightInset, ViewerStripRightInset + viewerWidth + 1f);
            }

            var panelWidth = _panelRoot != null ? _panelRoot.rect.width : _config.PanelWidth;
            var hasRoom = panelWidth - StatusLabelLeftInset - rightInset >= 9f;
            if (!hasRoom)
                return false;

            // The TMP child fills this rect. Applying panel-space insets to
            // the child as well would push the label down into the content.
            var rect = (RectTransform)_statusRoot.transform;
            rect.offsetMin = new Vector2(StatusLabelLeftInset,
                -(ViewerStripTopInset + ViewerStripHeight));
            rect.offsetMax = new Vector2(-rightInset, -ViewerStripTopInset);
            return true;
        }

        private void ToggleRequestsMode()
        {
            _requestsMode = !_requestsMode;
            if (_requestsMode)
            {
                _hasUnreadRequests = false;
                Interlocked.Exchange(ref _requestAddedSignal, 0);
            }
            ApplyRequestsMode();
            FlashStatus(_requestsMode ? "Requests" : "Chat");
        }

        private void ApplyRequestsMode()
        {
            if (_requestList == null)
                return;
            if (_requestsMode)
                UpdateRequestListGeometry();
            _requestList.SetActive(_requestsMode);
            if (_rowContainer != null)
                _rowContainer.gameObject.SetActive(!_requestsMode);
            // Viewer counters stay visible in requests mode too — they share the
            // top bar with the "Requests (N)" label (which reserves space for the
            // strip on its right).
            if (_viewerStrip != null)
                _viewerStrip.gameObject.SetActive(_config != null && ShouldShowViewerStrip(_config));
            if (_requestsMode)
            {
                _lastRequestLabelVersion = -1;
                UpdateRequestStatusLabel();
            }
            else
            {
                _lastRequestLabelVersion = -1;
                SetPersistentLabel(null);
            }
            UpdateRequestsModeButton();
            UpdateStatusLabel();
        }

        private void UpdateRequestListGeometry()
        {
            if (_requestList == null)
                return;
            UpdateRowMetrics();
            _requestList.SetChatTopSlack(GetChatTopSlack());
        }

        private void HandleRequestAccepted()
        {
            if (!_requestsMode)
                return;
            _requestsMode = false;
            ApplyRequestsMode();
            FlashStatus("Chat");
        }

        // Chat commands arrive on background client threads; only signal here.
        // The main-thread Update loop owns the actual Unity sprite change.
        private void HandleRequestAdded()
        {
            Interlocked.Exchange(ref _requestAddedSignal, 1);
        }

        // The toggle shows the mode you'll switch TO (requests-list glyph in
        // Chat mode; chat glyph in Requests mode), gold-tinted while Requests
        // mode is active. Falls back to a "REQ"/"CHAT" text label.
        private void UpdateRequestsModeButton()
        {
            if (_requestsModeIcon != null && _icons != null)
            {
                var requestIcon = _hasUnreadRequests && _icons.ModeRequestsNotification != null
                    ? _icons.ModeRequestsNotification
                    : _icons.ModeRequests;
                _requestsModeIcon.sprite = _requestsMode ? _icons.ModeChat : requestIcon;
                _requestsModeIcon.color = _requestsMode ? new Color(1f, 0.82f, 0.29f) : Color.white;
            }
            if (_requestsModeLabel != null)
                _requestsModeLabel.text = _requestsMode ? "CHAT" : (_hasUnreadRequests ? "REQ!" : "REQ");
        }

        private void ToggleOverlay()
        {
            if (_config == null || _overlay == null)
                return;
            _config.OverlayEnabled = !_config.OverlayEnabled;
            _overlay.SetEnabled(_config.OverlayEnabled);
            UpdateOverlayToggleButton();
            FlashStatus(_config.OverlayEnabled ? "Overlay On" : "Overlay Off");
        }

        // On = the plain monitor glyph in white; off = the slashed glyph, dimmed.
        private void UpdateOverlayToggleButton()
        {
            var on = _config != null && _config.OverlayEnabled;
            if (_overlayToggleIcon != null && _icons != null)
            {
                _overlayToggleIcon.sprite = on ? _icons.OverlayOn : _icons.OverlayOff;
                _overlayToggleIcon.color = on ? Color.white : new Color(1f, 1f, 1f, 0.45f);
            }
            if (_overlayToggleLabel != null)
                _overlayToggleLabel.color = on ? Color.white : new Color(1f, 1f, 1f, 0.45f);
        }

        private ChatRow CreateRow()
        {
            var rootGo = new GameObject("Row", typeof(RectTransform));
            rootGo.transform.SetParent(_rowContainer, false);
            var background = rootGo.AddComponent<HMUI.ImageView>();
            background.material = Utilities.ImageResources.NoGlowMat;
            background.sprite = Utilities.ImageResources.WhitePixel;
            background.raycastTarget = false;
            var layout = rootGo.AddComponent<HorizontalLayoutGroup>();
            layout.padding = new RectOffset(1, 1, 0, 0);
            layout.spacing = IconSpacing;
            layout.childControlWidth = true;
            layout.childControlHeight = true;
            // Never force-expand width: that splits spare row width between
            // icons and text, drifting both toward the center. Instead the
            // text alone is flexible (see below), so icons pin to the left.
            layout.childForceExpandWidth = false;
            // Text fills the whole fixed-height row; TMP's Left alignment is
            // vertically centered, so the line centers within its grid row.
            layout.childForceExpandHeight = true;
            var group = rootGo.AddComponent<CanvasGroup>();
            // Overrides the text-driven height so the row snaps to the grid.
            var layoutElement = rootGo.AddComponent<LayoutElement>();

            // Sibling order = layout order: [platform icon][badge icon][text].
            var (platformIcon, platformIconLayout) = CreateIcon(rootGo.transform);
            var (badgeIcon, badgeIconLayout) = CreateIcon(rootGo.transform);

            var text = BeatSaberUI.CreateText((RectTransform)rootGo.transform, "", Vector2.zero, Vector2.zero);
            text.alignment = TextAlignmentOptions.Left;
            // Rows hold exactly one pre-wrapped line; never wrap again.
            text.enableWordWrapping = false;
            text.richText = true;
            text.raycastTarget = false;
            if (_emotes != null)
                text.spriteAsset = _emotes.SpriteAsset;
            // The text soaks up all spare row width, keeping icons hard left.
            var textLayout = text.gameObject.AddComponent<LayoutElement>();
            textLayout.flexibleWidth = 1f;

            return new ChatRow
            {
                Root = rootGo,
                Rect = (RectTransform)rootGo.transform,
                Background = background,
                Text = text,
                Group = group,
                Layout = layoutElement,
                PlatformIcon = platformIcon,
                PlatformIconLayout = platformIconLayout,
                BadgeIcon = badgeIcon,
                BadgeIconLayout = badgeIconLayout,
            };
        }

        private ViewerGroup CreateViewerGroup(RectTransform parent)
        {
            var (icon, iconLayout) = CreateIcon(parent);
            var text = BeatSaberUI.CreateText(parent, "", Vector2.zero, Vector2.zero);
            text.alignment = TextAlignmentOptions.Left;
            text.enableWordWrapping = false;
            text.richText = true;
            text.raycastTarget = false;
            return new ViewerGroup { Icon = icon, IconLayout = iconLayout, Text = text };
        }

        // ImageView curves with the panel (same component as the row
        // backgrounds), unlike TMP inline sprites which stay flat.
        private static (HMUI.ImageView Image, LayoutElement Layout) CreateIcon(Transform parent)
        {
            var go = new GameObject("Icon", typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var image = go.AddComponent<HMUI.ImageView>();
            image.material = Utilities.ImageResources.NoGlowMat;
            image.raycastTarget = false;
            image.preserveAspect = true;
            var layoutElement = go.AddComponent<LayoutElement>();
            go.SetActive(false);
            return (image, layoutElement);
        }

        private void RebuildRows()
        {
            var config = _config!;
            var measure = _measureText;
            if (measure == null)
                return;
            _chatManager!.CopyMessages(_snapshot);
            var backgroundColor = config.HighlightMessages
                ? new Color(0f, 0f, 0f, Mathf.Clamp01(config.HighlightOpacity))
                : Color.clear;

            UpdateRowMetrics();

            // The measurer's width tracks the mask via its anchors; if the
            // rect is somehow degenerate, don't split (avoids one-character
            // rows) and show whole messages until geometry settles.
            measure.fontSize = config.FontSize;
            var measureWidth = measure.rectTransform.rect.width;
            var canSplit = measureWidth >= 20f;
            var iconSize = _rowUnit * 0.8f;
            var useCurvedTextMaterial = config.CurveAmount > 0f;
            ApplyTextMaterial(measure, useCurvedTextMaterial);

            _pendingRows.Clear();
            PruneWrapCache();
            foreach (var m in _snapshot)
            {
                if (m.Platform == ChatPlatform.System && !config.ShowSystemMessages)
                    continue;

                // Icons replace the [T]/[Y] and [B]/[M] text tags on the
                // message's first row; skipped when the panel is too narrow
                // for the reservation to make sense.
                Sprite? platformIcon = null;
                Sprite? badgeIcon = null;
                if (_icons != null && config.ShowPlatformIcons && m.Platform != ChatPlatform.System)
                {
                    platformIcon = PlatformSprite(m.Platform);
                    badgeIcon = m.IsBroadcaster ? _icons.Broadcaster
                        : m.IsModerator ? _icons.Moderator : null;
                    var reserved = 2f * (iconSize + IconSpacing);
                    if (reserved > measureWidth * 0.35f)
                    {
                        platformIcon = null;
                        badgeIcon = null;
                    }
                }
                var iconCount = (platformIcon != null ? 1 : 0) + (badgeIcon != null ? 1 : 0);
                // Icons off = clean "name: message" rows, no [T]/[Y] tags.
                // The text tags only step in when icons are wanted but can't
                // render (strip failed to load or the panel is too narrow).
                var showTextTags = m.Platform != ChatPlatform.System
                    && config.ShowPlatformIcons && platformIcon == null;

                var imageEmotes = _emotes != null && config.EmojiImages;
                var wrapped = GetWrappedMessage(m, measure, measureWidth, canSplit,
                    showTextTags, iconCount, imageEmotes, imageEmotes, _thirdPartyEmotes?.Version ?? 0);
                for (var li = 0; li < wrapped.Lines.Length; li++)
                    _pendingRows.Add(new PendingChatRow(wrapped.Lines[li],
                        li == 0 ? platformIcon : null, li == 0 ? badgeIcon : null, m));
            }

            // The feed stays non-scrollable: render only the newest complete
            // rows that can fit the panel's actual local-space height.
            var rowBudget = UpdateVisibleRowBudget();
            _lastRenderedRowBudget = rowBudget;
            var firstVisible = Mathf.Max(0, _pendingRows.Count - rowBudget);
            // Tell the atlas which emotes are on screen this rebuild; only those
            // animate (see EmoteAtlas.AnimateReadyEntries). Stops off-screen emotes
            // from old messages from driving per-frame atlas work.
            _emotes?.BeginVisiblePass();
            ChatMessage? markedSource = null;
            var index = 0;
            for (var i = firstVisible; i < _pendingRows.Count; i++)
            {
                var row = _pendingRows[i];
                if (_emotes != null && !ReferenceEquals(row.Source, markedSource))
                {
                    MarkVisibleEmotes(row.Source);
                    markedSource = row.Source;
                }
                AssignRow(ref index, row.Text, config, backgroundColor,
                    row.PlatformIcon, row.BadgeIcon, iconSize);
            }
            for (var i = index; i < _rows.Count; i++)
            {
                if (_rows[i].Root.activeSelf)
                    _rows[i].Root.SetActive(false);
            }

            // Lay out now so trimming sees final geometry this frame.
            LayoutRebuilder.ForceRebuildLayoutImmediate(_rowContainer);
            TrimRows();
            _trimFramesRemaining = TrimFramesAfterRebuild;
        }

        // Marks a shown message's custom emotes (7TV/BTTV/native animated) as
        // on-screen so the atlas animates only them. Unicode emoji are static and
        // need no marking.
        private void MarkVisibleEmotes(ChatMessage message)
        {
            var segments = message?.Segments;
            if (_emotes == null || segments == null)
                return;
            for (var i = 0; i < segments.Count; i++)
                if (segments[i] is EmoteSegment emote)
                    _emotes.MarkVisible(emote.Emote.Key);
        }

        private WrappedMessage GetWrappedMessage(ChatMessage message, TextMeshProUGUI measure,
            float measureWidth, bool canSplit, bool showTextTags, int iconCount,
            bool emojiImages, bool customEmotes, int thirdPartyVersion)
        {
            if (_wrapCache.TryGetValue(message, out var cached) &&
                Mathf.Approximately(cached.FontSize, measure.fontSize) &&
                Mathf.Approximately(cached.MeasureWidth, measureWidth) &&
                cached.CanSplit == canSplit &&
                cached.ShowTextTags == showTextTags &&
                cached.IconCount == iconCount &&
                cached.EmojiImages == emojiImages &&
                cached.CustomEmotes == customEmotes &&
                cached.ThirdPartyVersion == thirdPartyVersion)
                return cached;

            _builder.Length = 0;
            var displayStart = 0;
            if (iconCount > 0)
            {
                _builder.Append("<space=")
                    .Append((iconCount * (_rowUnit * 0.8f + IconSpacing))
                        .ToString("F2", System.Globalization.CultureInfo.InvariantCulture))
                    .Append('>');
                displayStart = _builder.Length;
            }
            BuildMessageText(message, showTextTags);
            var full = _builder.ToString();

            var wrapped = new WrappedMessage
            {
                Message = message,
                FontSize = measure.fontSize,
                MeasureWidth = measureWidth,
                CanSplit = canSplit,
                ShowTextTags = showTextTags,
                EmojiImages = emojiImages,
                CustomEmotes = customEmotes,
                ThirdPartyVersion = thirdPartyVersion,
                IconCount = iconCount,
                Lines = MeasureWrappedLines(measure, full, displayStart, canSplit),
            };
            _wrapCache[message] = wrapped;
            return wrapped;
        }

        private static string[] MeasureWrappedLines(TextMeshProUGUI measure, string full,
            int displayStart, bool canSplit)
        {
            string[] SingleLine()
            {
                var start = Mathf.Clamp(displayStart, 0, full.Length);
                return new[] { full.Substring(start) };
            }

            measure.text = full;
            measure.ForceMeshUpdate(true, true);
            var info = measure.textInfo;
            var lineCount = Mathf.Max(1, info.lineCount);
            if (lineCount == 1 || !canSplit)
                return SingleLine();

            var lines = new List<string>(lineCount);
            for (var li = 0; li < lineCount; li++)
            {
                var start = li == 0 ? displayStart : 0;
                if (li > 0 && !TryLineStart(info, li, full.Length, out start))
                    return SingleLine();

                var end = full.Length;
                if (li + 1 < lineCount && !TryLineStart(info, li + 1, full.Length, out end))
                    return SingleLine();

                if (start < 0 || end < start || start > full.Length)
                    return SingleLine();

                var length = Mathf.Clamp(end, start, full.Length) - start;
                var slice = full.Substring(start, length).Trim(' ');
                if (slice.Length == 0)
                    continue;
                lines.Add(li == 0 ? slice : ReopenTags(full, start) + slice);
            }

            return lines.Count == 0 ? SingleLine() : lines.ToArray();
        }

        private static bool TryLineStart(TMP_TextInfo info, int lineIndex, int sourceLength, out int start)
        {
            start = 0;
            if (lineIndex < 0 || lineIndex >= info.lineCount)
                return false;
            var firstCharacterIndex = info.lineInfo[lineIndex].firstCharacterIndex;
            if (firstCharacterIndex < 0 || firstCharacterIndex >= info.characterInfo.Length)
                return false;
            start = Mathf.Clamp(info.characterInfo[firstCharacterIndex].index, 0, sourceLength);
            return true;
        }

        private void PruneWrapCache()
        {
            if (_wrapCache.Count <= _snapshot.Count + 8)
                return;
            var live = new HashSet<ChatMessage>(_snapshot);
            var remove = new List<ChatMessage>();
            foreach (var entry in _wrapCache)
            {
                if (!live.Contains(entry.Key))
                    remove.Add(entry.Key);
            }
            foreach (var message in remove)
                _wrapCache.Remove(message);
        }

        private void AssignRow(ref int index, string text, PluginConfig config, Color backgroundColor,
            Sprite? platformIcon, Sprite? badgeIcon, float iconSize)
        {
            if (index >= _rows.Count)
                _rows.Add(CreateRow());
            var row = _rows[index];
            index++;
            ApplyTextMaterial(row.Text, config.CurveAmount > 0f);
            row.Text.text = text;
            row.Text.fontSize = config.FontSize;
            row.Layout.preferredHeight = _rowUnit;
            row.Background.color = backgroundColor;
            SetIcon(row.PlatformIcon, row.PlatformIconLayout, platformIcon, iconSize);
            SetIcon(row.BadgeIcon, row.BadgeIconLayout, badgeIcon, iconSize);
            row.Group.alpha = 1f;
            if (!row.Root.activeSelf)
                row.Root.SetActive(true);
        }

        private static void SetIcon(HMUI.ImageView image, LayoutElement layoutElement, Sprite? sprite, float size)
        {
            if (sprite == null)
            {
                if (image.gameObject.activeSelf)
                    image.gameObject.SetActive(false);
                return;
            }
            if (image.sprite != sprite)
                image.sprite = sprite;
            layoutElement.preferredWidth = size;
            layoutElement.preferredHeight = size;
            if (!image.gameObject.activeSelf)
                image.gameObject.SetActive(true);
        }

        private void UpdateRowMetrics()
        {
            if (_config == null || _measureText == null)
                return;
            // Guard .font (as RowHeight() already does): if the TMP font asset is
            // ever null this runs every frame and would otherwise NRE, stalling the
            // whole Update loop (and the request list with it).
            var font = _measureText.font;
            if (font == null)
                return;
            var face = font.faceInfo;
            var lineRatio = face.pointSize > 0f ? face.lineHeight / face.pointSize : 1.2f;
            _rowUnit = Mathf.Max(0.01f, _config.FontSize * lineRatio);
        }

        private int UpdateVisibleRowBudget()
        {
            var rowHeight = Mathf.Max(0.01f, _rowUnit);
            var maskHeight = GetContentMaskHeight();
            return Mathf.Max(1, Mathf.FloorToInt((maskHeight + ChatRowSpacing) /
                (rowHeight + ChatRowSpacing)));
        }

        private float GetChatTopSlack()
        {
            var budget = UpdateVisibleRowBudget();
            var usedHeight = budget * _rowUnit + Mathf.Max(0, budget - 1) * ChatRowSpacing;
            return Mathf.Max(0f, GetContentMaskHeight() - usedHeight);
        }

        private float GetContentMaskHeight()
        {
            var maskHeight = _maskRect == null ? 0f : _maskRect.rect.height;
            if (maskHeight <= 0.01f && _config != null)
                maskHeight = _config.PanelHeight - ContentBottomInset - ContentTopInset;
            return Mathf.Max(0f, maskHeight);
        }

        // Re-opens the rich-text tags (only ones we ever emit: b, i, color)
        // still unclosed at source[end], so a continuation line keeps the
        // styling the wrap split it away from. User text can't inject real
        // tags — Sanitize breaks them with a zero-width space, which also
        // means unrecognized '<' runs are treated as literal text here.
        private static string ReopenTags(string source, int end)
        {
            List<string>? open = null;
            var i = 0;
            while (i < end)
            {
                if (source[i] != '<')
                {
                    i++;
                    continue;
                }
                var close = source.IndexOf('>', i + 1);
                if (close < 0 || close >= end)
                    break;
                var inner = source.Substring(i + 1, close - i - 1);
                if (inner == "b" || inner == "i" ||
                    inner.StartsWith("color=", System.StringComparison.Ordinal))
                {
                    (open ??= new List<string>()).Add(inner);
                    i = close + 1;
                }
                else if (inner.Length > 1 && inner[0] == '/')
                {
                    var name = inner.Substring(1);
                    if (open != null)
                    {
                        for (var j = open.Count - 1; j >= 0; j--)
                        {
                            if (open[j] == name ||
                                open[j].StartsWith(name + "=", System.StringComparison.Ordinal))
                            {
                                open.RemoveAt(j);
                                break;
                            }
                        }
                    }
                    i = close + 1;
                }
                else
                {
                    i++;
                }
            }
            if (open == null || open.Count == 0)
                return "";
            var tags = new StringBuilder(32);
            foreach (var tag in open)
                tags.Append('<').Append(tag).Append('>');
            return tags.ToString();
        }

        private void BuildMessageText(ChatMessage m, bool showTextTags)
        {
            if (m.Platform == ChatPlatform.System)
            {
                _builder.Append("<color=#9E9E9E><i>").Append(Sanitize(m.Text)).Append("</i></color>");
                return;
            }

            if (showTextTags)
            {
                _builder.Append(m.Platform == ChatPlatform.Twitch
                    ? "<color=#A970FF>[T]</color> "
                    : m.Platform == ChatPlatform.YouTube
                        ? "<color=#FF4444>[Y]</color> "
                        : "<color=#53FC18>[K]</color> ");

                if (m.IsBroadcaster)
                    _builder.Append("<color=#E91916>[B]</color> ");
                else if (m.IsModerator)
                    _builder.Append("<color=#00AD03>[M]</color> ");
            }

            _builder.Append("<b><color=").Append(m.AuthorColorHex).Append('>')
                .Append(Sanitize(m.Author)).Append("</color></b>");

            if (m.IsAction)
            {
                _builder.Append(" <i>");
                AppendMessageBody(m);
                _builder.Append("</i>");
            }
            else
            {
                _builder.Append(": ");
                AppendMessageBody(m);
            }
        }

        private void AppendMessageBody(ChatMessage message)
        {
            var config = _config!;
            if (message.Segments == null || message.Segments.Count == 0 || !config.EmojiImages)
            {
                AppendTextSegment(message.Text, allowThirdParty: config.EmojiImages);
                return;
            }

            foreach (var segment in message.Segments)
            {
                if (segment is TextSegment text)
                {
                    AppendTextSegment(text.Text, allowThirdParty: true);
                }
                else if (segment is EmoteSegment emote)
                {
                    AppendEmoteOrFallback(emote.Emote);
                }
            }
        }

        private void AppendEmoteOrFallback(EmoteRef emote)
        {
            var config = _config!;
            if (_emotes != null && config.EmojiImages && _emotes.TryAppendEmote(_builder, emote))
                return;
            AppendTextSegment(emote.Name, allowThirdParty: false);
        }

        private void AppendTextSegment(string text, bool allowThirdParty)
        {
            var sanitized = Sanitize(text);
            if (allowThirdParty && _config!.EmojiImages && _thirdPartyEmotes != null)
                AppendTextWithThirdPartyEmotes(sanitized);
            else
                AppendTextWithUnicodeEmoji(sanitized);
        }

        private void AppendTextWithThirdPartyEmotes(string text)
        {
            var i = 0;
            while (i < text.Length)
            {
                if (char.IsWhiteSpace(text[i]))
                {
                    var start = i++;
                    while (i < text.Length && char.IsWhiteSpace(text[i]))
                        i++;
                    _builder.Append(text, start, i - start);
                    continue;
                }

                var tokenStart = i++;
                while (i < text.Length && !char.IsWhiteSpace(text[i]))
                    i++;
                var token = text.Substring(tokenStart, i - tokenStart);
                if (_thirdPartyEmotes != null &&
                    _thirdPartyEmotes.TryGet(token, out var emote) &&
                    _emotes != null &&
                    _emotes.TryAppendEmote(_builder, emote))
                    continue;

                AppendTextWithUnicodeEmoji(token);
            }
        }

        private void AppendTextWithUnicodeEmoji(string sanitized)
        {
            if (_emotes != null && _config!.EmojiImages)
                _emotes.AppendTextWithEmoji(_builder, sanitized, emojiImages: true);
            else
                EmoteAtlas.AppendWithoutEmojiControls(_builder, sanitized);
        }

        // Walks rows newest-first and hides, whole, every row that either
        // exceeds the geometry-derived grid budget (a wrapped message spends
        // one row per line) or pokes past the top of the panel into
        // the viewer-count band — rows either show fully or not at all, no
        // cut-off half rows.
        private void TrimRows()
        {
            if (_maskRect == null || _config == null)
                return;
            var maskTop = _maskRect.rect.yMax;
            UpdateRowMetrics();
            var rowBudget = UpdateVisibleRowBudget();
            var rowsUsed = 0;
            for (var i = _rows.Count - 1; i >= 0; i--)
            {
                var row = _rows[i];
                if (!row.Root.activeSelf)
                    continue;
                rowsUsed++;
                var visible = rowsUsed <= rowBudget;
                if (visible)
                {
                    row.Rect.GetWorldCorners(_corners);
                    var topLocal = _maskRect.InverseTransformPoint(_corners[1]).y;
                    visible = topLocal <= maskTop + 0.05f;
                }
                var alpha = visible ? 1f : 0f;
                if (!Mathf.Approximately(row.Group.alpha, alpha))
                    row.Group.alpha = alpha;
            }
        }

        private void UpdateViewerText()
        {
            var config = _config!;
            if (_viewerStrip == null || _viewerTwitch == null || _viewerYouTube == null || _viewerKick == null)
                return;
            var tracker = _chatManager?.ViewerTracker;
            var showTwitch = config.EnableTwitch;
            var showYouTube = config.EnableYouTube;
            var showKick = config.EnableKick;
            var show = ShouldShowViewerStrip(config) && tracker != null;
            if (_viewerStrip.gameObject.activeSelf != show)
                _viewerStrip.gameObject.SetActive(show);
            if (!show)
            {
                _lastViewerLine = null;
                UpdateStatusLabel();
                return;
            }

            var useIcons = _icons != null && config.ShowPlatformIcons;
            var twitchValue = FormatViewers(tracker!.TwitchViewers);
            var youtubeValue = FormatViewers(tracker!.YouTubeViewers);
            var kickValue = FormatViewers(tracker!.KickViewers);
            var line = (useIcons ? "i|" : "t|")
                + (showTwitch ? "T:" + twitchValue : "")
                + (showYouTube ? "|Y:" + youtubeValue : "")
                + (showKick ? "|K:" + kickValue : "");
            if (line == _lastViewerLine)
            {
                UpdateStatusLabel();
                return;
            }
            _lastViewerLine = line;

            SetViewerGroupVisible(_viewerTwitch, showTwitch);
            SetViewerGroupVisible(_viewerYouTube, showYouTube);
            SetViewerGroupVisible(_viewerKick, showKick);
            if (_viewerTwitchGap != null)
                _viewerTwitchGap.SetActive(showTwitch && (showYouTube || showKick));
            if (_viewerKickGap != null)
                _viewerKickGap.SetActive(showYouTube && showKick);
            if (showTwitch)
                SetViewerGroup(_viewerTwitch, useIcons ? _icons!.Twitch : null,
                    "<color=#A970FF>[T]</color> ", twitchValue);
            if (showYouTube)
                SetViewerGroup(_viewerYouTube, useIcons ? _icons!.YouTube : null,
                    "<color=#FF4444>[Y]</color> ", youtubeValue);
            if (showKick)
                SetViewerGroup(_viewerKick, useIcons ? _icons!.Kick : null,
                    "<color=#53FC18>[K]</color> ", kickValue);
            UpdateStatusLabel();
        }

        private static bool ShouldShowViewerStrip(PluginConfig config) =>
            config.ShowViewerCounts && (config.EnableTwitch || config.EnableYouTube || config.EnableKick);

        private Sprite? PlatformSprite(ChatPlatform platform)
        {
            if (_icons == null)
                return null;
            switch (platform)
            {
                case ChatPlatform.Twitch:
                    return _icons.Twitch;
                case ChatPlatform.YouTube:
                    return _icons.YouTube;
                case ChatPlatform.Kick:
                    return _icons.Kick;
                default:
                    return null;
            }
        }

        private void SetViewerGroup(ViewerGroup group, Sprite? icon, string textTag, string value)
        {
            var config = _config!;
            var fontSize = config.ViewerCountSize;
            SetIcon(group.Icon, group.IconLayout, icon, fontSize);
            ApplyTextMaterial(group.Text, config.CurveAmount > 0f);
            group.Text.fontSize = fontSize;
            group.Text.text = icon != null ? value : textTag + value;
        }

        private static void SetViewerGroupVisible(ViewerGroup group, bool visible)
        {
            if (!visible)
            {
                group.Icon.gameObject.SetActive(false);
                group.Text.gameObject.SetActive(false);
                return;
            }

            group.Text.gameObject.SetActive(true);
        }

        private static string FormatViewers(int count) => count < 0 ? "–" : count.ToString("N0");

        /// <summary>Master visibility from the "Show Chat Window" toggle.</summary>
        public void ApplyVisibility()
        {
            if (_config == null || _screen == null)
                return;
            var changed = _screen.gameObject.activeSelf != _config.ShowPanel;
            if (changed)
                _screen.gameObject.SetActive(_config.ShowPanel);
            if (_visibilityApplied && changed && _config.ShowPanel)
                FlashStatus("Window Shown");
            _visibilityApplied = true;
        }

        /// <summary>Moves the panel back to the default centered foot-marker pose.</summary>
        public void RecenterPanel()
        {
            var config = _config;
            if (config == null)
                return;

            _panelGrabbed = false;
            var position = GetDefaultPanelPosition();
            config.PositionX = position.x;
            config.PositionY = position.y;
            config.PositionZ = position.z;
            config.RotationX = PluginConfig.DefaultRotationX;
            config.RotationY = PluginConfig.DefaultRotationY;
            config.AlignWithFloor = true;

            if (_screen != null)
                _screen.transform.SetPositionAndRotation(position, GetPanelRotation(position));

            _log?.Info($"Chat panel recentered ({position.x:F2}, {position.y:F2}, {position.z:F2}).");
        }

        /// <summary>Re-applies size/curve/font/viewer-count visibility from config, live.</summary>
        public void ApplyAppearance()
        {
            var config = _config;
            if (_screen == null || config == null)
                return;
            ClampPanelSize(config);
            var widthChanged = !Mathf.Approximately(_lastAppearanceWidth, config.PanelWidth);
            var heightChanged = !Mathf.Approximately(_lastAppearanceHeight, config.PanelHeight);
            var fontChanged = !Mathf.Approximately(_lastAppearanceFontSize, config.FontSize);
            _screen.ScreenSize = GetScreenCanvasSize(config);
            if (_panelRoot != null)
                _panelRoot.sizeDelta = new Vector2(config.PanelWidth, config.PanelHeight);
            if (_screenBackground != null)
            {
                var tint = config.BackgroundColor;
                _screenBackground.color = new Color(tint.r, tint.g, tint.b,
                    Mathf.Clamp01(config.BackgroundOpacity));
            }
            var curvedCanvas = _screen.GetComponentInChildren<HMUI.CurvedCanvasSettings>();
            if (curvedCanvas != null)
                curvedCanvas.SetRadius(config.CurveAmount <= 0f ? 0f : 1200f / config.CurveAmount);
            var useCurvedTextMaterial = config.CurveAmount > 0f;
            _emotes?.SetCurved(useCurvedTextMaterial);
            if (_measureText != null)
            {
                ApplyTextMaterial(_measureText, useCurvedTextMaterial);
                _measureText.SetAllDirty();
            }
            // Curved TMP leaves prior curve data in the mesh when radius is
            // zero, so flat mode also uses a non-curved font material clone.
            foreach (var row in _rows)
            {
                ApplyTextMaterial(row.Text, useCurvedTextMaterial);
                row.Text.fontSize = config.FontSize;
                row.Text.SetAllDirty();
            }
            if (_viewerTwitch != null && _viewerYouTube != null && _viewerKick != null)
            {
                ApplyTextMaterial(_viewerTwitch.Text, useCurvedTextMaterial);
                ApplyTextMaterial(_viewerYouTube.Text, useCurvedTextMaterial);
                ApplyTextMaterial(_viewerKick.Text, useCurvedTextMaterial);
                _viewerTwitch.Text.fontSize = config.ViewerCountSize;
                _viewerYouTube.Text.fontSize = config.ViewerCountSize;
                _viewerKick.Text.fontSize = config.ViewerCountSize;
                _viewerTwitch.Text.SetAllDirty();
                _viewerYouTube.Text.SetAllDirty();
                _viewerKick.Text.SetAllDirty();
            }
            if (_statusLabel != null)
            {
                ApplyTextMaterial(_statusLabel, useCurvedTextMaterial);
                _statusLabel.fontSize = StatusLabelFontSize;
                _statusLabel.SetAllDirty();
            }
            UpdateRowMetrics();
            _requestList?.SetPanelSize(config.PanelWidth, config.PanelHeight,
                ContentBottomInset, ContentTopInset);
            UpdateRequestListGeometry();
            _requestList?.ApplyAppearance();
            OffsetGrabHandleBehindPanel(); // ScreenSize change re-centers the handle
            _lastViewerLine = null;
            _nextViewerRefresh = 0f;
            _lastAppearanceWidth = config.PanelWidth;
            _lastAppearanceHeight = config.PanelHeight;
            _lastAppearanceFontSize = config.FontSize;
            if (widthChanged || heightChanged || fontChanged)
            {
                if (widthChanged || fontChanged)
                    _wrapCache.Clear();
                _lastVersion = -1;
                _trimFramesRemaining = TrimFramesAfterRebuild;
            }
            UpdateStatusLabel();
        }

        private void ApplyTextMaterial(TextMeshProUGUI text, bool useCurvedMaterial)
        {
            if (_curvedTextMaterial == null && text.fontSharedMaterial != null && text.fontSharedMaterial != _flatTextMaterial)
                _curvedTextMaterial = text.fontSharedMaterial;

            var material = useCurvedMaterial ? _curvedTextMaterial : GetFlatTextMaterial(text);
            if (material != null && text.fontSharedMaterial != material)
            {
                text.fontSharedMaterial = material;
                text.SetMaterialDirty();
            }
        }

        private Material? GetFlatTextMaterial(TextMeshProUGUI text)
        {
            if (_flatTextMaterial != null)
                return _flatTextMaterial;

            var baseMaterial = _curvedTextMaterial != null ? _curvedTextMaterial : text.fontSharedMaterial;
            if (baseMaterial == null)
                return null;

            _curvedTextMaterial ??= baseMaterial;
            _flatTextMaterial = new Material(baseMaterial)
            {
                name = baseMaterial.name + " (SimpleBSStreamChat Flat)"
            };
            _flatTextMaterial.DisableKeyword("CURVED");
            return _flatTextMaterial;
        }

        /// <summary>Unlocked = the entire panel is grabbable; locked = immovable.</summary>
        public void ApplyPinState()
        {
            if (_screen == null || _config == null)
                return;
            if (_config.PanelUnlocked)
            {
                _screen.HandleSide = FloatingScreen.Side.Full;
                _screen.ShowHandle = true;
            }
            else
            {
                _screen.ShowHandle = false;
            }
            UpdateLockIcon();
            UpdateMoveModeIcon();
            OffsetGrabHandleBehindPanel();
        }

        // HMUI's VRGraphicRaycaster, on a FLAT canvas, stops testing UI graphics
        // when a physics collider is nearer than the canvas plane. The Side.Full
        // grab handle is a full-panel cube collider centered ON the plane, so it
        // blocks clicks to the corner buttons — but only when flat (the curved
        // raycast path skips that distance check, which is why curving hid this).
        // Shove the handle to the far side of the panel from the viewer so the
        // buttons are hit first; grabbing empty panel area still hits it behind.
        private void OffsetGrabHandleBehindPanel()
        {
            if (_screen == null || _config == null || !_config.PanelUnlocked)
                return;
            var handle = _screen.transform.Find("FloatingScreenHandle");
            if (handle == null)
                return;
            var cam = Camera.main;
            var reference = cam != null ? cam.transform.position : Vector3.zero;
            var toViewer = Vector3.Dot(_screen.transform.forward, reference - _screen.transform.position);
            // Move along local +z (= screen forward) in whichever direction leads
            // away from the viewer.
            var dir = toViewer > 0f ? -1f : 1f;
            var depth = Mathf.Max(handle.localScale.z, 1f);
            handle.localPosition = new Vector3(0f, 0f, dir * depth);
        }

        private void ToggleLock()
        {
            if (_config == null)
                return;
            _config.PanelUnlocked = !_config.PanelUnlocked;
            ApplyPinState();
            FlashStatus(_config.PanelUnlocked ? "Unlocked" : "Locked");
        }

        private void ToggleMoveMode()
        {
            if (_config == null)
                return;
            _config.PanelMoveMode = (GetMoveMode() + 1) % MoveModeCount;
            UpdateMoveModeIcon();
            FlashStatus("Move: " + MoveModeName(GetMoveMode()));
        }

        private static string MoveModeName(int mode)
        {
            switch (mode)
            {
                case MoveModeX: return "X axis";
                case MoveModeY: return "Y axis";
                case MoveModeZ: return "Z axis";
                default: return "Free";
            }
        }

        // Padlock mirrors grabbability: gold open lock = movable, dim closed
        // lock = pinned. Hidden entirely if the icon strip failed to load
        // (the settings toggle still covers locking then).
        private void UpdateLockIcon()
        {
            if (_lockIcon == null || _config == null)
                return;
            var icons = _icons;
            if (icons == null)
            {
                if (_lockIcon.gameObject.activeSelf)
                    _lockIcon.gameObject.SetActive(false);
                return;
            }
            var unlocked = _config.PanelUnlocked;
            _lockIcon.sprite = unlocked ? icons.Unlock : icons.Lock;
            _lockIcon.color = unlocked
                ? new Color(1f, 0.85f, 0.3f, 0.95f)
                : new Color(1f, 1f, 1f, 0.5f);
            if (!_lockIcon.gameObject.activeSelf)
                _lockIcon.gameObject.SetActive(true);
        }

        private void UpdateMoveModeIcon()
        {
            if (_moveModeIcon == null || _config == null)
                return;
            var icons = _icons;
            if (icons == null)
            {
                if (_moveModeIcon.gameObject.activeSelf)
                    _moveModeIcon.gameObject.SetActive(false);
                return;
            }

            switch (GetMoveMode())
            {
                case MoveModeX:
                    _moveModeIcon.sprite = icons.MoveX;
                    break;
                case MoveModeY:
                    _moveModeIcon.sprite = icons.MoveY;
                    break;
                case MoveModeZ:
                    _moveModeIcon.sprite = icons.MoveZ;
                    break;
                default:
                    _moveModeIcon.sprite = icons.MoveFree;
                    break;
            }

            var alpha = _config.PanelUnlocked ? 0.9f : 0.45f;
            _moveModeIcon.color = new Color(0.35f, 0.85f, 1f, alpha);
            if (!_moveModeIcon.gameObject.activeSelf)
                _moveModeIcon.gameObject.SetActive(true);
        }

        private int GetMoveMode()
        {
            if (_config == null)
                return MoveModeFree;
            var mode = _config.PanelMoveMode;
            if (mode < MoveModeFree || mode >= MoveModeCount)
            {
                mode = MoveModeFree;
                _config.PanelMoveMode = mode;
            }
            return mode;
        }

        private void ApplyMoveConstraints()
        {
            if (_screen == null || _config == null)
                return;
            var t = _screen.transform;
            var position = t.position;
            switch (GetMoveMode())
            {
                case MoveModeX:
                    position = new Vector3(position.x, _grabStartPosition.y, _grabStartPosition.z);
                    break;
                case MoveModeY:
                    position = new Vector3(_grabStartPosition.x, position.y, _grabStartPosition.z);
                    break;
                case MoveModeZ:
                    position = new Vector3(_grabStartPosition.x, _grabStartPosition.y, position.z);
                    break;
            }

            t.SetPositionAndRotation(position, GetPanelRotation(position));
        }

        private Quaternion GetPanelRotation(Vector3 position)
        {
            var config = _config;
            if (config == null || !config.AlignWithFloor)
                return Quaternion.Euler(config?.RotationX ?? -25f, config?.RotationY ?? 0f, 0f);

            var fromFloorMarker = position - FloorMarkerPosition;
            var horizontal = new Vector3(fromFloorMarker.x, 0f, fromFloorMarker.z);
            if (horizontal.sqrMagnitude < 0.0001f)
                return Quaternion.Euler(config.RotationX, config.RotationY, 0f);
            var yaw = Mathf.Atan2(horizontal.x, horizontal.z) * Mathf.Rad2Deg;
            return Quaternion.Euler(config.RotationX, yaw, 0f);
        }

        private static Vector3 GetDefaultPanelPosition() => new Vector3(
            FloorMarkerPosition.x + PluginConfig.DefaultPositionX,
            FloorMarkerPosition.y + PluginConfig.DefaultPositionY,
            FloorMarkerPosition.z + PluginConfig.DefaultPositionZ);

        private void MigrateDefaultPanelPosition()
        {
            var config = _config;
            if (config == null)
                return;

            if (Approximately(config.PositionX, PluginConfig.DefaultPositionX) &&
                Approximately(config.PositionY, PluginConfig.PreviousDefaultPositionY) &&
                Approximately(config.PositionZ, PluginConfig.DefaultPositionZ))
            {
                config.PositionY = PluginConfig.DefaultPositionY;
                _log?.Info("Migrated chat panel default height to the higher recenter position.");
            }
        }

        private static bool Approximately(float a, float b) => Mathf.Abs(a - b) < 0.001f;

        public void ForceRefresh()
        {
            _wrapCache.Clear();
            _lastVersion = -1;
            _trimFramesRemaining = TrimFramesAfterRebuild;
        }

        private Vector2 GetScreenCanvasSize(PluginConfig config)
        {
            // The symmetric extra width is intentional: it keeps the visible
            // panel centered at its saved pose while the right sidecar remains
            // inside the FloatingScreen's clip/raycast bounds.
            var sidecarMargin = _requestManager == null ? 0f : RequestListView.SidecarCanvasExtent;
            return new Vector2(config.PanelWidth + sidecarMargin * 2f, config.PanelHeight);
        }

        private static void ClampPanelSize(PluginConfig config)
        {
            if (config.PanelWidth < PluginConfig.MinPanelWidth)
                config.PanelWidth = PluginConfig.MinPanelWidth;
        }

        /// <summary>Makes the viewer-count strip re-render on the next frame.</summary>
        public void RefreshViewerCounts()
        {
            _lastViewerLine = null;
            _nextViewerRefresh = 0f;
        }

        // Called from CreatePanel (once BSML is demonstrably ready) and from
        // MainMenuAwaiter.MainMenuInitializing after every menu (re)load.
        // BSML ignores a button whose text is already registered, so the
        // overlapping calls on first boot are harmless.
        private void RegisterMenuButton()
        {
            _menuButton ??= new MenuButton("SimpleBSStreamChat", "Twitch + YouTube + Kick stream chat settings", ShowSettingsFlow, true);
            MenuButtons.Instance.RegisterButton(_menuButton);
            _log?.Info("Registered Mods menu button.");
        }

        private void ShowSettingsFlow()
        {
            if (_settingsFlow == null)
            {
                _settingsFlow = BeatSaberUI.CreateFlowCoordinator<SettingsFlowCoordinator>();
                var view = BeatSaberUI.CreateViewController<SettingsViewController>();
                view.Setup(_config!, _chatManager!, this, _requestManager, _overlay);
                _settingsFlow.Setup(view);
            }
            BeatSaberUI.MainFlowCoordinator.PresentFlowCoordinator(_settingsFlow);
        }

        private void OnHandleGrabbed(object sender, FloatingScreenHandleEventArgs args)
        {
            _panelGrabbed = true;
            _grabStartPosition = args.Position;
            ApplyMoveConstraints();
        }

        private void OnHandleReleased(object sender, FloatingScreenHandleEventArgs args)
        {
            if (_panelGrabbed)
                ApplyMoveConstraints();
            _panelGrabbed = false;
            SavePanelTransform();
        }

        private void SavePanelTransform()
        {
            if (_screen == null || _config == null)
                return;
            var t = _screen.transform;
            var p = t.position;
            var r = t.rotation.eulerAngles;
            _config.PositionX = p.x;
            _config.PositionY = p.y;
            _config.PositionZ = p.z;
            _config.RotationX = r.x;
            _config.RotationY = r.y;
            _log?.Info($"Chat panel position saved ({p.x:F2}, {p.y:F2}, {p.z:F2}).");
        }

        // Chat text is untrusted: break any rich-text tag a viewer types by
        // inserting a zero-width space after '<'.
        private static string Sanitize(string text) =>
            text.IndexOf('<') >= 0 ? text.Replace("<", "<\u200B") : text;
    }
}
