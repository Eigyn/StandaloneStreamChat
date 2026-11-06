using System;
using System.Collections.Generic;
using System.Text;
using BeatSaberMarkupLanguage;
using SimpleBSStreamChat.Chat;
using SimpleBSStreamChat.Chat.Requests;
using TMPro;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.UI;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.UI
{
    /// <summary>
    /// Full-height, fixed-row request viewport. Rows are positioned manually
    /// so chevrons can move exactly one whole request at a time and no partial
    /// row is ever revealed at the viewport edge.
    /// </summary>
    internal class RequestListView
    {
        // The FloatingScreen canvas reserves this amount on both sides so the
        // right sidecar remains inside its RectMask2D and curved raycast bounds.
        public const float SidecarWidth = 10f;
        public const float SidecarGap = 3f;
        public const float SidecarCanvasExtent = SidecarWidth + SidecarGap;

        private const float RowSpacing = 0.8f;
        private const float SidecarButtonHeight = 7f;
        private const float SidecarPadding = 0.6f;
        private const float ScrollTrackHorizontalInset = 3f;
        private const float ScrollButtonFontSize = 3f;
        private const float MinimumHandleHeight = 2.5f;
        private const float HandleWidth = 1.2f; // thin centered indicator bar

        private class RequestRowUi
        {
            public GameObject Root = null!;
            public RectTransform Rect = null!;
            public HMUI.ImageView PlatformIcon = null!;
            public LayoutElement PlatformIconLayout = null!;
            public TextMeshProUGUI Text = null!;
            public ActionButtonUi Accept = null!;
            public ActionButtonUi Deny = null!;
            public GameObject ProgressRoot = null!;
            public HMUI.ImageView ProgressRing = null!;
            public LayoutElement ProgressLayout = null!;
            public SongRequest? Current;
        }

        private class ActionButtonUi
        {
            public GameObject Root = null!;
            public Button Button = null!;
            public Graphic Graphic = null!;
            public LayoutElement Layout = null!;
            public Color EnabledColor;
        }

        private static readonly Color AcceptTint = new Color(0.45f, 0.92f, 0.45f, 1f);
        private static readonly Color DenyTint = new Color(1f, 0.42f, 0.42f, 1f);
        private static Sprite? _progressRingSprite;

        private readonly SongRequestManager _manager;
        private readonly PluginConfig _config;
        private readonly IPALogger? _log;
        private readonly Action<TextMeshProUGUI> _styleText;
        private readonly ChatIcons? _icons;

        private readonly RectTransform _root;
        private readonly RectTransform _rowViewport;
        private readonly RectTransform _listRoot;
        private readonly TextMeshProUGUI _emptyLabel;
        private readonly RectTransform _sidecar;
        private readonly HMUI.ImageView _sidecarBackground;
        private readonly Button _scrollUpButton;
        private readonly Button _scrollDownButton;
        private readonly HMUI.ImageView _scrollUpVisual;
        private readonly HMUI.ImageView _scrollDownVisual;
        private readonly TextMeshProUGUI _scrollUpLabel;
        private readonly TextMeshProUGUI _scrollDownLabel;
        private readonly RectTransform _scrollTrack;
        private readonly HMUI.ImageView _scrollTrackVisual;
        private readonly RectTransform _scrollHandle;
        private readonly HMUI.ImageView _scrollHandleVisual;
        private readonly List<RequestRowUi> _rows = new List<RequestRowUi>();
        private readonly List<SongRequest> _snapshot = new List<SongRequest>();

        private int _lastVersion = -1;
        private int _firstVisibleIndex;
        private int _visibleRows = 1;
        private float _rowHeight = 1f;
        private float _chatTopSlack;
        private float _contentBottomInset = ChatPanelController.ContentBottomInset;
        private float _contentTopInset = ChatPanelController.ContentTopInset;
        // Whether accepting a request could actually navigate right now (true only
        // at the top-level menu). Polled so the Accept button dims the moment a song
        // starts or a menu screen (incl. the results screen) is opened.
        private bool _navigationActionable = true;
        private float _nextActionableCheck;

        public GameObject GameObject => _root.gameObject;

        public RequestListView(RectTransform maskParent, RectTransform sidecarCanvas, Sprite? sidecarBackgroundSprite,
            SongRequestManager manager, PluginConfig config, IPALogger? log,
            Action<TextMeshProUGUI> styleText, ChatIcons? icons)
        {
            _manager = manager;
            _config = config;
            _log = log;
            _styleText = styleText;
            _icons = icons;

            var rootGo = new GameObject("RequestsContent", typeof(RectTransform));
            _root = (RectTransform)rootGo.transform;
            _root.SetParent(maskParent, false);
            _root.anchorMin = Vector2.zero;
            _root.anchorMax = Vector2.one;
            _root.offsetMin = Vector2.zero;
            _root.offsetMax = Vector2.zero;

            // This inner mask deliberately uses only the height of a whole
            // number of rows. The root still spans the full chat mask so the
            // empty state remains centered in the available request space.
            var viewportGo = new GameObject("RequestRowsViewport", typeof(RectTransform), typeof(RectMask2D));
            _rowViewport = (RectTransform)viewportGo.transform;
            _rowViewport.SetParent(_root, false);
            _rowViewport.anchorMin = new Vector2(0f, 1f);
            _rowViewport.anchorMax = new Vector2(1f, 1f);
            _rowViewport.pivot = new Vector2(0.5f, 1f);
            _rowViewport.sizeDelta = new Vector2(0f, 1f);
            _rowViewport.anchoredPosition = Vector2.zero;

            var listGo = new GameObject("RequestRows", typeof(RectTransform));
            _listRoot = (RectTransform)listGo.transform;
            _listRoot.SetParent(_rowViewport, false);
            _listRoot.anchorMin = new Vector2(0f, 1f);
            _listRoot.anchorMax = new Vector2(1f, 1f);
            _listRoot.pivot = new Vector2(0.5f, 1f);
            _listRoot.sizeDelta = Vector2.zero;
            _listRoot.anchoredPosition = Vector2.zero;

            _emptyLabel = MakeText(_root, "No requests yet");
            _emptyLabel.alignment = TextAlignmentOptions.Center;
            _emptyLabel.fontStyle = FontStyles.Italic;
            var emptyRect = _emptyLabel.rectTransform;
            emptyRect.anchorMin = Vector2.zero;
            emptyRect.anchorMax = Vector2.one;
            emptyRect.offsetMin = Vector2.zero;
            emptyRect.offsetMax = Vector2.zero;

            _sidecar = CreateSidecar(sidecarCanvas, sidecarBackgroundSprite);
            _sidecarBackground = _sidecar.GetComponent<HMUI.ImageView>();
            _scrollUpButton = CreateSidecarButton("ScrollUp", "▲", true, out _scrollUpVisual, out _scrollUpLabel);
            _scrollDownButton = CreateSidecarButton("ScrollDown", "▼", false, out _scrollDownVisual, out _scrollDownLabel);
            _scrollUpButton.onClick.AddListener(() => ScrollByRows(-1));
            _scrollDownButton.onClick.AddListener(() => ScrollByRows(1));

            var trackGo = new GameObject("ScrollTrack", typeof(RectTransform));
            _scrollTrack = (RectTransform)trackGo.transform;
            _scrollTrack.SetParent(_sidecar, false);
            _scrollTrack.anchorMin = Vector2.zero;
            _scrollTrack.anchorMax = Vector2.one;
            _scrollTrack.offsetMin = new Vector2(ScrollTrackHorizontalInset,
                SidecarButtonHeight + SidecarPadding);
            _scrollTrack.offsetMax = new Vector2(-ScrollTrackHorizontalInset,
                -(SidecarButtonHeight + SidecarPadding));
            _scrollTrackVisual = trackGo.AddComponent<HMUI.ImageView>();
            _scrollTrackVisual.material = Utilities.ImageResources.NoGlowMat;
            _scrollTrackVisual.sprite = Utilities.ImageResources.WhitePixel;
            _scrollTrackVisual.color = new Color(1f, 1f, 1f, 0.16f);
            _scrollTrackVisual.raycastTarget = false;

            // The handle is a position/proportion indicator moved by the chevron
            // buttons (one row per press); it is sized and placed manually in
            // UpdateScrollbarHandle so it always stays fully inside the track.
            var handleGo = new GameObject("ScrollHandle", typeof(RectTransform));
            _scrollHandle = (RectTransform)handleGo.transform;
            _scrollHandle.SetParent(_scrollTrack, false);
            _scrollHandle.anchorMin = new Vector2(0.5f, 1f);
            _scrollHandle.anchorMax = new Vector2(0.5f, 1f);
            _scrollHandle.pivot = new Vector2(0.5f, 1f);
            _scrollHandle.sizeDelta = new Vector2(HandleWidth, MinimumHandleHeight);
            _scrollHandle.anchoredPosition = Vector2.zero;
            _scrollHandleVisual = handleGo.AddComponent<HMUI.ImageView>();
            _scrollHandleVisual.material = Utilities.ImageResources.NoGlowMat;
            _scrollHandleVisual.sprite = Utilities.ImageResources.WhitePixel;
            _scrollHandleVisual.color = new Color(0.95f, 0.82f, 0.29f, 0.95f);
            _scrollHandleVisual.raycastTarget = false;

            SetPanelSize(config.PanelWidth, config.PanelHeight,
                ChatPanelController.ContentBottomInset, ChatPanelController.ContentTopInset);
            rootGo.SetActive(false);
            _sidecar.gameObject.SetActive(false);
        }

        public void SetActive(bool active)
        {
            if (_root.gameObject.activeSelf == active)
                return;
            _root.gameObject.SetActive(active);
            if (!active)
            {
                _sidecar.gameObject.SetActive(false);
                return;
            }

            _lastVersion = -1;
            ApplyAppearance();
        }

        /// <summary>Main-thread poll; only rebuilds when queue/layout changes.</summary>
        public void Tick()
        {
            if (!_root.gameObject.activeSelf)
                return;
            var version = _manager.Version;
            if (version != _lastVersion)
            {
                _lastVersion = version;
                Rebuild();
            }
            RefreshActionable();
            UpdateBusyRows();
        }

        // Cheap throttled poll of whether navigation is currently possible.
        private void RefreshActionable()
        {
            if (Time.unscaledTime < _nextActionableCheck)
                return;
            _nextActionableCheck = Time.unscaledTime + 0.25f;
            _navigationActionable = ComputeActionable();
        }

        // Enabled only where accepting is actually allowed — the main menu and the
        // solo song list, but NOT the results screen or anywhere else. Shares the one
        // rule in MenuState with the navigator and refresh so they can't disagree.
        private static bool ComputeActionable() => Songs.MenuState.CanAcceptNow();

        private bool AcceptEnabled(SongRequest request) =>
            _navigationActionable &&
            (request.Status == SongRequestStatus.Ready || request.Status == SongRequestStatus.Failed);

        public void SetPanelSize(float panelWidth, float panelHeight,
            float contentBottomInset, float contentTopInset)
        {
            _contentBottomInset = contentBottomInset;
            _contentTopInset = contentTopInset;
            // Center the sidecar on the whole window (Y = 0 at the panel centre)
            // rather than on the content area, so it has equal margin above and
            // below regardless of the taller top band.
            _sidecar.anchoredPosition = new Vector2(panelWidth * 0.5f + SidecarGap, 0f);
            _sidecar.sizeDelta = new Vector2(SidecarWidth,
                Mathf.Max(1f, panelHeight - contentBottomInset - contentTopInset));
            _lastVersion = -1;
        }

        public void SetChatTopSlack(float value)
        {
            value = Mathf.Max(0f, value);
            if (Mathf.Approximately(_chatTopSlack, value))
                return;
            _chatTopSlack = value;
            _lastVersion = -1;
        }

        /// <summary>Re-applies curve-aware text/material styling and rebuilds
        /// the fixed-row geometry when the list is currently visible.</summary>
        public void ApplyAppearance()
        {
            StyleHierarchy(_root);
            StyleHierarchy(_sidecar);

            var tint = _config.BackgroundColor;
            _sidecarBackground.color = new Color(tint.r, tint.g, tint.b,
                Mathf.Clamp01(_config.BackgroundOpacity * 0.9f));
            _scrollTrackVisual.color = new Color(1f, 1f, 1f, 0.16f);
            _scrollHandleVisual.color = new Color(0.95f, 0.82f, 0.29f, 0.95f);
            _scrollUpLabel.fontSize = ScrollButtonFontSize;
            _scrollDownLabel.fontSize = ScrollButtonFontSize;
            _scrollUpLabel.SetAllDirty();
            _scrollDownLabel.SetAllDirty();
            _lastVersion = -1;
            if (_root.gameObject.activeSelf)
                Tick();
        }

        private void StyleHierarchy(RectTransform root)
        {
            foreach (var text in root.GetComponentsInChildren<TextMeshProUGUI>(true))
                _styleText(text);
            foreach (var image in root.GetComponentsInChildren<HMUI.ImageView>(true))
                image.SetAllDirty();
        }

        private RectTransform CreateSidecar(RectTransform canvas, Sprite? backgroundSprite)
        {
            var go = new GameObject("RequestScrollbarSidecar", typeof(RectTransform));
            var rect = (RectTransform)go.transform;
            rect.SetParent(canvas, false);
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0f, 0.5f);
            var background = go.AddComponent<HMUI.ImageView>();
            background.material = Utilities.ImageResources.NoGlowMat;
            background.sprite = backgroundSprite ?? Utilities.ImageResources.WhitePixel;
            background.type = backgroundSprite != null ? Image.Type.Sliced : Image.Type.Simple;
            background.raycastTarget = false;
            return rect;
        }

        private Button CreateSidecarButton(string name, string label, bool top,
            out HMUI.ImageView visual, out TextMeshProUGUI labelText)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_sidecar, false);
            rect.anchorMin = new Vector2(0.5f, top ? 1f : 0f);
            rect.anchorMax = new Vector2(0.5f, top ? 1f : 0f);
            rect.pivot = new Vector2(0.5f, top ? 1f : 0f);
            rect.anchoredPosition = new Vector2(0f, top ? -SidecarPadding : SidecarPadding);
            rect.sizeDelta = new Vector2(SidecarWidth - 1f, SidecarButtonHeight);

            // Transparent background: no gray square, but still the button's
            // raycast/click target (raycasting ignores alpha). The chevron glyph
            // itself carries the enabled/disabled state via its color.
            visual = go.AddComponent<HMUI.ImageView>();
            visual.material = Utilities.ImageResources.NoGlowMat;
            visual.sprite = Utilities.ImageResources.WhitePixel;
            visual.color = new Color(1f, 1f, 1f, 0f);
            visual.raycastTarget = true;

            labelText = MakeText(rect, label);
            labelText.alignment = TextAlignmentOptions.Center;
            labelText.fontSize = ScrollButtonFontSize;
            var textRect = labelText.rectTransform;
            textRect.anchorMin = Vector2.zero;
            textRect.anchorMax = Vector2.one;
            textRect.offsetMin = Vector2.zero;
            textRect.offsetMax = Vector2.zero;

            var button = go.AddComponent<Button>();
            button.targetGraphic = visual;
            return button;
        }

        private float RowHeight()
        {
            var font = _emptyLabel.font;
            var lineRatio = 1.2f;
            if (font != null && font.faceInfo.pointSize > 0f)
                lineRatio = font.faceInfo.lineHeight / font.faceInfo.pointSize;
            return Mathf.Max(1f, _config.FontSize * lineRatio * 2f + 3f);
        }

        private void Rebuild()
        {
            // Remember which request sat at the top (from the OLD snapshot) so the
            // viewport can stay anchored to it across add/remove/auto-purge.
            string? topRequestId = (_firstVisibleIndex >= 0 && _firstVisibleIndex < _snapshot.Count)
                ? _snapshot[_firstVisibleIndex].Id
                : null;

            _manager.CopyRequests(_snapshot);
            _rowHeight = RowHeight();
            RefreshViewportGeometry();

            if (topRequestId != null)
            {
                for (var i = 0; i < _snapshot.Count; i++)
                {
                    if (_snapshot[i].Id == topRequestId)
                    {
                        _firstVisibleIndex = i;
                        break;
                    }
                }
            }

            while (_rows.Count < _snapshot.Count)
                _rows.Add(CreateRow());

            _emptyLabel.gameObject.SetActive(_snapshot.Count == 0);
            // ApplyScroll positions, formats, and activates only the on-screen rows
            // (and deactivates the rest), so a large queue isn't fully re-formatted
            // on every version bump — e.g. the per-1% bumps during a download.
            ApplyScroll();
        }

        private void RefreshViewportGeometry()
        {
            var availableHeight = _root.rect.height;
            if (availableHeight <= 0.01f)
                availableHeight = Mathf.Max(1f, _config.PanelHeight - _contentBottomInset - _contentTopInset);
            availableHeight = Mathf.Max(1f, availableHeight - _chatTopSlack);
            var stride = RowStride;
            _visibleRows = Mathf.Max(1, Mathf.FloorToInt((availableHeight + RowSpacing) / stride));
            var fullRowsHeight = _visibleRows * _rowHeight + Mathf.Max(0, _visibleRows - 1) * RowSpacing;
            _rowViewport.sizeDelta = new Vector2(0f, fullRowsHeight);
            _rowViewport.anchoredPosition = new Vector2(0f, -_chatTopSlack);
            _listRoot.sizeDelta = new Vector2(0f, ContentHeight(_snapshot.Count));
        }

        private float RowStride => _rowHeight + RowSpacing;

        private float ContentHeight(int count) => count <= 0 ? 0f :
            count * _rowHeight + Mathf.Max(0, count - 1) * RowSpacing;

        private int MaxFirstVisibleIndex => Mathf.Max(0, _snapshot.Count - _visibleRows);

        private void PopulateRow(RequestRowUi row, int index)
        {
            var request = _snapshot[index];
            row.Current = request;
            PositionRow(row, index);
            row.Text.text = Format(index + 1, request);
            ApplyPlatformIcon(row, request.RequesterPlatform);
            ApplyRowState(row, request);
        }

        private void PositionRow(RequestRowUi row, int index)
        {
            row.Rect.anchorMin = new Vector2(0f, 1f);
            row.Rect.anchorMax = new Vector2(1f, 1f);
            row.Rect.pivot = new Vector2(0.5f, 1f);
            row.Rect.sizeDelta = new Vector2(0f, _rowHeight);
            row.Rect.anchoredPosition = new Vector2(0f, -index * RowStride);

            // Pin minWidth == preferredWidth on the fixed controls so a row with a
            // long name can't make the HorizontalLayoutGroup shrink the icon/
            // accept/deny/progress. Only the flexible text shrinks and ellipsizes,
            // so every row's buttons stay the same size at any panel width.
            var controlSize = _rowHeight * 0.55f;
            var progressSize = _rowHeight * 1.22f;
            row.PlatformIconLayout.preferredWidth = controlSize;
            row.PlatformIconLayout.preferredHeight = controlSize;
            row.PlatformIconLayout.minWidth = controlSize;
            row.Accept.Layout.preferredWidth = controlSize;
            row.Accept.Layout.minWidth = controlSize;
            row.Deny.Layout.preferredWidth = controlSize;
            row.Deny.Layout.minWidth = controlSize;
            row.ProgressLayout.preferredWidth = progressSize;
            row.ProgressLayout.minWidth = progressSize;
        }

        private void ScrollByRows(int delta)
        {
            _firstVisibleIndex = Mathf.Clamp(_firstVisibleIndex + delta, 0, MaxFirstVisibleIndex);
            ApplyScroll();
        }

        private void ApplyScroll()
        {
            _firstVisibleIndex = Mathf.Clamp(_firstVisibleIndex, 0, MaxFirstVisibleIndex);
            _listRoot.anchoredPosition = new Vector2(0f, _firstVisibleIndex * RowStride);

            var lastVisibleExclusive = Mathf.Min(_snapshot.Count, _firstVisibleIndex + _visibleRows);
            for (var i = 0; i < _rows.Count; i++)
            {
                var row = _rows[i];
                var visible = i >= _firstVisibleIndex && i < lastVisibleExclusive;
                if (visible)
                    PopulateRow(row, i);      // format/position only what's on screen
                else
                    row.Current = null;
                if (row.Root.activeSelf != visible)
                    row.Root.SetActive(visible);
            }

            var overflows = _snapshot.Count > _visibleRows;
            if (_sidecar.gameObject.activeSelf != (_root.gameObject.activeSelf && overflows))
                _sidecar.gameObject.SetActive(_root.gameObject.activeSelf && overflows);
            SetScrollButtonState(_scrollUpButton, _scrollUpLabel, _firstVisibleIndex > 0);
            SetScrollButtonState(_scrollDownButton, _scrollDownLabel,
                _firstVisibleIndex < MaxFirstVisibleIndex);
            UpdateScrollbarHandle();
        }

        // Size the handle to the visible fraction and slide it down the track by
        // the scroll fraction, keeping the whole handle inside the track at every
        // position (a top-pivot handle anchored at a single point — as a Unity
        // Slider does it — clips below the track at the bottom).
        private void UpdateScrollbarHandle()
        {
            var trackHeight = Mathf.Max(0f, _scrollTrack.rect.height);
            if (trackHeight <= 0f)
                return;

            var fraction = _snapshot.Count <= 0 ? 1f :
                Mathf.Clamp01(_visibleRows / (float)_snapshot.Count);
            var handleHeight = Mathf.Clamp(trackHeight * fraction, Mathf.Min(MinimumHandleHeight, trackHeight), trackHeight);
            _scrollHandle.sizeDelta = new Vector2(HandleWidth, handleHeight);

            var scrollFraction = MaxFirstVisibleIndex == 0 ? 0f :
                _firstVisibleIndex / (float)MaxFirstVisibleIndex;
            _scrollHandle.anchoredPosition = new Vector2(0f, -scrollFraction * (trackHeight - handleHeight));
        }

        private static void SetScrollButtonState(Button button, TextMeshProUGUI label, bool enabled)
        {
            button.interactable = enabled;
            label.color = enabled
                ? new Color(1f, 1f, 1f, 1f)
                : new Color(1f, 1f, 1f, 0.3f);
        }

        private RequestRowUi CreateRow()
        {
            var rowGo = new GameObject("RequestRow", typeof(RectTransform));
            var rect = (RectTransform)rowGo.transform;
            rect.SetParent(_listRoot, false);
            var bg = rowGo.AddComponent<HMUI.ImageView>();
            bg.material = Utilities.ImageResources.NoGlowMat;
            bg.sprite = Utilities.ImageResources.WhitePixel;
            bg.color = new Color(1f, 1f, 1f, 0.06f);
            bg.raycastTarget = false;
            var layout = rowGo.AddComponent<HorizontalLayoutGroup>();
            layout.padding = new RectOffset(2, 2, 1, 1);
            layout.spacing = 1.2f;
            layout.childControlWidth = true;
            layout.childControlHeight = true;
            layout.childForceExpandWidth = false;
            layout.childForceExpandHeight = true;
            layout.childAlignment = TextAnchor.MiddleLeft;

            var (platformIcon, platformIconLayout) = MakeIcon(rowGo.transform);
            var text = MakeText(rect, "");
            text.overflowMode = TextOverflowModes.Ellipsis;
            var textElement = text.gameObject.AddComponent<LayoutElement>();
            textElement.flexibleWidth = 1f;
            textElement.minWidth = 0f; // absorbs all row-width pressure (ellipsizes)

            var row = new RequestRowUi
            {
                Root = rowGo,
                Rect = rect,
                PlatformIcon = platformIcon,
                PlatformIconLayout = platformIconLayout,
                Text = text,
            };
            row.Accept = CreateActionButton(rowGo.transform, _icons?.Accept, AcceptTint, "✓",
                () => { if (row.Current != null) _manager.Accept(row.Current.Id); });
            row.Deny = CreateActionButton(rowGo.transform, _icons?.Deny, DenyTint, "✕",
                () => { if (row.Current != null) _manager.Deny(row.Current.Id); });
            row.ProgressRing = CreateProgressRing(rowGo.transform, out var progressRoot, out var progressLayout);
            row.ProgressRoot = progressRoot;
            row.ProgressLayout = progressLayout;
            rowGo.SetActive(false);
            return row;
        }

        private (HMUI.ImageView Image, LayoutElement Layout) MakeIcon(Transform parent)
        {
            var go = new GameObject("Icon", typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var image = go.AddComponent<HMUI.ImageView>();
            image.material = Utilities.ImageResources.NoGlowMat;
            image.preserveAspect = true;
            image.raycastTarget = false;
            var element = go.AddComponent<LayoutElement>();
            go.SetActive(false);
            return (image, element);
        }

        private ActionButtonUi CreateActionButton(Transform parent, Sprite? sprite, Color tint,
            string fallbackLabel, UnityAction onClick)
        {
            var go = new GameObject("ActionButton", typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var element = go.AddComponent<LayoutElement>();
            element.flexibleWidth = 0f;

            Graphic target;
            if (sprite != null)
            {
                var icon = go.AddComponent<HMUI.ImageView>();
                icon.material = Utilities.ImageResources.NoGlowMat;
                icon.sprite = sprite;
                icon.color = tint;
                icon.preserveAspect = true;
                icon.raycastTarget = true;
                target = icon;
            }
            else
            {
                var bg = go.AddComponent<HMUI.ImageView>();
                bg.material = Utilities.ImageResources.NoGlowMat;
                bg.sprite = Utilities.ImageResources.WhitePixel;
                bg.color = new Color(1f, 1f, 1f, 0.16f);
                bg.raycastTarget = true;
                var text = MakeText((RectTransform)go.transform, fallbackLabel);
                text.alignment = TextAlignmentOptions.Center;
                text.color = tint;
                var textRect = text.rectTransform;
                textRect.anchorMin = Vector2.zero;
                textRect.anchorMax = Vector2.one;
                textRect.offsetMin = Vector2.zero;
                textRect.offsetMax = Vector2.zero;
                target = bg;
            }

            var button = go.AddComponent<Button>();
            button.targetGraphic = target;
            button.onClick.AddListener(onClick);
            return new ActionButtonUi
            {
                Root = go,
                Button = button,
                Graphic = target,
                Layout = element,
                EnabledColor = tint,
            };
        }

        private HMUI.ImageView CreateProgressRing(Transform parent, out GameObject root,
            out LayoutElement layout)
        {
            root = new GameObject("ProgressRing", typeof(RectTransform));
            root.transform.SetParent(parent, false);
            layout = root.AddComponent<LayoutElement>();
            layout.flexibleWidth = 0f;

            var image = root.AddComponent<HMUI.ImageView>();
            image.material = Utilities.ImageResources.NoGlowMat;
            image.sprite = ProgressRingSprite();
            image.color = new Color(1f, 0.82f, 0.29f, 0.95f);
            image.preserveAspect = true;
            image.raycastTarget = false;
            image.type = Image.Type.Filled;
            image.fillMethod = Image.FillMethod.Radial360;
            image.fillOrigin = 2;
            image.fillClockwise = true;
            image.fillAmount = 0f;
            root.SetActive(false);
            return image;
        }

        private void ApplyRowState(RequestRowUi row, SongRequest request)
        {
            var busy = request.Status == SongRequestStatus.Downloading ||
                       request.Status == SongRequestStatus.Installing;
            row.Accept.Root.SetActive(!busy);
            row.Deny.Root.SetActive(!busy);
            row.ProgressRoot.SetActive(busy);
            SetButtonEnabled(row.Accept, AcceptEnabled(request));
            SetButtonEnabled(row.Deny, !busy);
            UpdateProgressRing(row, request);
        }

        private void UpdateBusyRows()
        {
            foreach (var row in _rows)
            {
                if (!row.Root.activeSelf || row.Current == null)
                    continue;
                var request = row.Current;
                // Keep the Accept button's enabled state live between rebuilds so it
                // dims/undims immediately as navigation becomes (im)possible, rather
                // than only on the next queue-version change.
                if (row.Accept.Root.activeSelf)
                {
                    var enabled = AcceptEnabled(request);
                    if (row.Accept.Button.interactable != enabled)
                        SetButtonEnabled(row.Accept, enabled);
                }
                UpdateProgressRing(row, request);
            }
        }

        private static void SetButtonEnabled(ActionButtonUi action, bool enabled)
        {
            action.Button.interactable = enabled;
            action.Graphic.color = enabled
                ? action.EnabledColor
                : new Color(action.EnabledColor.r, action.EnabledColor.g, action.EnabledColor.b, 0.35f);
        }

        private static void UpdateProgressRing(RequestRowUi row, SongRequest request)
        {
            if (request.Status == SongRequestStatus.Downloading)
            {
                row.ProgressRing.fillAmount = Mathf.Clamp01(Mathf.Max(0.04f, request.DownloadProgress));
                row.ProgressRing.rectTransform.localRotation = Quaternion.identity;
            }
            else if (request.Status == SongRequestStatus.Installing)
            {
                row.ProgressRing.fillAmount = 0.72f;
                row.ProgressRing.rectTransform.localRotation =
                    Quaternion.Euler(0f, 0f, -Time.unscaledTime * 240f);
            }
        }

        private static Sprite ProgressRingSprite()
        {
            if (_progressRingSprite != null)
                return _progressRingSprite;

            const int size = 64;
            const float outer = 29f;
            const float inner = 21f;
            var texture = new Texture2D(size, size, TextureFormat.RGBA32, false);
            texture.wrapMode = TextureWrapMode.Clamp;
            for (var y = 0; y < size; y++)
            {
                for (var x = 0; x < size; x++)
                {
                    var dx = x + 0.5f - size * 0.5f;
                    var dy = y + 0.5f - size * 0.5f;
                    var d = Mathf.Sqrt(dx * dx + dy * dy);
                    var alpha = Mathf.Clamp01(Mathf.Min(outer - d, d - inner));
                    texture.SetPixel(x, y, new Color(1f, 1f, 1f, alpha));
                }
            }
            texture.Apply(false, true);
            _progressRingSprite = Sprite.Create(texture, new Rect(0f, 0f, size, size),
                new Vector2(0.5f, 0.5f), size);
            return _progressRingSprite;
        }

        private void ApplyPlatformIcon(RequestRowUi row, ChatPlatform platform)
        {
            Sprite? sprite = _icons == null ? null
                : platform == ChatPlatform.Twitch ? _icons.Twitch
                : platform == ChatPlatform.YouTube ? _icons.YouTube
                : platform == ChatPlatform.Kick ? _icons.Kick
                : null;
            if (sprite != null)
            {
                row.PlatformIcon.sprite = sprite;
                if (!row.PlatformIcon.gameObject.activeSelf)
                    row.PlatformIcon.gameObject.SetActive(true);
            }
            else if (row.PlatformIcon.gameObject.activeSelf)
            {
                row.PlatformIcon.gameObject.SetActive(false);
            }
        }

        private TextMeshProUGUI MakeText(RectTransform parent, string content)
        {
            var text = BeatSaberUI.CreateText(parent, content, Vector2.zero, Vector2.zero);
            text.alignment = TextAlignmentOptions.Left;
            text.enableWordWrapping = false;
            text.richText = true;
            text.raycastTarget = false;
            _styleText(text);
            return text;
        }

        private static string Format(int position, SongRequest request)
        {
            var sb = new StringBuilder(160);
            sb.Append("<color=#FFD24A>#").Append(position).Append("</color> ");
            sb.Append(Sanitize(request.RequesterName));
            sb.Append('\n');

            if (request.Status == SongRequestStatus.FetchingInfo)
            {
                sb.Append("<color=#999999><i>loading…</i> · <color=#7FD4FF>")
                  .Append(request.Key).Append("</color></color>");
                return sb.ToString();
            }

            if (request.Status == SongRequestStatus.Downloading ||
                request.Status == SongRequestStatus.Installing)
            {
                sb.Append(StatusLine(request));
                return sb.ToString();
            }

            sb.Append("<color=#DDDDDD>");
            if (!string.IsNullOrEmpty(request.SongName))
                sb.Append(Sanitize(request.SongName));
            sb.Append("</color><color=#999999>");
            if (!string.IsNullOrEmpty(request.SongAuthorName))
                sb.Append(" · ").Append(Sanitize(request.SongAuthorName));
            if (!string.IsNullOrEmpty(request.LevelAuthorName))
                sb.Append(" · ").Append(Sanitize(request.LevelAuthorName));
            sb.Append(" · </color><color=#7FD4FF>").Append(request.Key).Append("</color>");

            var status = StatusLabel(request);
            if (status != null)
                sb.Append(status);
            return sb.ToString();
        }

        private static string StatusLine(SongRequest request)
        {
            var detail = string.IsNullOrEmpty(request.StatusDetail) ? "" : " " + Sanitize(request.StatusDetail);
            switch (request.Status)
            {
                case SongRequestStatus.Downloading:
                    return "<color=#FFD24A>Downloading" + detail + "</color>";
                case SongRequestStatus.Installing:
                    return "<color=#FFD24A>Installing</color>";
                default:
                    return "";
            }
        }

        private static string? StatusLabel(SongRequest request)
        {
            var detail = string.IsNullOrEmpty(request.StatusDetail) ? "" : " " + Sanitize(request.StatusDetail);
            switch (request.Status)
            {
                case SongRequestStatus.Ready:
                    return string.IsNullOrEmpty(detail) ? null : " <color=#FFD24A>[" + detail.TrimStart() + "]</color>";
                case SongRequestStatus.NotFound: return " <color=#FF6B6B>[not found]</color>";
                case SongRequestStatus.Failed: return " <color=#FF6B6B>[failed" + detail + "]</color>";
                default: return null;
            }
        }

        // Zero-width space after '<' so viewer text cannot inject rich-text tags.
        private static string Sanitize(string text) => text.Replace("<", "<​");
    }
}
