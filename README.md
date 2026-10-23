<div align="center">

# OCTAVE

### The car head unit you build yourself.

**Live engine data. Gauges you design on the screen. Music that repaints the whole dash.<br>Your phone on the dash. A black box that remembers every drive.**

<img src="frontend/assets/readme/drive.webp" alt="OCTAVE pulling through the gears: album art beside live speed and RPM, the whole UI coloured by the cover" width="900">

<sub>Real app, real UI, recorded from a script. Engine data comes from OCTAVE's built-in car simulator, so you can try all of this with no car.</sub>

[![Latest Release](https://img.shields.io/github/v/release/RobDeGeorge/OCTAVE?label=latest%20release&style=for-the-badge&color=41cd52)](https://github.com/RobDeGeorge/OCTAVE/releases/latest)
[![Stars](https://img.shields.io/github/stars/RobDeGeorge/OCTAVE?style=for-the-badge&color=ffb400&logo=github)](https://github.com/RobDeGeorge/OCTAVE/stargazers)
[![Downloads](https://img.shields.io/github/downloads/RobDeGeorge/OCTAVE/total?style=for-the-badge&color=2b8aff&label=downloads)](https://github.com/RobDeGeorge/OCTAVE/releases)
[![License: GPL v3+](https://img.shields.io/badge/license-GPLv3%2B-orange?style=for-the-badge)](LICENSE)

**Windows · macOS · Linux · Raspberry Pi / Orange Pi · Android · your browser**

**Try it in your browser: https://robdegeorge.com/OCTAVE/** (the simulated car, no install, about 25 MB)

### [Download](https://github.com/RobDeGeorge/OCTAVE/releases/latest) &nbsp;·&nbsp; [Wiki](https://robdegeorge.com/OCTAVE/wiki/) &nbsp;·&nbsp; [Build your own frontend](https://robdegeorge.com/OCTAVE/wiki/writing-a-frontend.html)

</div>

---

Stock head units are locked boxes. Phone projection hides the car behind your phone. **OCTAVE is a full infotainment system you run on your own hardware**: a Pi behind the dash, a laptop in a project car, an old Android tablet. It reads the car over OBD-II, plays your music, mirrors your phone, and every pixel of it is yours to change.

I daily-drive it in my 2003 Jeep TJ.

> **Getting data from your car:** plug an ELM327 OBD-II adapter (the cheap $15–30 kind) into the port under your dash. Every car sold in the US since 1996 has one.
> - **USB** adapters work on every platform.
> - **Bluetooth** adapters pair from inside OCTAVE on Linux and the Pi. On Windows and macOS, pair it in system settings and pick its port.
> - **BLE** adapters work on Android.
>
> No car handy? `octave --sim` drives a simulated one, in any build.

<table>
<tr>
<td width="33%" align="center"><a href="#music-that-paints-the-whole-dash"><img src="frontend/assets/readme/media_media_room_playing.png" alt="Media room"></a><br><b>Music paints the dash</b><br><sub>Every song recolours the UI</sub></td>
<td width="33%" align="center"><a href="#gauges-you-design-on-the-screen"><img src="frontend/assets/readme/dashboards_sport.png" alt="Sport dashboard"></a><br><b>Gauges you design</b><br><sub>Drag-and-drop editor, 129 live readings</sub></td>
<td width="33%" align="center"><a href="#a-black-box-for-every-drive"><img src="frontend/assets/readme/blackbox_coolant.png" alt="Trip history chart"></a><br><b>A black box</b><br><sub>Every drive recorded and charted</sub></td>
</tr>
<tr>
<td align="center"><a href="#your-phone-on-the-dash"><img src="frontend/assets/readme/mirror_live.png" alt="Phone mirroring"></a><br><b>Your phone on the dash</b><br><sub>USB or Wi-Fi, nothing to install</sub></td>
<td align="center"><a href="#shift-light-warnings-and-trouble-codes"><img src="frontend/assets/readme/shiftlight_strobe.png" alt="Shift light"></a><br><b>Shift light & codes</b><br><sub>Read and clear the check-engine light</sub></td>
<td align="center"><a href="#drive-it-from-anything"><img src="frontend/assets/readme/webclient_tile.png" alt="OCTAVE's web client on a phone"></a><br><b>Open API</b><br><sub>Drive it from a browser or a script</sub></td>
</tr>
</table>

---

## Music that paints the whole dash

<p align="center"><img src="frontend/assets/readme/media.webp" alt="Skipping tracks: the accent, glow and background follow each album's art" width="900"></p>

Skip a track and the whole interface takes its colours from the cover art. Local MP3, M4A and FLAC with a live waveform, and a search that pulls tracks from YouTube Music straight into your library.

<p align="center">
  <img src="frontend/assets/readme/media_media_player_playing.png" alt="The music library in the red of the playing cover" width="49%">
  <img src="frontend/assets/readme/library_search.png" alt="Searching YouTube Music for David Bowie, each result with album art and a download button" width="49%">
</p>

## Gauges you design on the screen

<p align="center"><img src="frontend/assets/readme/dashboards.webp" alt="The dashboard chooser with live miniatures, then Sport, Performance, TJ Wrangler and Full Grid under load" width="900"></p>

**129 live readings** (121 OBD-II PIDs plus the IMU), **11 widget types**, and full-screen dashboards you swipe between. Pick one of the built-ins or build your own:

<p align="center"><img src="frontend/assets/readme/editor.webp" alt="Building a dashboard in the editor: tap a cell, pick a gauge and a parameter, preview with demo data, save, drive on it" width="900"></p>

No code and no JSON: tap an empty cell, pick a gauge, pick what it shows, drag it where you want it. Hit **Demo data** to watch it move before you save. Share it as a file or through the clipboard.

<p align="center">
  <img src="frontend/assets/readme/dashboards_fullgrid.png" alt="Full Grid dashboard: eight circular gauges" width="49%">
  <img src="frontend/assets/readme/dashboards_tj_wrangler.png" alt="TJ Wrangler 4.0 dashboard: fuel trims, O2 sensors, timing and pressures" width="49%">
</p>

## A black box for every drive

<p align="center"><img src="frontend/assets/readme/blackbox.webp" alt="Trip History: the drive in progress recording at the top, then a past drive's stats and its coolant and voltage charts" width="900"></p>

OCTAVE records **every reading of every drive**, quietly, in the background. Open Trip History to see each drive's distance, top speed, peak coolant, voltage and fuel used, and chart any parameter over the whole trip. Export a drive as CSV, or open the files yourself.

## Shift light, warnings and trouble codes

<p align="center"><img src="frontend/assets/readme/shiftlight.webp" alt="Revving up: the shift lamp turns green, flashes yellow, then the whole screen strobes red" width="900"></p>

Set your own RPM bands: the lamp goes green, flashes yellow, and at the top **the whole screen strobes red**, whatever page you're on. Any gauge can also be a warning light with its own threshold.

<p align="center"><img src="frontend/assets/readme/diagnostics.webp" alt="TEMP and FUEL warning lights come on, then the diagnostics page reads two trouble codes, takes a freeze frame and clears them" width="900"></p>

Check-engine light on? Read the stored and pending codes in plain English, grab the freeze frame, and clear them, right from the dash.

## Your phone on the dash

<p align="center"><img src="frontend/assets/readme/mirror_live.png" alt="A Samsung phone's DeX desktop mirrored onto the head unit" width="900"></p>

Plug in an Android phone, or pair it once over Wi-Fi by scanning a QR code. OCTAVE mirrors a DeX-style desktop onto the dash with its own built-in client: **nothing to install on the phone**. A bumped USB cable picks up where it left off instead of starting over.

## The car, live in 3D

<p align="center"><img src="frontend/assets/readme/jeep.webp" alt="A rigged 3D Jeep TJ: doors, hood and tailgate open, lights on, steering and wheels turning, tilted by the IMU" width="900"></p>

A rigged model of the Jeep with working doors, hood, tailgate, lights, steering and wheels, rolled and pitched in real time by an IMU on the dash.

<p align="center"><img src="frontend/assets/readme/sensors.webp" alt="The sensors page while driving: pitch, roll, heading, a G-force trail, altitude" width="900"></p>

Pitch, roll, heading, a G-force plot with a trail, altitude and temperature, sampled at 200 Hz.

## Drive it from anything

<table>
<tr>
<td width="38%" align="center"><img src="frontend/assets/readme/webclient_phone.png" alt="OCTAVE's web client on a phone: live engine gauges, now playing with controls, volume" width="280"></td>
<td>

Everything the touch screen can do is open to other programs over a small JSON API. Start OCTAVE with `--service --service-bind 0.0.0.0` and open **`http://<head-unit-ip>:9876/`** on your phone, enter the pairing code from Settings > Device > Service: live gauges, now playing, volume. Or run it with **no screen at all** (`--headless`) and make your own UI.

```python
from octave_client import Client

with Client() as octave:
    octave.call("nowPlaying", "next")
    octave.subscribe("paramStore", properties=["RPM", "SPEED"])
    for event in octave.events():
        print(event["values"])        # {'RPM': 2210, 'SPEED': 41}
```

[SDKs](sdk/) for Python, TypeScript and Go, a no-build [web client](sdk/web/index.html), [starter projects](templates/) to copy, the full [API reference](https://robdegeorge.com/OCTAVE/wiki/api-reference.html), and a walkthrough: **[Writing a Frontend](https://robdegeorge.com/OCTAVE/wiki/writing-a-frontend.html)**. Install your frontend as the head unit's screen from Settings > Device > Frontends.

</td>
</tr>
</table>

---

## Install

Grab the file for your platform from **[Releases](https://github.com/RobDeGeorge/OCTAVE/releases/latest)**. No toolchain needed.

| Platform | File | |
|---|---|---|
| Windows | `OCTAVE-<version>-windows-x86_64.exe` | Run the installer |
| macOS | `OCTAVE-<version>-macos.dmg` | Drag to Applications |
| Linux | `OCTAVE-<version>-linux-x86_64.AppImage` | `chmod +x`, then run |
| Raspberry Pi / Orange Pi (64-bit) | `OCTAVE-<version>-linux-aarch64.AppImage` | `chmod +x`, then run |
| Android | `OCTAVE-<version>-android-arm64-v8a.apk` | Sideload it |

<details>
<summary>Platform notes</summary>

- **Linux:** Ubuntu 22.04+, Debian, Mint, Fedora, openSUSE and Arch. Arch may need `sudo pacman -S fuse2`, or run `./OCTAVE-*.AppImage --appimage-extract-and-run`.
- **Android:** each release APK is signed with its own key, so uninstall the previous version before updating.

</details>

## How it compares

| | OCTAVE | Crankshaft | OpenAuto Pro | Stock Android Auto |
|---|---|---|---|---|
| Open source | Yes, GPLv3 | Yes | partial | No |
| Works without a phone | Yes | No, AA projection | No, AA projection | No |
| Live OBD-II, your own dashboards | Yes | No | limited | No |
| Records every drive | Yes | No | No | No |
| Phone mirroring | Yes, nothing to install | AA only | AA only | n/a |
| Open API for your own UI | Yes | No | No | No |
| Fully themable / forkable UI | Yes, QML | limited | limited | No |
| Develop on your laptop | Yes, Win / macOS / Linux | Pi only | Pi only | n/a |

## Everything else it does

- **ESP32 wireless volume knob** with an LED ring that matches the theme
- **Gesture sensor** (PAJ7620U2): wave to skip, pause or change the volume
- **Bluetooth OBD adapters** found, paired and connected from the app on Linux; BLE adapters on Android
- **100+ settings**, all on the screen, all saved
- **The whole wiki inside the app**, readable offline
- **Logs and crash traces** you can export from the dash when something goes wrong

---

## Hack on it

OCTAVE is C++ / Qt 6 with a QML frontend, built with CMake, small enough to fork. `--dev` runs it with a **simulated car** (warm-up, overheating and low-fuel scenarios) and simulated motion, so you can work on gauges at your desk.

<details>
<summary><b>Build from source</b></summary>

Qt 6.7+ and CMake 3.21+:

```bash
git clone https://github.com/RobDeGeorge/OCTAVE.git
cd OCTAVE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
./build/octave --dev                       # with the simulated car
ctest --test-dir build --output-on-failure # the test suite
```

Every platform and packaging target is in [BUILD.md](BUILD.md). Music downloads on desktop use yt-dlp; put it in a virtualenv next to the checkout, where OCTAVE looks first:

```bash
python3 -m venv venv && venv/bin/pip install -r requirements.txt
```

Until October 2026 OCTAVE also had a Python / PySide6 backend. It is frozen, with its own wiki, on the [`python-final`](https://github.com/RobDeGeorge/OCTAVE/tree/python-final) branch.

</details>

<details>
<summary><b>Dev tooling: how the clips above were made</b></summary>

Every clip and screenshot in this README is scripted. `./build/octave --dev --profile` (a Debug build) runs OCTAVE with the simulator and a local command server; a story script drives it scene by scene (navigate, play, floor the throttle, tap buttons by their label, type into fields, build a dashboard), captures the window from inside Qt, and writes WebP and PNG. The same channel is exposed as MCP tools, so a coding agent can drive the app, change settings, read performance counters and record clips. The in-app half is in `src/dev/`; see the [Development guide](https://robdegeorge.com/OCTAVE/wiki/development.html).

</details>

<details>
<summary><b>YouTube downloads failing?</b></summary>

*"Sign in to confirm you're not a bot"* usually means a VPN: YouTube blocks most shared VPN exit IPs. Downloads work again on a home connection or a dedicated-IP exit.

To stay on the VPN, sign in through a cookies file instead:

1. Install **Get cookies.txt LOCALLY** in a Chromium-based browser (desktop Chrome / Brave / Edge / Vivaldi; on Android, **Kiwi** or **Brave**).
2. Log into <https://www.youtube.com> in that browser.
3. On a YouTube tab, click the extension → **Export → cookies.txt**.
4. Save it as `youtube_cookies.txt` in your **Downloads** folder:
   - Linux / macOS: `~/Downloads/youtube_cookies.txt`
   - Windows: `%USERPROFILE%\Downloads\youtube_cookies.txt`
   - Android: `/storage/emulated/0/Download/youtube_cookies.txt`
5. Restart OCTAVE. It picks the file up on its own, and the cookies usually last for weeks.

</details>

## What's next

- **Android on the Play Store**: the APK runs today as a sideload ([plan](TODO/android-cpp-port.md))
- **Health trends across drives** on top of the black box ([plan](TODO/trip-history.md))
- **Wireless CarPlay / Android Auto** through a CarlinKit dongle ([plan](TODO/carlinkit-dongle-port.md))
- **Video over the API**, so your own frontend can show the phone mirror too ([plan](TODO/open-backend.md))

Every plan lives in [`TODO/`](TODO/).

## Documentation

The **[wiki](https://robdegeorge.com/OCTAVE/wiki/)** covers the architecture, every manager and page, every setting, hardware setup and the build guides. The same pages ship in [`wiki/`](wiki/index.html) and inside the app. Building gauges: [`docs/GAUGE_AUTHORING.md`](docs/GAUGE_AUTHORING.md). Building a frontend: [Writing a Frontend](https://robdegeorge.com/OCTAVE/wiki/writing-a-frontend.html) and [`docs/api/`](docs/api/README.md).

## Contributing

Pull requests and bug reports are welcome. Built a dashboard, a sensor integration, a port to new hardware, or your own frontend? Open an issue or a PR. I'd love to see it.

<a href="https://star-history.com/#RobDeGeorge/OCTAVE&Date">
  <img src="https://api.star-history.com/svg?repos=RobDeGeorge/OCTAVE&type=Date" alt="Star History Chart" width="720">
</a>

<details>
<summary><b>Built with</b></summary>

OCTAVE stands on a lot of other people's work.

**C++ app (every release binary)**

| Library | Used for |
|---|---|
| [Qt 6](https://www.qt.io/) (Core, Gui, Qml, Quick, Quick Controls 2, Multimedia, Network, Concurrent, SerialPort, Bluetooth) | The whole application: UI, audio playback, networking, serial and Bluetooth OBD |
| Qt Quick 3D, Quick 3D Asset Utils, Quick Timeline | The 3D Jeep and the music-device scenes |
| Qt DBus | BlueZ pairing on Linux |
| Qt WebEngine / Qt WebView | The offline wiki reader (desktop / Android) |
| Qt Sensors | The phone's motion sensors on Android |
| [TagLib](https://taglib.org/) | Reading tags and embedded album art |
| [FFmpeg](https://ffmpeg.org/) (libavcodec, libavutil) | Decoding the phone mirror's H.264 stream |
| [SQLite](https://sqlite.org/) | The black box (compiled in from `third_party/sqlite/`) |
| [CMake](https://cmake.org/) | Build system |

**Python helper tools (optional, `requirements.txt`)**

| Package | Used for |
|---|---|
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) | Music downloads on desktop |
| [ytmusicapi](https://github.com/sigma67/ytmusicapi) | YouTube Music search and metadata (`scripts/ytmusic_search.py`, also bundled into the APK) |
| [MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk), [Pillow](https://python-pillow.org/), [mss](https://github.com/BoboTiG/python-mss), [NumPy](https://numpy.org/), [psutil](https://github.com/giampaolo/psutil) | Dev tooling: MCP servers, screenshots and clips, perf scripts |
| [python-OBD](https://github.com/brendan-w/python-OBD) | Dev scripts for reading and clearing trouble codes |
| [Ruff](https://docs.astral.sh/ruff/) | Lint |

**Android**

| Library | Used for |
|---|---|
| Qt for Android, Gradle, Android SDK / NDK | Building the APK |
| [youtubedl-android](https://github.com/JunkFood02/youtubedl-android) (library + ffmpeg) | yt-dlp and FFmpeg on the phone |
| [OpenSSL](https://www.openssl.org/) | HTTPS for Qt Network (`android/openssl/`) |

**Code bundled in this repository**

| Component | License | Where |
|---|---|---|
| [scrcpy](https://github.com/Genymobile/scrcpy) server, forked at v3.3.4 | Apache 2.0 | `phone_server/`, prebuilt jar in `tools/phone-server/` |
| [QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki | MIT | `src/phone_mirror/qrcodegen.*` |
| Android platform-tools (`adb`) | See its `NOTICE.txt` | `tools/platform-tools/` |

**External tools and services:** adb (phone mirroring and Android Auto), Google's Android Auto Desktop Head Unit (the Android Auto view), YouTube Music (search and downloads).

**Fonts, icons and 3D assets**

- **[Google Sans](https://fonts.google.com/)**: UI typeface. **[JetBrains Mono](https://www.jetbrains.com/lp/mono/)**: monospace readouts.
- **OCTAVE Symbols**: a subset of [Noto Sans Symbols and Noto Sans Symbols 2](https://fonts.google.com/noto) (SIL OFL), renamed as the OFL requires, built by `tools/fonts/build_symbol_font.py` with [fontTools](https://github.com/fonttools/fonttools).
- **Icons**: original SVGs drawn for OCTAVE (Illustrator sources in `frontend/assets/-src/`).
- **The 2003 Jeep TJ**: an original model, built procedurally in [Blender](https://www.blender.org/) by `tools/vehicle/build_jeep_tj.py` from photos of one real TJ; no third-party meshes or textures ([details](tools/vehicle/README.md)).
- **Record player, cassette deck, CD player, iPod and friends**: original glTF models generated by `tools/music_devices/generate.py` (NumPy, Pillow).
- **Studio lighting (`.hdr`)**: generated by `tools/vehicle/build_studio_probe.py`.

**CI and packaging:** [GitHub Actions](https://github.com/features/actions) with [install-qt-action](https://github.com/jurplel/install-qt-action), [setup-android](https://github.com/android-actions/setup-android) and [action-gh-release](https://github.com/softprops/action-gh-release); [vcpkg](https://vcpkg.io/) and [Homebrew](https://brew.sh/) for TagLib and FFmpeg; [Inno Setup](https://jrsoftware.org/isinfo.php) for the Windows installer; [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) with its Qt plugin for the AppImages; `windeployqt`, `macdeployqt` and `androiddeployqt` from Qt.

</details>

## License

Copyright 2026 Rob DeGeorge.

OCTAVE is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.

Code published before 2026-10-12 (v0.9.4 and earlier) was released under the MIT License and remains available under those terms.
