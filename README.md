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
2. Run the game once, then edit `UserData\SimpleBSStreamChat.json`:
   - `"TwitchChannel"`: your Twitch channel name (e.g. `"myname"`)
   - `"YouTubeChannel"`: your YouTube handle (e.g. `"@myname"`), channel id (`"UC…"`), or an 11-character video id
   - `"KickChannel"`: your Kick channel slug, profile URL, or numeric chatroom id
3. Restart the game. The chat panel appears above the menu; grab its handle to move it. Most options can also be changed in Mods -> SimpleBSStreamChat.

## Song Requests
- Viewers can request BeatSaver maps with `!request <key>` by default. The command, cooldowns, duplicate behavior, permissions, and queue persistence are configurable in the Requests settings tab; the queue has a fixed safety cap of 100 requests.
- Accepted requests download, install, refresh SongCore, and navigate the streamer to the map from the All Songs view.
- Queued requests are restored from `UserData\SimpleBSStreamChat.Requests.json` after restart when Persist Queue is enabled.
- Twitch is connected read-only, so request feedback appears as system messages on the in-game panel rather than being sent back to Twitch chat.
- If BeatSaberPlus ChatRequest is also enabled, disable that module or change one of the request commands so both mods are not listening to the same chat command.

## Building
- Install the .NET SDK (any recent version).
- `dotnet build` — references and post-build deployment resolve via the `BeatSaberDir` MSBuild property (defaults to `..\BeatSaber 1.40.8 (MODDED)`; override in `Directory.Build.props.user`).

## Credits
- Architecture informed by [BeatSaberPlus](https://github.com/hardcpp/BeatSaberPlus) (MIT), [BSML](https://github.com/monkeymanboy/BeatSaberMarkupLanguage) (MIT), and [BSIPA](https://github.com/bsmg/BeatSaber-IPA-Reloaded) (MIT).
- Twitch IRC protocol details cross-checked against [CatCore](https://github.com/ErisApps/CatCore) (GPL-3.0 — used as documentation only; no code copied).

## License
MIT
