# SimpleBSStreamChat

A free, simple Beat Saber PC mod that shows your **Twitch**, **YouTube**, and **Kick** live stream chat on a floating panel in-game. No accounts, no third-party relay services, no paywall.

- **Twitch:** connects anonymously to Twitch chat (read-only) — no login needed.
- **YouTube:** reads the public live chat of your stream the same way a browser does — no API key, no login. Automatically finds your current livestream from your handle and reconnects between streams.
- **Kick:** connects to Kick's public live chat stream by channel slug or chatroom id - no login needed. If Kick blocks slug lookup, enter the numeric chatroom id directly.

## Requirements
- Beat Saber PC 1.40.8 with [BSIPA](https://github.com/bsmg/BeatSaber-IPA-Reloaded) 4.3.6+
- [BeatSaberMarkupLanguage](https://github.com/monkeymanboy/BeatSaberMarkupLanguage) 1.12.5+
- [SongCore](https://github.com/Kylemc1413/SongCore) for custom-song refresh/navigation
- BeatSaverSharp, included by common mod managers and referenced from Beat Saber's `Libs` folder

## Setup
1. Drop `SimpleBSStreamChat.dll` into your `Beat Saber\Plugins` folder.
2. Run the game once, then add your platform connections inside Mods -> SimpleBSStreamChat:
   - `"TwitchChannel"`: your Twitch channel name (e.g. `"myname"`)
   - `"YouTubeChannel"`: your YouTube handle (e.g. `"@myname"`), channel id (`"UC…"`), or an 11-character video id
   - `"KickChannel"`: your Kick channel slug, profile URL, or numeric chatroom id
   **You can also edit `UserData\SimpleBSStreamChat.json` to add your connections as well.**
4. Restart the game. The chat panel appears above the menu; you can grab anywhere on the window to move it. Most options can also be changed in Mods -> SimpleBSStreamChat.

## Song Requests
- Viewers can request BeatSaver maps using `!request <key>` by default. `!bsr <key>` is also supported. The command, cooldowns, duplicate behavior, permissions, and queue persistence are all configurable in the Requests settings tab; the queue has a fixed safety cap of 100 requests. 
- Accepted requests download, install, refresh SongCore, and navigate the streamer to the map from the All Songs view automatically.
- Queued requests are restored from `UserData\SimpleBSStreamChat.Requests.json` after restart when Persist Queue is enabled.
- Twitch is connected read-only, so request feedback appears as system messages on the in-game panel rather than being sent back to Twitch chat.
- If BeatSaberPlus ChatRequest is also enabled, disable that module or change one of the request commands so both mods are not listening to the same chat command if both are installed.

## Song Overlay
- A simple song overlay can be toggled on via the chat window. It shows all the details of the song you are currently playing to your viewers via flatscreen projection. It is not viewable in VR.
- Shows the **Cover**, **Title**, **Author**, **Mapper & Map Key**, **Clock**, and **Difficulty** of the map. All of these are toggleable in settings.
- The overlay's **Position** can also be set in settings to a various amount of positions around your screen. 

## Credits
- Architecture inspired by [BeatSaberPlus](https://github.com/hardcpp/BeatSaberPlus) (MIT), [BSML](https://github.com/monkeymanboy/BeatSaberMarkupLanguage) (MIT), and [BSIPA](https://github.com/bsmg/BeatSaber-IPA-Reloaded) (MIT).
- Twitch IRC protocol details cross-checked against [CatCore](https://github.com/ErisApps/CatCore) (GPL-3.0 — used as documentation only; no code copied).

## License
MIT
