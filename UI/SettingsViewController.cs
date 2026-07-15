using System.Collections.Generic;
using BeatSaberMarkupLanguage.Attributes;
using BeatSaberMarkupLanguage.ViewControllers;
using SimpleBSStreamChat.Chat;
using SimpleBSStreamChat.Chat.Requests;
using UnityEngine;

namespace SimpleBSStreamChat.UI
{
    /// <summary>
    /// Settings view shown by the left-panel Mods → "Stream Chat" button.
    /// Setters write straight to the BSIPA generated config (auto-saved) and
    /// apply live: channel/enable changes restart the chat services, UI
    /// changes re-style the panel in place.
    /// </summary>
    public class SettingsViewController : BSMLResourceViewController
    {
        public override string ResourceName => "SimpleBSStreamChat.UI.Settings.bsml";

        private PluginConfig _config = null!;
        private ChatManager _chatManager = null!;
        private ChatPanelController _panel = null!;
        private SongRequestManager? _requestManager;
        private MapOverlayController? _overlay;
        private bool _liveSettingsReady;

        // Order matches PluginConfig.OverlayPosition indices 0..7.
        private static readonly string[] OverlayPositionNames =
        {
            "Top Left", "Top Center", "Top Right", "Right",
            "Bottom Right", "Bottom Center", "Bottom Left", "Left",
        };

        public void Setup(PluginConfig config, ChatManager chatManager, ChatPanelController panel,
            SongRequestManager? requestManager, MapOverlayController? overlay)
        {
            _config = config;
            _chatManager = chatManager;
            _panel = panel;
            _requestManager = requestManager;
            _overlay = overlay;
            _liveSettingsReady = false;
        }

        protected override void DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling)
        {
            base.DidActivate(firstActivation, addedToHierarchy, screenSystemEnabling);
            _liveSettingsReady = true;
        }

        protected override void DidDeactivate(bool removedFromHierarchy, bool screenSystemDisabling)
        {
            _liveSettingsReady = false;
            base.DidDeactivate(removedFromHierarchy, screenSystemDisabling);
        }

        [UIValue("show-window")]
        public bool ShowWindow
        {
            get => _config.ShowPanel;
            set { _config.ShowPanel = value; _panel.ApplyVisibility(); }
        }

        [UIValue("twitch-channel")]
        public string TwitchChannel
        {
            get => _config.TwitchChannel;
            set
            {
                var next = value ?? "";
                if (_config.TwitchChannel == next)
                    return;
                _config.TwitchChannel = next;
                if (_liveSettingsReady)
                    _chatManager.RestartPlatform(ChatPlatform.Twitch, clearMessages: true);
            }
        }

        [UIValue("enable-twitch")]
        public bool EnableTwitch
        {
            get => _config.EnableTwitch;
            set
            {
                if (_config.EnableTwitch == value)
                    return;
                _config.EnableTwitch = value;
                if (_liveSettingsReady)
                {
                    _chatManager.RestartPlatform(ChatPlatform.Twitch, clearMessages: false);
                    _panel.RefreshViewerCounts();
                }
            }
        }

        [UIValue("youtube-channel")]
        public string YouTubeChannel
        {
            get => _config.YouTubeChannel;
            set
            {
                var next = value ?? "";
                if (_config.YouTubeChannel == next)
                    return;
                _config.YouTubeChannel = next;
                if (_liveSettingsReady)
                    _chatManager.RestartPlatform(ChatPlatform.YouTube, clearMessages: true);
            }
        }

        [UIValue("enable-youtube")]
        public bool EnableYouTube
        {
            get => _config.EnableYouTube;
            set
            {
                if (_config.EnableYouTube == value)
                    return;
                _config.EnableYouTube = value;
                if (_liveSettingsReady)
                {
                    _chatManager.RestartPlatform(ChatPlatform.YouTube, clearMessages: false);
                    _panel.RefreshViewerCounts();
                }
            }
        }

        [UIValue("kick-channel")]
        public string KickChannel
        {
            get => _config.KickChannel;
            set
            {
                var next = value ?? "";
                if (_config.KickChannel == next)
                    return;
                _config.KickChannel = next;
                if (_liveSettingsReady)
                    _chatManager.RestartPlatform(ChatPlatform.Kick, clearMessages: true);
            }
        }

        [UIValue("enable-kick")]
        public bool EnableKick
        {
            get => _config.EnableKick;
            set
            {
                if (_config.EnableKick == value)
                    return;
                _config.EnableKick = value;
                if (_liveSettingsReady)
                {
                    _chatManager.RestartPlatform(ChatPlatform.Kick, clearMessages: false);
                    _panel.RefreshViewerCounts();
                }
            }
        }

        [UIValue("show-viewer-counts")]
        public bool ShowViewerCounts
        {
            get => _config.ShowViewerCounts;
            set { _config.ShowViewerCounts = value; _panel.RefreshViewerCounts(); }
        }

        [UIValue("viewer-count-size")]
        public float ViewerCountSize
        {
            get => _config.ViewerCountSize;
            set { _config.ViewerCountSize = value; _panel.ApplyAppearance(); }
        }

        [UIValue("curve-amount")]
        public float CurveAmount
        {
            get => _config.CurveAmount;
            set { _config.CurveAmount = value; _panel.ApplyAppearance(); }
        }

        [UIValue("highlight-messages")]
        public bool HighlightMessages
        {
            get => _config.HighlightMessages;
            set { _config.HighlightMessages = value; _panel.ForceRefresh(); }
        }

        [UIValue("background-color")]
        public Color BackgroundColor
        {
            get => _config.BackgroundColor;
            set { _config.BackgroundColor = value; _panel.ApplyAppearance(); }
        }

        [UIValue("background-opacity")]
        public float BackgroundOpacity
        {
            get => _config.BackgroundOpacity;
            set { _config.BackgroundOpacity = value; _panel.ApplyAppearance(); }
        }

        [UIValue("highlight-opacity")]
        public float HighlightOpacity
        {
            get => _config.HighlightOpacity;
            set { _config.HighlightOpacity = value; _panel.ForceRefresh(); }
        }

        [UIValue("platform-icons")]
        public bool PlatformIcons
        {
            get => _config.ShowPlatformIcons;
            set { _config.ShowPlatformIcons = value; _panel.ForceRefresh(); }
        }

        [UIValue("emoji-images")]
        public bool EmojiImages
        {
            get => _config.EmojiImages;
            set
            {
                _config.EmojiImages = value;
                _config.CustomEmotes = value;
                _config.AnimatedEmotes = value;
                _config.ThirdPartyEmotes = value;
                _config.SevenTvEmotes = value;
                _config.BttvEmotes = value;
                _config.FfzEmotes = value;
                _panel.ForceRefresh();
            }
        }

        [UIValue("show-system")]
        public bool ShowSystemMessages
        {
            get => _config.ShowSystemMessages;
            set { _config.ShowSystemMessages = value; _panel.ForceRefresh(); }
        }

        [UIValue("font-size")]
        public float FontSize
        {
            get => _config.FontSize;
            set { _config.FontSize = value; _panel.ApplyAppearance(); }
        }

        [UIValue("panel-width")]
        public float PanelWidth
        {
            get => _config.PanelWidth;
            set { _config.PanelWidth = Mathf.Max(PluginConfig.MinPanelWidth, value); _panel.ApplyAppearance(); }
        }

        [UIValue("panel-height")]
        public float PanelHeight
        {
            get => _config.PanelHeight;
            set { _config.PanelHeight = value; _panel.ApplyAppearance(); }
        }

        [UIValue("enable-song-requests")]
        public bool EnableSongRequests
        {
            get => _config.EnableSongRequests;
            set { _config.EnableSongRequests = value; _overlay?.ApplySettings(); }
        }

        // Switches the request command between "!request" (off) and "!bsr" (on).
        // The parser and the overlay's "… is available" label both read RequestCommand.
        [UIValue("use-bsr-command")]
        public bool UseBsrCommand
        {
            get => (_config.RequestCommand ?? "").Trim().Equals("!bsr", System.StringComparison.OrdinalIgnoreCase);
            set { _config.RequestCommand = value ? "!bsr" : "!request"; _overlay?.ApplySettings(); }
        }

        [UIValue("request-cooldown")]
        public int RequestCooldown
        {
            get => _config.PerUserCooldownSeconds;
            set { _config.PerUserCooldownSeconds = Mathf.Clamp(value, 0, 600); }
        }

        [UIValue("allow-duplicates")]
        public bool AllowDuplicates
        {
            get => _config.AllowDuplicates;
            set { _config.AllowDuplicates = value; }
        }

        [UIValue("persist-request-queue")]
        public bool PersistRequestQueue
        {
            get => _config.PersistRequestQueue;
            set
            {
                _config.PersistRequestQueue = value;
                _requestManager?.SaveQueue();
            }
        }

        [UIValue("overlay-song-name")]
        public bool OverlaySongName
        {
            get => _config.OverlayShowSongName;
            set { _config.OverlayShowSongName = value; _overlay?.ApplySettings(); }
        }

        [UIValue("overlay-song-author")]
        public bool OverlaySongAuthor
        {
            get => _config.OverlayShowSongAuthor;
            set { _config.OverlayShowSongAuthor = value; _overlay?.ApplySettings(); }
        }

        [UIValue("overlay-cover")]
        public bool OverlayCover
        {
            get => _config.OverlayShowCover;
            set { _config.OverlayShowCover = value; _overlay?.ApplySettings(); }
        }

        [UIValue("overlay-mapper")]
        public bool OverlayMapper
        {
            get => _config.OverlayShowMapper;
            set { _config.OverlayShowMapper = value; _overlay?.ApplySettings(); }
        }

        [UIValue("overlay-duration")]
        public bool OverlayDuration
        {
            get => _config.OverlayShowDuration;
            set { _config.OverlayShowDuration = value; _overlay?.ApplySettings(); }
        }

        [UIValue("overlay-difficulty")]
        public bool OverlayDifficulty
        {
            get => _config.OverlayShowDifficulty;
            set { _config.OverlayShowDifficulty = value; _overlay?.ApplySettings(); }
        }

        [UIValue("overlay-position-options")]
        public List<object> OverlayPositionOptions { get; } = new List<object>(OverlayPositionNames);

        [UIValue("overlay-position")]
        public string OverlayPosition
        {
            get
            {
                var index = Mathf.Clamp(_config.OverlayPosition, 0, OverlayPositionNames.Length - 1);
                return OverlayPositionNames[index];
            }
            set
            {
                var index = System.Array.IndexOf(OverlayPositionNames, value);
                _config.OverlayPosition = index < 0 ? 0 : index;
                _overlay?.ApplySettings();
            }
        }

        [UIAction("recenter-window")]
        public void RecenterWindow()
        {
            _panel.RecenterPanel();
        }
    }
}
