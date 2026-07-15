using System.Runtime.CompilerServices;
using IPA.Config.Stores;
using IPA.Config.Stores.Attributes;
using IPA.Config.Stores.Converters;
using UnityEngine;

[assembly: InternalsVisibleTo(GeneratedStore.AssemblyVisibilityTarget)]

namespace SimpleBSStreamChat
{
    public class PluginConfig
    {
        public const float DefaultPositionX = 0f;
        public const float DefaultPositionY = 4.4f;
        public const float DefaultPositionZ = 4.0f;
        public const float PreviousDefaultPositionY = 3.6f;
        public const float DefaultRotationX = -25f;
        public const float DefaultRotationY = 0f;
        public const float MinPanelWidth = 70f;

        public static PluginConfig Instance { get; set; } = null!;

        // Twitch channel name, e.g. "myname" (no #). Anonymous read-only connection.
        public virtual string TwitchChannel { get; set; } = "";
        public virtual bool EnableTwitch { get; set; } = true;

        // YouTube handle ("@myname"), channel id ("UC…") or an 11-char video id.
        public virtual string YouTubeChannel { get; set; } = "";
        public virtual bool EnableYouTube { get; set; } = true;

        // Kick channel slug (e.g. "myname") or numeric chatroom id.
        public virtual string KickChannel { get; set; } = "";
        public virtual bool EnableKick { get; set; } = false;

        public virtual float FontSize { get; set; } = 3.4f;
        public virtual float ViewerCountSize { get; set; } = 2.9f;
        public virtual float PanelWidth { get; set; } = 90f;
        public virtual float PanelHeight { get; set; } = 65f;
        public virtual float PositionX { get; set; } = DefaultPositionX;
        public virtual float PositionY { get; set; } = DefaultPositionY;
        public virtual float PositionZ { get; set; } = DefaultPositionZ;
        public virtual float RotationX { get; set; } = DefaultRotationX;
        public virtual float RotationY { get; set; } = DefaultRotationY;
        public virtual bool ShowPanel { get; set; } = true;
        // 0 = flat; higher = stronger curve (radius = 1200 / amount).
        public virtual float CurveAmount { get; set; } = 0f;
        // Window backdrop tint (stored as hex), rendered literally on the
        // NoGlow material. Alpha comes from BackgroundOpacity, not from here.
        [UseConverter(typeof(HexColorConverter))]
        public virtual Color BackgroundColor { get; set; } = Color.black;
        public virtual float BackgroundOpacity { get; set; } = 0.6f;
        public virtual bool HighlightMessages { get; set; } = true;
        // Darkness of the per-message backdrop when HighlightMessages is on.
        public virtual float HighlightOpacity { get; set; } = 0.55f;
        // Image icons for platform + broadcaster/mod badges; off = [T]/[Y] text tags.
        public virtual bool ShowPlatformIcons { get; set; } = true;
        // Master image toggle: Unicode Twemoji plus platform and provider
        // emotes. Off = plain text characters/names.
        public virtual bool EmojiImages { get; set; } = true;
        // Legacy advanced flags from the brief multi-toggle build. The
        // settings UI now syncs them to EmojiImages, and the renderer treats
        // EmojiImages as the master switch.
        public virtual bool CustomEmotes { get; set; } = true;
        public virtual bool AnimatedEmotes { get; set; } = true;
        public virtual bool ThirdPartyEmotes { get; set; } = true;
        public virtual bool SevenTvEmotes { get; set; } = true;
        public virtual bool BttvEmotes { get; set; } = true;
        public virtual bool FfzEmotes { get; set; } = true;
        // Provider priority for exact name collisions. Later channel-specific
        // emotes always beat global emotes from the same provider.
        public virtual string EmoteProviderPriority { get; set; } = "7TV,BTTV,FFZ";
        public virtual bool ShowSystemMessages { get; set; } = true;
        public virtual bool ShowViewerCounts { get; set; } = true;
        // Unlocked = whole panel is a grab handle; locked = handle hidden.
        public virtual bool PanelUnlocked { get; set; } = true;
        // 0 = free, 1 = X only, 2 = Y only, 3 = Z only.
        public virtual int PanelMoveMode { get; set; } = 0;
        // Keep the panel aimed at the player's floor/feet marker.
        public virtual bool AlignWithFloor { get; set; } = true;

        // ---- Song requests ----
        public virtual bool EnableSongRequests { get; set; } = true;
        // Comma-separated aliases; the word viewers type before a map key.
        public virtual string RequestCommand { get; set; } = "!request";
        // 0 = everyone, 1 = moderators + broadcaster, 2 = broadcaster only.
        public virtual int WhoCanRequest { get; set; } = 0;
        public virtual int PerUserCooldownSeconds { get; set; } = 30;
        public virtual bool AllowDuplicates { get; set; } = false;
        // In-queue cap per requester; 0 = unlimited.
        public virtual int MaxRequestsPerUser { get; set; } = 2;
        public virtual bool PersistRequestQueue { get; set; } = true;

        // ---- Map overlay ----
        // A "now playing" card drawn only on the desktop/flatscreen mirror
        // (never in the headset). Toggled by a button on the chat panel.
        public virtual bool OverlayEnabled { get; set; } = false;
        public virtual bool OverlayShowSongName { get; set; } = true;
        public virtual bool OverlayShowSongAuthor { get; set; } = true;
        public virtual bool OverlayShowCover { get; set; } = true;
        // "Mapper & Key": the level author plus the BeatSaver key (custom maps).
        public virtual bool OverlayShowMapper { get; set; } = true;
        public virtual bool OverlayShowDuration { get; set; } = true;
        public virtual bool OverlayShowDifficulty { get; set; } = true;
        // Screen anchor: 0 = top-left, 1 = top-center, 2 = top-right, 3 = right,
        // 4 = bottom-right, 5 = bottom-center, 6 = bottom-left, 7 = left.
        public virtual int OverlayPosition { get; set; } = 0;
    }
}
