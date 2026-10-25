# One media source at a time: local, phone mirror, Android Auto

**Status:** in progress (branch `android-auto-native`)
**Last updated:** 2026-10-25

**Goal:** exactly one source owns music at any moment, only a user action changes the owner, and navigation prompts / Assistant / calls interrupt the owner without taking it over.

## Why

OCTAVE now has three things that play music: its own player (`MediaManager`), the phone while mirroring (`PhoneMirrorManager`, media controls over scrcpy) and native Android Auto (`src/android_auto/`, media on its own audio channel). `NowPlaying` already arbitrates the first two ("phone takes the controls, local pauses"; "local starts, phone pauses"), but Android Auto is outside it: it is granted audio focus on every request, so with local music playing, Android Auto starting Spotify on connect plays both at once, and the bottom bar / knob / gestures / Now Playing card don't control it.

## Decisions (made 2026-10-25)

1. **Only a user action changes the owner.** A phone connecting, Android Auto starting, a reconnect or "Start music automatically" never does.
2. **Android Auto connecting while local music plays: local keeps playing.** The phone's media focus request is refused; it pauses itself.
3. **When the owner disappears (unplug, crash, session ends): pause.** No fallback to another source. The card shows the last source, paused.
4. **Resume after a restart is on by default.** The owner and whether it was playing are saved; on boot / reconnect only that source resumes, and only if it was playing.
5. **Phone mirroring and Android Auto are the same phone: never both at once.**
6. **Interruptions don't change the owner.** Android Auto guidance and system sounds duck local media; Assistant (the phone opening the mic) pauses it; both restore afterwards.

## What counts as a user action

- **Android Auto:** the phone asks before playing media (`AUDIO_FOCUS_REQUEST` GAIN on the control channel), so OCTAVE decides. A request within a few seconds of a touch on the Android Auto screen, or of an OCTAVE media control aimed at Android Auto, is the user: granted, Android Auto becomes the owner. Any other request (the auto-resume on connect, a background app) is refused with LOSS while another source owns; granted when Android Auto already owns or nothing does.
- **Phone mirror:** scrcpy doesn't ask, OCTAVE sees playback after it started. Keep the current hand-off (phone starts → it owns); add the same touch-recency check so the phone resuming by itself on connect doesn't take over from local (today the phone's *first* report takes the controls even when paused: `PhoneMirrorManager::onMediaState`, `firstReport`).
- **Local:** pressing play / picking a track in OCTAVE's player.

## Steps

### 1. Android Auto focus decided outside the session — Effort: M, desktop + AA phone
- `AapSession::Options::externalAudioFocus`: GAIN requests are emitted (`audioFocusRequested`) instead of auto-granted; `answerAudioFocus(state)` answers; `setAudioFocus(state)` sends an unsolicited notification (head unit takes focus back: the phone pauses). Transient requests (Assistant, prompts) and RELEASE keep being answered by the session.
- Verify on the phone: a LOSS answer to GAIN pauses playback and the phone asks again on the next tap of play; an unsolicited LOSS pauses; logcat for the request types navigation prompts and Assistant use.
- Media keys: announce KEYCODE_MEDIA_PLAY_PAUSE / NEXT / PREVIOUS in the input service and send key events (`InputReport` field 4), so OCTAVE's controls drive Android Auto.

### 2. Arbiter in NowPlaying — Effort: M, desktop
- `src/managers/androidautomedia.{h,cpp}`: a plain QObject (built everywhere) with Android Auto's media state and intents, fed by `AndroidAutoManager`; `NowPlaying` takes it like it takes `PhoneMirrorManager`, so `tst_nowplaying` drives it without a phone.
- `NowPlaying::source` gains `"androidauto"`; the rules above; controls route to the owner.
- Persist `mediaLastSource` / `mediaLastPlaying` (generic settings keys); resume rule on boot and on Android Auto / phone connect.
- Ducking: reuse `MediaManager::setDucking` (phone mirroring already drives it from `PhoneMirrorManager::duckingChanged`) while Android Auto guidance / system audio plays; pause local while the Android Auto mic is open.
- Tests: every row of the table in `wiki/phone-mirror-android-auto.html` (written in this step).

### 3. Android Auto now-playing — Effort: M, desktop + AA phone
- Announce the media playback status service; parse playback status (state, position) and metadata (title, artist, album, art, duration); feed `AndroidAutoMedia` so the Now Playing card, Media Room and Album Art Capture work for Android Auto.

### 4. Phone mirror on the same rules — Effort: S, desktop + phone
- No takeover on the first report while another source owns; touch-recency check; refuse to start mirroring while Android Auto runs (and the reverse).

### 5. On the device — Effort: S, Pi in the Jeep
- Ignition-cycle resume with each source; guidance ducking by ear.

Delete this file (and its README row) when step 5 is done.
