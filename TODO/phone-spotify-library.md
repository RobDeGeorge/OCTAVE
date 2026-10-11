# Phone Spotify library in the music select menu (*)

**Status:** \* maybe — researched and prototyped, undecided whether to build at all
**Last updated:** 2026-10-10

## What it is

Show the phone's full Spotify library (Liked Songs, playlists, saved albums)
in `MediaPlayer.qml` next to the local playlists, with art on every row and
tap to play, using only the phone mirror server — no Spotify Web API or
developer account (hard constraint: the user won't deal with Spotify's
developer rules).

The Phone Queue (current track + next 49 with art, `phoneMirrorManager.mediaQueue`)
is already shipped and is the 50-track slice of this.

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
  can silently break it. Ownership spans phone server + both backends + QML.
- Phone must be awake (MirrorKeeper covers this during a session).
- Read-only: no move/delete/create playlist.

## If it is built — reduced-scope version recommended

1. Fold the Phone Queue into the shared song-row delegate (`lib` source
   abstraction in `MediaPlayer.qml`), add shuffle/repeat to the phone
   (`PhoneMedia.java` custom actions, `MEDIA_STATE` flags, both backends).
2. Library reader for **browsing**; tapping a playlist/album plays it from
   the top via deep link; the queue then gives instant jumps within 50.
3. Disk cache (`<appdata>/phone_library/`, atomic writes, art deduped by
   hash), incremental re-scan by count, offline read-only browsing.
4. Deep-row tap-to-play as an accepted-latency extra.

Protocol/architecture sketch (new `PhoneLibraryManager` in both backends,
control type 104 `TYPE_OCTAVE_LIBRARY`, device types 105/106/108, bulk lane
in `DeviceMessageSender` — already added) is in the 2026-10-10 session
notes; re-derive from this file if those are gone.
