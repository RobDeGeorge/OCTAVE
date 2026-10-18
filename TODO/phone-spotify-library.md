# Phone Spotify library in the music select menu (*)

**Goal:** browse and play the phone's Spotify library (Liked Songs, playlists, albums) from OCTAVE's music menu, using only the phone mirror server.
**Status:** \* maybe — researched and prototyped, undecided whether to build the library reader. Step 1 below is worth doing on its own either way.
**Last updated:** 2026-10-18

## What it is

Show the phone's full Spotify library (Liked Songs, playlists, saved albums)
in `MediaPlayer.qml` next to the local playlists, with art on every row and
tap to play, using only the phone mirror server — no Spotify Web API or
developer account (hard constraint: the user won't deal with Spotify's
developer rules).

Already shipped: the Phone Queue (current track + next 49 with art, `phoneMirrorManager.mediaQueue`, `MediaPlayer.qml:219-225`) and the bulk lane for big device messages (`phone_server/.../control/DeviceMessageSender.java:17-55`).

## What the research established (2026-10-10, SM-S948U, Android 17, Spotify 9.1.90)

- **No non-UI way to read the library.** Spotify's MediaBrowserService only
  trusts a built-in package + signature allowlist (Android Auto, SystemUI,
  …); the media3 library service is disabled; the `mediaapi` content
  provider serves images only; shortcuts/AppSearch expose nothing. A test
  APK was refused with "No root for client".
- **UI scraping works.** From the shell-privileged server process:
  UiAutomation (`new UiAutomation(handlerThreadLooper, new UiAutomationConnection())`,
  `connect(0)`, `FLAG_RETRIEVE_INTERACTIVE_WINDOWS`, `clearCache()` before
  every read) reading Spotify on a private trusted virtual display, phone
  locked. Best display: **1080x6600 @ 420 dpi** (Spotify loads 36 rows per
  page regardless of display height; 16384 px is the hard max). Slow
  injected drag with 2-row overlap; tail rows need a resize-to-2340 +
  SCROLL_BACKWARD/FORWARD fix. Liked Songs 500/500 in ~20 s (24 s with
  art); 150-track playlist in 7 s; library lists in ~4 s. Art crops from
  the ImageReader frame via node bounds (126–168 px, ~5 KB JPEG, ~7% need a
  second-chance crop). Rows expose title/artists/explicit only — **no URIs**.
  Deep links `spotify:collection:tracks`, `spotify:collection`,
  `spotify:playlist:<id>` work with `am start --display N`. ACTION_CLICK on
  a track row plays it in the collection's context.
- **Playback from the session** (MediaController, verified): play/pause/
  seek/next/prev/skipToQueueItem; shuffle and repeat via
  `sendCustomAction("android.support.v4.media.session.action.SET_SHUFFLE_MODE" / "…SET_REPEAT_MODE")`
  with the compat int extras. `playFromUri/MediaId/Search` are advertised
  but no-op for untrusted callers — so "play track N of context C" is only
  reachable by tapping the row on the hidden display.
- Prototype sources and logs: were in the session scratchpad
  (`tallscan/`, `ids/`, `scrape/`); not kept in the repo.

## Why it's a maybe

- **Tap-to-play latency.** Playing a specific row means reopening the
  collection on the hidden display and scrolling 36 rows/s to it: song 400
  of Liked Songs takes 10+ s to start. Fine for playlists/albums from the
  top (one deep link), poor for deep rows.
- **Spotify runs one activity.** Every scan *and every tap-to-play* pulls
  Spotify off the mirror display; it has to be moved back
  (`am start --display`). Visible flicker if the user is on Spotify in the
  mirror at that moment.
- **Fragile.** Depends on Spotify's 36-row loading window, English labels
  ("Open context menu for", "Now Playing Bar", "Clear filter selection"),
  row pixel layout for crops, and the tail-fix trick. Each Spotify update
  can silently break it. Ownership spans phone server + C++ client + QML.
- Phone must be awake (MirrorKeeper covers this during a session).
- Read-only: no move/delete/create playlist.

## Step 1 — standalone, do it regardless of the library decision

**Effort:** M. **Device:** S22 or S26 with Spotify, mirror running.

- **Shared row delegate.** The Phone Queue has its own list (`MediaPlayer.qml:468`) separate from the local song list (`:398`). Fold it into the shared song-row delegate through a `lib`-style source abstraction in `MediaPlayer.qml`, so phone and local rows look and behave the same.
- **Shuffle / repeat on the phone.** Add two actions to `TYPE_OCTAVE_MEDIA` (`phone_server/.../control/ControlMessage.java:34-42`, next to `MEDIA_SKIP_TO_QUEUE_ITEM = 7`), implemented in `PhoneMedia.java` with `sendCustomAction("android.support.v4.media.session.action.SET_SHUFFLE_MODE" / "…SET_REPEAT_MODE")` and the compat int extras; report the modes as flags in `MEDIA_STATE` (device type 101); send and parse them in `src/phone_mirror/scrcpyclient.cpp` (`kMsgOctaveMedia`, `kDevMsgOctaveMediaState`, lines 73/79) and expose them on `phoneMirrorManager`. Wire the shuffle button in `frontend/BottomBar.qml` (around line 51, which today dims it while the phone is the source) to the phone's shuffle; there is no repeat button yet, so add one or leave repeat to the phone.
- Rebuild the jar with `phone_server/build_without_gradle.sh` and commit it with the source.

## Library reader — only if the maybe becomes a yes

2. **Library reader for browsing.** **Effort:** L. **Device:** S26 (Android 17) with Spotify; ideally a second Spotify version to check fragility. UiAutomation on the hidden trusted virtual display as researched above; tapping a playlist/album plays it from the top via deep link; the queue then gives instant jumps within 50.
3. **Disk cache.** **Effort:** M. **Device:** none beyond step 2. `<appdata>/phone_library/`, atomic writes, art deduped by hash, incremental re-scan by count, offline read-only browsing.
4. **Deep-row tap-to-play** as an accepted-latency extra. **Effort:** M. **Device:** S26.

Protocol/architecture sketch: a new C++ `PhoneLibraryManager`, control type 104 `TYPE_OCTAVE_LIBRARY` (still free; control types stop at 103 in `ControlMessage.java:29-32`), device types 105/106/108 (104 is now taken by `TYPE_OCTAVE_MEDIA_QUEUE_ART`, `DeviceMessage.java:14`), sent through the bulk lane. The 2026-10-10 session notes are gone; re-derive from this file.

**Delete this file when** step 1 is done and the library reader is either built (steps 2–3) or decided against.
