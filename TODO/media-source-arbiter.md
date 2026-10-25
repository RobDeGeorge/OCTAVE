# One media source at a time: local, phone mirror, Android Auto

**Status:** mostly done (branch `android-auto-native`); left: checks by ear, phone mirror on the same rules
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

## Done (2026-10-25, branch `android-auto-native`)

Steps 1-3 of the first plan: OCTAVE decides Android Auto's media focus (`AapSession::Options::externalAudioFocus`), media keys, the media playback status channel, `AndroidAutoMedia`, the rules in `NowPlaying` (`source` = `"androidauto"`, `useLocal()`), ducking under guidance / system sounds, pausing for the mic, `mediaLastSource` / `mediaLastPlaying` and the resume. Checked against the phone (Android Auto 17.9): refusal on connect and on a background start, grant after a tap, OCTAVE's play / pause, local taking the music back, resume after a restart, the Media Room / home card showing Android Auto's track. Rules table: `wiki/phone-mirror-android-auto.html#aa-native-music`. Found on the way: answering LOSS to a GAIN request makes Android Auto loop; refusing is grant + unsolicited LOSS (`docs/ANDROID_AUTO_PROTOCOL.md`, *Audio focus*).

## Left

### 1. By ear and voice — Effort: S, desktop + AA phone
- A navigation prompt while local music plays: local ducks to 30 % and comes back.
- "Hey Google, play ..." while local plays: local pauses, Android Auto is granted (the mic counts as the user), local stays paused.
- Assistant asked something else: local resumes after the mic closes.
- Next / previous from the bottom bar with Spotify (only play / pause was tried, on a podcast).

### 2. Phone mirror on the same rules — Effort: S, desktop + phone
- No takeover on the first media report while another source owns (`PhoneMirrorManager::onMediaState`, `firstReport`); a touch on the mirror screen in the last few seconds counts as the user's.
- Refuse to start mirroring while Android Auto runs, and the reverse (same phone).
- Resume a mirrored phone after a restart like Android Auto (local auto-play is only skipped for `androidauto` today, `MediaManager` restore).

### 3. Android Auto audio in the visualizer — Effort: S, desktop
- `AudioAnalyzer` live mode gets no Android Auto PCM yet (bars stay still): feed it the media stream from `AapConnection::onAudio`.

### 4. On the device — Effort: S, Pi in the Jeep
- Ignition-cycle resume with each source; guidance ducking in the car.

Delete this file (and its README row) when step 4 is done.
