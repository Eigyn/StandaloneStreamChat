# SimpleBSStreamChat (Quest)

C++ port of SimpleBSStreamChat for Beat Saber on Quest, packaged as a `.qmod`.
Targets **Beat Saber 1.45.3_30054** (Quest 2 / 3S / 3).

Ported so far: Twitch, YouTube and Kick chat (text, badges and inline emotes) on
a floating panel in the menu, and the PC settings screen's Connections,
Appearance (curve, size, backdrop color and opacity, font) and General tabs
(`include/UI/SettingsBsml.hpp`, the PC `UI/Settings.bsml` minus unported parts).
The PC panel's lock and move-mode buttons sit in a column outside the panel's
left edge; their icons (`assets/`) are compiled in as byte arrays that
`cmake/EmbedAssets.cmake` generates (not `#embed`, which the 1.40.8 build's
NDK 27 compiler lacks).
Viewer counts sit in a strip across the top: each chat client polls its own
platform every 30 s (Twitch web GQL, YouTube InnerTube `updated_metadata`,
Kick's channel API).
Each message is its own row (`UI/ChatRow`), with the Highlight Messages
backdrop. Platform and badge icons and emotes show inline as TMP sprites from a
runtime 1024x1024 atlas (`UI/EmoteAtlas`): images download on a background
thread, PNGs decode through Unity, and GIFs through `UI/GifDecoder`. Atlas
entries referenced by active rows are protected from eviction; when the
working set cannot fit, the affected emotes remain readable as text.
Twitch, YouTube and Kick emotes come from the messages;
BTTV/FFZ/7TV emotes (`Chat/ThirdPartyEmotes`, global plus the Twitch channel's)
are matched by word, as on the PC. Channel lookup no longer depends on FFZ:
the Twitch session or public web resource response supplies the channel ID.
Provider failures retry with backoff, preserving successful data, and the
resource cache periodically refreshes. Global and channel Twitch badge
images, YouTube member images and Kick badge images share the same atlas.

Message IDs, user IDs and channel identity are retained so Twitch deletions,
user purges and chat clears update the feed. Single-message deletions retain
a gray deleted-message marker; user purges and channel clears remove the
matching rows. YouTube removal actions and
Kick deletion/ban/clear events use the same ordered history update path.
Mentions and provider-highlighted messages have distinct background colors;
cheer amounts and full badge metadata are retained.

**Animated Emotes** in General is optional and **off by default** on Quest.
It uses GIF resources where available and retains at most four animations
and 2 MiB of frame pixels in total, with up to 512 KiB and 128 sampled frames
per animation. Frame uploads are limited to ten animation updates per second.
Fast/long clips are sampled to fit; sources beyond the dimension/work budget
fall back to a static image. Turning the toggle off releases retained frame
data immediately. The existing atlas stays 1024x1024; animation does not add
another texture atlas. Temporary decode/transfer buffers are bounded
separately, so the 2 MiB figure describes retained animation data.
One downloader handles at most 64 waiting jobs and one completed result,
preventing decoded images from piling up while Requests or a hidden panel
pauses consumption.

Song requests: viewers type `!bsr <key>` (or paste a BeatSaver link after
`!bsr`); the PC's `!request` is not supported. `Requests/SongRequestManager`
keeps the queue (looked up on BeatSaver by a worker thread, saved to
`Configs/SimpleBSStreamChat.Requests.json`), and the third side button swaps
the feed for the request list (`UI/RequestList`, `UI/RequestRow`). Accept is
available on the main menu and Solo song list, with one active request at a
time. An installed map goes directly to selection, without downloading or
refreshing SongCore. Files not yet loaded get one discovery refresh first.
New downloads unpack on their own thread (`Songs/MapInstaller`,
`Songs/ZipExtractor`) and install into a new folder identified by the map hash;
an older loaded version is never overwritten. Selection waits for Solo and
the song list to settle, switches to All Songs, filters to the exact level,
and confirms the selected ID before removing the request. Its temporary ID
filter is released afterward so later searches use the whole library.
Selection/loading errors retain the installed map with a Select retry, while
download failures offer a download retry. Leaving Solo during installation
parks the installed request for another Select press instead of keeping it
busy indefinitely. The same workflow is used by both Quest builds.

The Appearance tab adds a Quest-only Corners slider: the panel draws its own
sliced rounded backdrop (`Icons::RoundedPanel`), starting at the rounding
BSML's backdrop had.

The PC map overlay is intentionally not part of the Quest version.

## Requirements
- PowerShell 7 (`pwsh`), CMake, Ninja
- [qpm](https://github.com/QuestPackageManager/QPM.CLI) (`qpm download ninja` installs Ninja)
- Android NDK **29** (`qpm ndk download 29.0.14206865`). NDK 27 can't build
  beatsaber-hook 9.x, which needs C++26 `std::span` from a braced list.

## Building
```
qpm restore
qpm s qmod
```
`qpm s build` builds only the `.so`. Put your NDK path in `ndkpath.txt`
(gitignored) if `ANDROID_NDK_HOME` isn't set.

### Beat Saber 1.40.8
`bs-1.40.8/` builds the same sources against the 1.40.8 libraries
(bs-cordl 4008, beatsaber-hook 6.4.2, and the exact 1.40.8 core mods: BSML
0.4.55, SongCore 1.1.24, custom-types 0.18.3, paper2 4.6.4) into
`bs-1.40.8/SimpleBSStreamChat-1.40.8.qmod`:
```
cd bs-1.40.8
qpm restore
qpm s qmod
```
It needs NDK **27** (`qpm ndk download 27.2.12479018`, path in
`bs-1.40.8/ndkpath.txt`): beatsaber-hook 6's headers don't compile with NDK 29's
libc++. beatsaber-hook 6 has no flamingo, so its inline hooker is compiled
in. Code that differs between the versions lives in `include/Compat.hpp` or
checks `SBSC_BS_1_40`.

## Notes
- `tests/RunNativeTests.ps1 -OutputDirectory <folder> -RunOnQuest -AdbPath
  <adb.exe> -Serial <device>` compiles production chat clients, resource loading,
  GIF decoding and the bounded download queue into an isolated Android test.
  Its network transport is simulated; it sends no external chat messages.
  It covers moderation scope, badge/mention metadata, provider recovery,
  GIF disposal/interlacing and memory limits, row reference lifetime, and a
  paused-consumer burst of 1,000 image requests.
- `libcurl` is pinned to `=8.5.0`: the 8.7.1 archive is missing OpenSSL's
  `EVP_Encode*`/`EVP_Decode*` objects and fails to link.
- `flamingo` is pinned to `~1.2.1`, the version the 1.45.3 core mods ship. With
  `^1.3.0`, MBF upgrades the shared `libflamingo.so` to 1.3.0, and custom-types
  then aborts at launch ("Failed to mark ptr ... Permission denied"). The crash
  stays after removing this mod, because MBF leaves the upgraded library behind.
- The bundled libcurl has neither WebSocket nor gzip support, so
  `Net/WebSocket` is a small RFC 6455 client on top of the raw TLS stream, and
  YouTube pages download uncompressed (the `/live` lookup stops reading early).
- Kick chat now runs on Centrifugo, not Pusher. The client gets the server from
  `web.kick.com/api/v1/realtime/channels/{id}/chat/connection` and an anonymous
  guest token from `.../realtime/auth/connection`, then subscribes to
  `chatrooms.{chatroomId}.v2`.
- SongCore (`^1.3.5`, a core mod) is a dependency for refreshing songs and
  finding them by hash. Map zips are unpacked with the system `libz.so`
  (an NDK stable library), so no zip library is bundled.
- TLS certificates are verified against the headset's own CA store
  (`/apex/com.android.conscrypt/cacerts`, then `/system/etc/security/cacerts`).
- Config: `/sdcard/ModData/com.beatgames.beatsaber/Configs/SimpleBSStreamChat.json`,
  same key names as the PC `UserData\SimpleBSStreamChat.json`.
