using System.Reflection;
using BeatSaberMarkupLanguage;
using UnityEngine;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.UI
{
    /// <summary>
    /// Platform, badge, and control sprites loaded from individual embedded
    /// 128px PNGs. The move/request glyphs are white assets meant to be tinted
    /// by the UI; the Twitch/YouTube/Kick/broadcaster/lock/unlock assets are the
    /// original art split out from the old strip.
    /// </summary>
    internal sealed class ChatIcons
    {
        public Sprite Twitch { get; private set; } = null!;
        public Sprite YouTube { get; private set; } = null!;
        public Sprite Kick { get; private set; } = null!;
        public Sprite Broadcaster { get; private set; } = null!;
        public Sprite Moderator { get; private set; } = null!;
        public Sprite Lock { get; private set; } = null!;
        public Sprite Unlock { get; private set; } = null!;
        public Sprite MoveX { get; private set; } = null!;
        public Sprite MoveY { get; private set; } = null!;
        public Sprite MoveZ { get; private set; } = null!;
        public Sprite MoveFree { get; private set; } = null!;

        // Nullable: a missing request icon falls back to text labels rather
        // than breaking the whole panel.
        public Sprite? Accept { get; private set; }
        public Sprite? Deny { get; private set; }
        public Sprite? ModeRequests { get; private set; }
        public Sprite? ModeRequestsNotification { get; private set; }
        public Sprite? ModeChat { get; private set; }
        public Sprite? OverlayOn { get; private set; }
        public Sprite? OverlayOff { get; private set; }

        public static ChatIcons? Load(IPALogger? log)
        {
            try
            {
                var icons = new ChatIcons
                {
                    Twitch = LoadRequired("icon-twitch.png", log),
                    YouTube = LoadRequired("icon-youtube.png", log),
                    Kick = LoadRequired("kick-icon.png", log),
                    Broadcaster = LoadRequired("icon-broadcaster.png", log),
                    Moderator = LoadRequired("icon-moderator.png", log),
                    Lock = LoadRequired("icon-lock.png", log),
                    Unlock = LoadRequired("icon-unlock.png", log),
                    MoveX = LoadRequired("icon-move-x.png", log),
                    MoveY = LoadRequired("icon-move-y.png", log),
                    MoveZ = LoadRequired("icon-move-z.png", log),
                    MoveFree = LoadRequired("icon-move-free.png", log),
                };
                icons.Accept = LoadSingle("SimpleBSStreamChat.Resources.accept.png", log);
                icons.Deny = LoadSingle("SimpleBSStreamChat.Resources.deny.png", log);
                icons.ModeRequests = LoadSingle("SimpleBSStreamChat.Resources.icon-requests.png", log);
                icons.ModeRequestsNotification = LoadSingle(
                    "SimpleBSStreamChat.Resources.icon-requests-notification.png", log);
                icons.ModeChat = LoadSingle("SimpleBSStreamChat.Resources.icon-chat.png", log);
                icons.OverlayOn = LoadSingle("SimpleBSStreamChat.Resources.icon-overlay-on.png", log);
                icons.OverlayOff = LoadSingle("SimpleBSStreamChat.Resources.icon-overlay-off.png", log);
                return icons;
            }
            catch (System.Exception ex)
            {
                log?.Warn($"Icons unavailable, using text tags: {ex.Message}");
                return null;
            }
        }

        private static Sprite LoadRequired(string fileName, IPALogger? log)
        {
            var resourceName = "SimpleBSStreamChat.Resources." + fileName;
            var sprite = LoadSingle(resourceName, log);
            if (sprite == null)
                throw new System.InvalidOperationException($"Required icon '{resourceName}' failed to load.");
            return sprite;
        }

        private static Sprite? LoadSingle(string resourceName, IPALogger? log)
        {
            try
            {
                var data = Utilities.GetResource(Assembly.GetExecutingAssembly(), resourceName);
                if (data == null || data.Length == 0)
                    return null;
                var texture = new Texture2D(2, 2, TextureFormat.RGBA32, false);
                if (!ImageConversion.LoadImage(texture, data))
                    return null;
                texture.wrapMode = TextureWrapMode.Clamp;
                texture.filterMode = FilterMode.Bilinear;
                return Sprite.Create(texture, new Rect(0, 0, texture.width, texture.height),
                    new Vector2(0.5f, 0.5f), 100f, 0, SpriteMeshType.FullRect);
            }
            catch (System.Exception ex)
            {
                log?.Warn($"Icon '{resourceName}' failed to load: {ex.Message}");
                return null;
            }
        }
    }
}
