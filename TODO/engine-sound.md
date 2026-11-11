# Engine sound (pick a real engine, hear it through the car's speakers)

**Goal:** the user browses a catalog of real engines, picks one, and OCTAVE plays that engine through the car's speakers on top of whatever else is playing, following the car's RPM and throttle, or speed and acceleration in an EV. It also works in the browser demo.
**Status:** parked: blocked on source recordings. Four ways of making the sound were prototyped on 2026-11-09 and 2026-11-10 and none passed the owner's ear test. Nothing is built in the app.
**Last updated:** 2026-11-10

## Why it's parked

The playback method is not the blocker; the recordings are. Free recordings of named, iconic cars exist only as YouTube uploads (phone or camera audio, lossy, level-compressed, shot in a noisy dyno room or a car park). The owner's verdict on the untouched clips was "alright, but compressed and not dynamic", and every processed version sounded worse than its source. Pick this up again only when clean recordings are in hand (see "What would unblock it").

## What was tried (prototypes in the gitignored `dev/engine-sound/`)

All were judged by ear on a private listening page; the session that built them could not hear them.

| Approach | Tool | Verdict |
|---|---|---|
| Physics simulation: Engine Simulator (ange-yaghi/engine-sim, MIT) with our own Jeep 4.0 definition from published specs | scratch build, not kept | "Sounds like fart noises"; brighter variants "pretty far off" |
| Measure a real clip and rebuild it (harmonic orders + shaped noise) | `esmatch.py` | "Still pretty bad" |
| Slicing: cut the recording into engine cycles and replay them along new revs (the Forza-style granular method) | `esgrain.py`, `esgrain2.py`, `esgrain3.py` | Better than the first two, but "choppy", even with waveform-aligned splices |
| Loops: steady loops cut from a dyno pull, pitched and crossfaded (the classic game method) | `esloop.py` | "This all sounds so bad" |

What the prototypes established, so nobody repeats it:

- **Parked-car revving is the wrong material.** It never loads the engine and rarely passes 3000 rpm. A **dyno pull** (full throttle from low revs to redline, then coast-down) is what game studios record, and it is the only free material that covers the rev range.
- **Engine speed can be read from audio alone** (`estrack.py`: harmonic summation + Viterbi), but it locks an octave off on some V8 / V10 / V12 clips. Give it the plausible rpm range per clip and check the result on a spectrogram.
- **Mark engine cycles by matching each cycle's waveform against the next** (`build_index2` in `esgrain2.py`), not from estimated phase. Phase-based marks left splices misaligned (waveform match about 0 at every splice), which was the choppiness.
- **An engine waveform does not repeat well.** Cycles 8 or more apart match only about 0.5 even in a clean clip, so any splice or blend between distant cycles is audible. Fewer, shorter, aligned splices help; they do not make it sound real.
- **Coast-down on a dyno lasts 2 to 3 seconds**, so power-off material is always thin.
- **Render checks:** look at a spectrogram of every render before anyone listens. One batch of loop renders was silent (NaN) and was published unnoticed.

## What would unblock it

In order of likely payoff:

1. **Clean recordings we may ship:** uncompressed, close-miked at the exhaust or in the engine bay, one car at a time, a slow pull under load and a slow coast-down, ideally steady holds every 500 to 1000 rpm. Own recordings with OCTAVE logging RPM alongside (step 8) are the surest source.
2. **Freesound WAV uploads** (96 kHz, hobbyist sound recordists). Quality is right, but downloads need an account, the cars are rarely iconic, and much of it is CC BY-NC, which cannot ship. Example of the right kind of recording with the wrong license: el-bee, "Volvo S60 Engine Ramp Test" (pack 35809).
3. **A paid per-car library** (Pole Position Production and similar, roughly 100 to 450 USD per car). Cannot go in the open repo or the browser demo; ruled out by the owner on cost.

## Decisions made (2026-11-09)

1. **Real engines, by name.** The catalog is recordings (or faithful simulations) of named engines, not generic synth presets.
2. **Plays over everything:** local music, phone mirror, Android Auto, AirPlay, Spotify. It is not a music source and stays outside the `NowPlaying` owner rules (`media-source-arbiter.md`).
3. **Must work in the browser demo**, driven by the simulated car.
4. **Audience is anyone**, petrol or EV. Neither mode is second-class.

## Legal ground rules

- An engine's sound is not owned; a *recording* is. Game audio (Forza, Assetto Corsa, Rigs of Rods mods) and paid sound libraries cannot ship: the repo is open and the browser demo serves every file publicly, which is redistribution.
- Accepted licenses for catalog packs: **CC0** or **CC-BY** (credit shown in the UI). Nothing "NC", nothing with a no-redistribution clause.
- Names describe what was recorded ("Chevrolet LS3 6.2 L V8, 2010 Camaro SS"). No logos, no badges, a "not affiliated with any manufacturer" line on the browser page. Every pack carries recorder credit and license in its manifest.
- Packs a user loads from disk are theirs to answer for, like the music library.
- Cabin sound only. Never describe it as the legally required pedestrian warning sound (AVAS) of an EV.

## Where the sounds come from

Researched 2026-11-09 and 2026-11-10. No free, shippable pack of real named cars in game-ready form was found; Stunt Rally 3 hit the same wall (`docs/data.md` in its repo).

| Source | License | State |
|---|---|---|
| YouTube, filtered to "Creative Commons" (search URL parameter `sp=EgIwAQ%253D%253D`; yt-dlp reports `%(license)s`) | CC BY | The only free source of named cars. Dozens of dyno pulls and exhaust clips; audio is lossy and level-compressed. Credit is required, the uploader must own the recording (skip compilations and game footage), many cars have aftermarket exhausts, and downloading is against YouTube's terms. Ten dyno clips and four parked-car clips with credits are in `dev/engine-sound/refs/` |
| muted.io "Performance Cars" (68 field recordings, 96 kHz WAV) | CC0 | Unnamed cars; described as field recordings of cars and asphalt, so likely drive-bys. Download is through a Ko-fi checkout; not fetched |
| OpenGameArt "racing car engine sound loops" | CC0 (unverified) | Generic loops, no named cars; not listened to |
| Freesound | per file | Good uncompressed ramps exist; see "What would unblock it" |
| Open-source racing games (Speed Dreams, VDrift, Stunt Rally) | GPL / Free Art License, compatible with OCTAVE's GPLv3 | Mostly one generic loop per car; Stunt Rally is removing its engine sounds over licensing doubts |
| Engine Simulator rendering our own engine definitions | ours | Tried; failed the ear test. Its bundled `.mr` engine files have no license |
| Own recordings: static revs + dyno day, RPM logged by OCTAVE | ours | Best quality; needs the recorder (step 8) |

## Design

### Pack format (v1)

A folder or zip: `pack.json` + mono Ogg Vorbis files.

```json
{
  "format": 1,
  "id": "chevrolet-ls3",
  "name": "Chevrolet LS3 6.2 L V8",
  "recordedFrom": "2010 Camaro SS, stock exhaust",
  "layout": "V8 cross-plane", "cylinders": 8, "displacementL": 6.2,
  "idleRpm": 650, "redlineRpm": 6600,
  "credit": "…", "license": "CC0-1.0", "origin": "recording | simulation",
  "loops": [
    {"file": "on_2000.ogg", "rpm": 2000, "load": "on"},
    {"file": "off_2000.ogg", "rpm": 2000, "load": "off"}
  ],
  "oneShots": {"start": "start.ogg", "stop": "stop.ogg", "limiter": "limiter.ogg"}
}
```

- `origin` is shown in the browser, so a simulated engine is never passed off as a recording.
- Loops are cut to a whole number of engine cycles (720° of crank) starting at the same crank phase, so two loops crossfade without flanging.
- Decoder: **stb_vorbis** (public domain, one file) compiled in under `third_party/`, like SQLite. No `QAudioDecoder`: it is missing on wasm and the arm64 FFmpeg question (`arm64-qt-ffmpeg-lgpl.md`) stays out of it. The same decoder runs on every platform.
- Size target: 1–3 MB per pack. Decoded in memory: about 5 MB.

### Player (`src/enginesound/`)

- `enginevoice`: per audio block, the two loops either side of the current RPM, each resampled by `rpmNow / loopRpm`, equal-power crossfade by RPM; the same between the `on` and `off` sets by load. All voices read from one shared crank-phase counter.
- `enginemixer`: one-shots (start, stop, limiter), level, soft limiter on the output, fade in / out.
- Renders on its own thread into a lock-free ring buffer; a pull-mode `QAudioSink` on the GUI thread drains it (`QAudioSink` is not thread-safe: same rule as `scrcpyclient.h:232`). 48 kHz, device buffer about 30–40 ms. The existing sinks use about 200 ms (`scrcpyclient.cpp:1099`), which is too slow here.
- A separate sink is mixed over the other streams by PipeWire / PulseAudio / CoreAudio / WASAPI / Android's mixer, so `MediaManager`, the phone mirror, Android Auto and AirPlay are untouched.

### Inputs (`enginedrive`)

Produces `rpm` and `load` (0–1) at 200 Hz for the player, from one of three modes:

| Mode | When | RPM from | Load from |
|---|---|---|---|
| **Follow** | petrol car, pack close to the real engine | `RPM` | `ACCELERATOR_POS_D`, else `THROTTLE_POS`, else `ENGINE_LOAD` |
| **Match revs** | petrol car, different engine (a Jeep 4.0 playing a 9000 rpm V10) | real RPM mapped idle→idle, redline→redline | same |
| **Virtual** | EV, or by choice | `SPEED` through a simulated gearbox: ratios, shift points by load, shift time with a torque cut, launch slip so it never sits at 0 rpm | pedal if reported, else `LONGITUDINAL_G` (IMU), else the slope of speed |

`auto` picks Follow / Match revs when the vehicle reports RPM, Virtual otherwise.

**Lag is the main risk.** OBD delivers RPM a few times a second and late; the Jeep TJ's J1850 VPW is the worst case. Mitigations:
1. `obdManager.setParameterDemand("engineSound", {"RPM", <pedal id>, "SPEED"})` while enabled, dropped when off. It competes with dashboards for polling: measure the cost.
2. A predictor: between samples, RPM is advanced with the last slope and with `LONGITUDINAL_G` (in a gear, RPM rate is proportional to acceleration), then pulled onto each new sample over about 80 ms. Never a step.
3. The DIY adapter's native fast mode (`diy-obd-adapter.md`) removes most of the lag later; nothing here depends on it.

### Manager (`src/managers/enginesoundmanager`, context property `engineSound`)

- Properties: `enabled`, `packId`, `level` (0–100, relative to master), `mode` (`auto` / `follow` / `matchRevs` / `virtual`), `idleWhenStopped`, `duckForCalls`, `rpm`, `load`, `gear` (virtual), `installedPacks`, `catalog`, `downloadProgress`.
- Invokables: `selectPack(id)`, `downloadPack(id)`, `removePack(id)`, `refreshCatalog()`, `preview(id)` (a rev sweep with the car off), `importPack(path)`.
- Volume: connects to `VolumeController::volumeApplied` and mute like the other outputs (`src/appcore.cpp:292`); output gain = master linear × level. No curve math of its own.
- Settings in `settingsmanager`: `engineSoundEnabled`, `engineSoundPack`, `engineSoundLevel`, `engineSoundMode`, `engineSoundIdleWhenStopped`, `engineSoundDuckForCalls`.
- Service scope `media`; the whole object `octave.experimental` at first. Regenerate `docs/api`, the wiki API reference and both SDKs in the same commit.

### Rules

- Engine off or not connected (Follow / Match revs): silent. Virtual mode at standstill: idle only if `idleWhenStopped`.
- Enable and disable fade over about 300 ms; start and stop one-shots play on ignition on / off when the pack has them.
- Hard cap on level so it cannot bury everything else; follows master mute.
- `duckForCalls` (default on): ducks while the phone has the mic open (the signal Android Auto already gives `AndroidAutoMedia`). **Open:** also duck under navigation prompts?

### Catalog and UI

- `catalog.json` + packs served from GitHub Pages next to the demo (`robdegeorge.com/OCTAVE/engines/`), from a separate repo so the main clone stays small. **Open:** confirm that hosting. Packs arrive by pull request.
- Fetched with `QNetworkAccessManager` (same-origin on the web build), cached in `<config>/engine-sounds/<id>/`. Desktop and mobile builds carry one small pack as a Qt resource so it works offline; the wasm build bundles none (every MB is downloaded by every visitor).
- `frontend/EngineSound.qml`: on / off, level, mode; the list (name, layout, displacement, recording or simulation, credit, license) with preview, download and remove; a live rev bar. Entry: a card in Settings > Media and **Open:** a tile in `CarMenu.qml`?

### Browser demo

- Try Qt's own wasm `QAudioSink` first and measure the delay. If it is over about 80 ms, add a Web Audio AudioWorklet output fed from the same ring buffer over shared memory (cross-origin isolation is already on for threads).
- Sound starts only after a tap (browser autoplay rule): the adapter sheet's "Simulated car" tap, or the page's own switch.
- Guard the sink choice with `OCTAVE_WASM`; no `QProcess`, no FFmpeg.

## Steps

| # | Step | Effort | Needs |
|---|---|---|---|
| 1 | **Get one clean recording and pass the ear test.** One car, recorded as "What would unblock it" describes, turned into a pack with `dev/engine-sound/esloop.py` (loops) or `esgrain3.py` (slices) and played to the owner along a driving pattern. Nothing below starts until one engine passes | S–M | desktop, a car + a mic |
| 2 | **Pack format + offline renderer.** `pack.json` parser and validator, stb_vorbis, `enginevoice` / `enginemixer`, and a dev tool that renders a WAV from a black-box trip CSV (`history/trips/`) or `tools/trips/make_mock_trip.py` output. Hand-build 1–2 packs from step 1. QtTest: manifest validation, crossfade weights, no clipping / NaN over a sweep | L | desktop |
| 3 | **Live playback.** `EngineSoundManager`, the sink + render thread, Follow and Match revs, the predictor, `VolumeController` wiring, settings, demand registration. Works under `--sim` (`src/sim/carsim` already has rpm, throttle, gear). Test the predictor against a recorded trip | L | desktop |
| 4 | **Virtual drivetrain.** Gearbox model, shift behaviour, load from pedal / IMU / speed slope, `auto` mode. Tests for shift points and launch | M | desktop sim |
| 5 | **Browser demo.** wasm sink (or AudioWorklet), pack fetch, tap-to-start, size check of the page | M | browser |
| 6 | **Catalog + browser page.** `catalog.json`, download / cache / remove, `EngineSound.qml`, preview sweep, credits. Wiki: a manager page, `settings-reference.html`, `pages.html`, `signals-slots-reference.html` | M | desktop, browser |
| 7 | **In the car.** Measure RPM rate and lag on the Jeep (VPW) and on a CAN car; tune the predictor; level against road noise; check phone mirror, Android Auto and AirPlay all sit under it; call ducking | M | Pi + Jeep, a CAN car |
| 8 | **Filling the catalog.** **Record engine sound**: a guided capture (idle, slow revs, a pull, lift-off) that logs RPM alongside the mic, and a pack builder that slices loops by RPM and load | L | desktop; Jeep + a mic |
| 9 | **Polish.** Limiter bounce, lift-off pops, turbo whoosh / blow-off from `INTAKE_PRESSURE`, cabin vs exhaust perspective | M | Jeep |

## Order

Step 1 gates everything. 2 → 3 give something to listen to on the desktop. 4 and 5 are independent after 3. 6 needs at least three packs to be worth building. 7 before any release note mentions the feature. 8 can start any time after 2, since the renderer defines what a pack must be.

## Cross-references

- `media-source-arbiter.md`: engine sound is deliberately not a source there; its ducking signals are reused.
- `diy-obd-adapter.md`: native fast mode would cut RPM lag.
- `arm64-qt-ffmpeg-lgpl.md`: avoided by decoding with stb_vorbis.
- `android-cpp-port.md`: audio focus on Android must not pause the engine stream when another app plays.

Delete this file (and its row in `TODO/README.md`) when done.
