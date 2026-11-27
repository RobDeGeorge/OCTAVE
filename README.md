<div align="center">

# OCTAVE

### **O**pen-source **C**ross-platform **T**elematics for **A**ugmented **V**ehicle **E**xperience

<img src="frontend/assets/readme/phone_home.png" alt="OCTAVE's Home: a phone's navigation mirrored in the middle, the playing song under it, speed, RPM, coolant temperature and voltage either side" width="900">

<sub>The real app, shot from a script: a real phone mirrored live (street names made up), engine data from the built-in car simulator.</sub>

[![Latest Release](https://img.shields.io/github/v/release/RobDeGeorge/OCTAVE?label=latest%20release&style=for-the-badge&color=41cd52)](https://github.com/RobDeGeorge/OCTAVE/releases/latest)
[![Stars](https://img.shields.io/github/stars/RobDeGeorge/OCTAVE?style=for-the-badge&color=ffb400&logo=github)](https://github.com/RobDeGeorge/OCTAVE/stargazers)
[![Downloads](https://img.shields.io/github/downloads/RobDeGeorge/OCTAVE/total?style=for-the-badge&color=2b8aff&label=downloads)](https://github.com/RobDeGeorge/OCTAVE/releases)
[![License: GPL v3+](https://img.shields.io/badge/license-GPLv3%2B-orange?style=for-the-badge)](LICENSE)

**Windows · macOS · Linux · Raspberry Pi / Orange Pi · Android · your browser**

**Try it in your browser: https://robdegeorge.com/OCTAVE/** (the simulated car, no install, about 25 MB)

### [Download](https://github.com/RobDeGeorge/OCTAVE/releases/latest) &nbsp;·&nbsp; [Wiki](https://robdegeorge.com/OCTAVE/wiki/) &nbsp;·&nbsp; [Build your own frontend](https://robdegeorge.com/OCTAVE/wiki/writing-a-frontend.html) &nbsp;·&nbsp; [Frontend playground](https://robdegeorge.com/OCTAVE/playground.html)

</div>

---

**OCTAVE is a car head unit you build yourself**, on a Pi behind the dash, a laptop or an old Android tablet. Your phone, your music and the car's own data on one screen, and every pixel yours to change. I daily-drive it in my 2003 Jeep TJ.

> **Getting data from your car:** plug a $15–30 ELM327 OBD-II adapter into the port under your dash (every US car since 1996 has one). USB works everywhere; Bluetooth pairs inside OCTAVE on Linux and the Pi, and through system settings on Windows and macOS; BLE works on Android.
>
> No car handy? `octave --sim` drives a simulated one, in any build.

<table>
<tr>
<td width="33%" align="center"><a href="#your-phone-on-the-dash"><img src="frontend/assets/readme/phone_mediaroom.png" alt="A phone's navigation beside the music controls"></a><br><b>Your phone on the dash</b><br><sub>Android by cable or Wi-Fi, iPhone over AirPlay</sub></td>
<td width="33%" align="center"><a href="#music-that-paints-the-whole-dash"><img src="frontend/assets/readme/media_media_room_playing.png" alt="Media room"></a><br><b>Music paints the dash</b><br><sub>Every song recolours the UI</sub></td>
<td width="33%" align="center"><a href="#a-home-that-is-yours"><img src="frontend/assets/readme/homes_glass.png" alt="The Glass Home screen"></a><br><b>A Home that is yours</b><br><sub>Pick one, or write your own</sub></td>
</tr>
<tr>
<td align="center"><a href="#a-black-box-for-every-drive"><img src="frontend/assets/readme/blackbox_coolant.png" alt="Trip history chart"></a><br><b>A black box</b><br><sub>Every drive recorded</sub></td>
<td align="center"><a href="#shift-light-warnings-and-trouble-codes"><img src="frontend/assets/readme/shiftlight_strobe.png" alt="Shift light"></a><br><b>Shift light & codes</b><br><sub>Read and clear the check-engine light</sub></td>
<td align="center"><a href="#drive-it-from-anything"><img src="frontend/assets/readme/webclient_tile.png" alt="OCTAVE's web client on a phone"></a><br><b>Open API</b><br><sub>Drive it from a browser or a script</sub></td>
</tr>
<tr>
<td align="center"><a href="#your-car-live-in-3d"><img src="frontend/assets/readme/jeep_open.png" alt="The 3D Jeep with its doors and hood open"></a><br><b>Your car in 3D</b><br><sub>The Jeep, a BMW, or a model of your own</sub></td>
<td align="center"><a href="#drive-it-from-anything"><img src="wiki/images/frontends/retro-cluster.png" alt="Retro Cluster, one of the example frontends"></a><br><b>Your own screen</b><br><sub>Swap the whole UI for one you wrote</sub></td>
<td align="center"><a href="#hardware-designed-here"><img src="frontend/assets/readme/obd_adapter_top.png" alt="A 3D render of OCTAVE's OBD adapter board"></a><br><b>Its own hardware</b><br><sub>Designed here, not built yet</sub></td>
</tr>
</table>

---

## Your phone on the dash

<p align="center"><img src="frontend/assets/readme/phone_mediaroom.png" alt="The music page with a phone's navigation where the album art would be, beside the transport keys" width="900"></p>

Plug in an Android phone and it is on the dash: in the middle of Home, or in place of the album art. **Nothing to install on the phone**, only USB debugging turned on.

- **Pull the cable and it carries on** over Wi-Fi (Android 11+), song and all.
- **Touch it on the dash**, and the dash's play, skip and volume drive the phone's player.

<p align="center">
  <img src="frontend/assets/readme/phone_portrait.png" alt="Home in an upright window: the phone on top, four readings and the song under it" width="27%">
</p>

**iPhone:** OCTAVE is an AirPlay receiver, and pairs as a Bluetooth mouse so taps on the dash reach the phone. Linux and the Pi for now.

**Android Auto** runs through Google's Desktop Head Unit or OCTAVE's own receiver (beta).

## Music that paints the whole dash

<p align="center"><img src="frontend/assets/readme/media.webp" alt="Skipping tracks: the accent, glow and background follow each album's art" width="900"></p>

Every song recolours the whole interface from its cover. Tap the art to play it on something else: a spinning vinyl, or one of five 3D players, from a record player to an iPod. Local MP3, M4A and FLAC.

<p align="center">
  <img src="frontend/assets/readme/media_ring.png" alt="The ring of players over the music page: the cover, a vinyl, a cassette deck, a CD player, an iPod, a record player" width="49%">
  <img src="frontend/assets/readme/media_media_player_playing.png" alt="The music library in the colours of the playing cover" width="49%">
</p>

## A Home that is yours

<p align="center"><img src="frontend/assets/readme/drive.webp" alt="The Cockpit Home pulling through the gears: a speed arc, the tachometer strip, readings either side, then the car's readings as cards" width="900"></p>

<p align="center">
  <img src="frontend/assets/readme/homes_cockpit.png" alt="Cockpit: a large speed arc with readings either side" width="32%">
  <img src="frontend/assets/readme/homes_glass.png" alt="Glass: album art and frosted panels over the blurred cover" width="32%">
  <img src="frontend/assets/readme/homes_trail.png" alt="Trail: the 3D Jeep with a compass strip, a G-force plot, pitch and roll" width="32%">
</p>
<p align="center">
  <img src="frontend/assets/readme/homes_retro.png" alt="Retro: a segment-display speedometer under a bar tachometer" width="32%">
  <img src="frontend/assets/readme/homes_rings.png" alt="Rings: four concentric rings for RPM, speed, throttle and load" width="32%">
  <img src="frontend/assets/readme/homes_telemetry.png" alt="Telemetry: four live traces beside the speed and a G-force plot" width="32%">
</p>
<p align="center"><sub>Cockpit, Glass, Trail, Retro, Rings, Telemetry: the same car, the same minute.</sub></p>

Pick your Home in Settings: six screens built by hand, each its own idea of a dash, or the phone-in-the-middle one at the top of this page.

Or write your own. A dashboard is a small JSON file: a grid of **16 widget types** bound to any of **129 live readings** ([format](docs/GAUGE_AUTHORING.md)). Drop it in the dashboards folder and it appears. Or build it in a browser, previewed live on the car's screen:

<p align="center"><img src="frontend/assets/readme/webeditor.png" alt="The dashboard editor in a browser, with the Home dashboard open: the phone in the middle, readings either side" width="80%"></p>

Every reading your car reports is one tap away as plain cards, and every page re-flows for an upright screen.

<p align="center">
  <img src="frontend/assets/readme/drive_drive_cards.png" alt="The OBD page: a card for each live reading" width="58%">
  &nbsp;
  <img src="frontend/assets/readme/portrait_home.png" alt="The Rings Home in an upright window" width="19%">
  <img src="frontend/assets/readme/portrait_cards.png" alt="The reading cards in an upright window" width="19%">
</p>

## A black box for every drive

<p align="center"><img src="frontend/assets/readme/blackbox.webp" alt="Trip History: the drive in progress recording at the top, then a past drive's stats and its coolant and voltage charts" width="900"></p>

OCTAVE records **every reading of every drive** in the background. Trip History charts any of them over a whole trip and exports CSV.

<p align="center"><img src="frontend/assets/readme/blackbox_trends_warmup.png" alt="Trends: warm-up time per drive over two months, creeping up, with a note that a thermostat stuck open is the usual cause" width="80%"></p>

**Trends** compares your drives: warm-up time, coolant, voltage, idle, fuel trims. When recent drives drift it says so, and what usually causes it. Here a thermostat is starting to stick open.

<sub>The history in these pictures is generated: 40 drives with a thermostat wearing out.</sub>

## Shift light, warnings and trouble codes

<p align="center"><img src="frontend/assets/readme/shiftlight.webp" alt="Revving up: the shift lamp turns green, flashes yellow, then the whole screen strobes red" width="900"></p>

Set your own RPM bands: green, flashing yellow, then **the whole screen strobes red**, whatever page you're on.

<p align="center"><img src="frontend/assets/readme/diagnostics.webp" alt="The engine runs hot and the tank low on the reading cards, then the diagnostics page reads two trouble codes and clears them" width="900"></p>

Check-engine light on? Read the codes in plain English and clear them from the dash.

## Your car, live in 3D

<p align="center"><img src="frontend/assets/readme/jeep.webp" alt="The 3D view: the Jeep with its doors and hood open over a level ring, pitch and roll in large numbers, then the BMW and the vehicle chooser" width="900"></p>

An inclinometer: your vehicle over a level ring, tilted in real time by an IMU on the dash. A rigged Jeep TJ and a BMW 330i ZHP ship with it, or add a model of your own car (any `.glb`, [guide](docs/VEHICLES.md)).

<p align="center">
  <img src="frontend/assets/readme/jeep_bmw.png" alt="The BMW 330i ZHP in the 3D view" width="49%">
  <img src="frontend/assets/readme/jeep_chooser.png" alt="Choose your vehicle: the Jeep, the BMW, or add your own 3D model" width="49%">
</p>

<p align="center"><img src="frontend/assets/readme/sensors.webp" alt="The sensors page while driving: pitch, roll, heading, a G-force trail, altitude" width="900"></p>

Pitch, roll, heading, G-force, altitude and temperature, sampled at 200 Hz.

## Drive it from anything

<table>
<tr>
<td width="38%" align="center"><img src="frontend/assets/readme/webclient_phone.png" alt="OCTAVE's web client on a phone: live engine gauges, now playing with controls, volume" width="280"></td>
<td>

Everything the touch screen can do is open to other programs over a small JSON API. Start OCTAVE with `--service --service-bind 0.0.0.0` and open **`http://<head-unit-ip>:9876/`** on your phone, or run it with **no screen at all** (`--headless`) and make your own UI.

```python
from octave_client import Client

with Client() as octave:
    octave.call("nowPlaying", "next")
    octave.subscribe("paramStore", properties=["RPM", "SPEED"])
    for event in octave.events():
        print(event["values"])        # {'RPM': 2210, 'SPEED': 41}
```

[SDKs](sdk/) for Python, TypeScript and Go, [starter projects](templates/), the [API reference](https://robdegeorge.com/OCTAVE/wiki/api-reference.html), and a walkthrough: **[Writing a Frontend](https://robdegeorge.com/OCTAVE/wiki/writing-a-frontend.html)**. Install yours as the head unit's screen from Settings; five presses of mute always bring the built-in one back.

**[Try a frontend in your browser](https://robdegeorge.com/OCTAVE/playground.html)**, or browse the **[gallery](https://robdegeorge.com/OCTAVE/wiki/frontend-gallery.html)** of five complete examples (`examples/frontends/`, MIT).

</td>
</tr>
</table>

<p align="center">
  <img src="wiki/images/frontends/glass.png" alt="Glass: a web frontend in Svelte" width="19%">
  <img src="wiki/images/frontends/sunlight.png" alt="Sunlight: a high-contrast native frontend in Go" width="19%">
  <img src="wiki/images/frontends/retro-cluster.png" alt="Retro Cluster: an instrument cluster in Rust" width="19%">
  <img src="wiki/images/frontends/arcade.png" alt="Arcade: a pixel-art dash in C and SDL2" width="19%">
  <img src="wiki/images/frontends/console.png" alt="Console: a terminal frontend in Python" width="19%">
</p>
<p align="center"><sub>Glass (Svelte), Sunlight (Go), Retro Cluster (Rust), Arcade (C + SDL2), Console (Python): five complete screens for the same car.</sub></p>

## Hardware designed here

<p align="center"><img src="frontend/assets/readme/obd_adapter_top.png" alt="A 3D render of the OCTAVE OBD adapter: an 80 by 55 mm board with an STM32, an ESP32 module, a USB-C socket and the OBD-II plug on one end" width="760"></p>

The $20 ELM327 clones work, and they are the slow part of the system. So OCTAVE is getting hardware of its own, designed in this repository as code: [`hardware/`](hardware/).

- **OBD adapter:** every OBD-II bus on one 80 x 55 mm board (two CAN, K-line, J1850 VPW / PWM for older cars like my TJ), an STM32 on the car's side, an ESP32 for Bluetooth and Wi-Fi, USB-C.
- **LoRa pod:** where the parked car is, beaconed over LoRa where there is no phone signal ([plan](TODO/lora-car-finder.md)).
- **Volume knob:** wireless, battery-powered, with a ring of twelve LEDs that follows the theme.

<p align="center">
  <img src="frontend/assets/readme/lora_pod_top.png" alt="The LoRa pod: a GPS module with its patch antenna, a LoRa and Bluetooth module, two pod-bus sockets" width="49%">
  <img src="frontend/assets/readme/volume_knob_top.png" alt="The volume knob: a round board with an encoder in the middle of a ring of twelve LEDs" width="49%">
</p>

> **Where it stands:** all three boards are designed and routed. **Nobody else has reviewed them, and none has been ordered or built.** Read them, tell me what is wrong with them, but do not send them to a fab yet ([plan](TODO/diy-obd-adapter.md)). Nothing in OCTAVE depends on them.

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
| Phone mirroring | Yes: Android with nothing to install, iPhone over AirPlay | AA only | AA only | n/a |
| Open API for your own UI | Yes | No | No | No |
| Fully themable / forkable UI | Yes, QML | limited | limited | No |
| Develop on your laptop | Yes, Win / macOS / Linux | Pi only | Pi only | n/a |

## Everything else it does

- **Swipe between sections**: a sideways flick on any page opens the next one
- **ESP32 wireless volume knob** with an LED ring that matches the theme
- **Gesture sensor** (PAJ7620U2): wave to skip, pause or change the volume
- **Bluetooth OBD adapters** found, paired and connected from the app on Linux; BLE adapters on Android
- **Readings your car's maker kept to itself**: describe a manufacturer-specific PID in a file and it becomes a gauge like any other ([guide](docs/CUSTOM_PIDS.md))
- **A bus monitor**: watch and record the raw messages on the vehicle bus, listen only ([guide](docs/BUS_MONITOR.md))
- **Spotify** (experimental): your playlists in the same song table as your own music, playing on the Spotify app on your phone (Premium)
- **Pick the audio output**: the car's amp, a USB DAC or Bluetooth, from Settings
- **The browser demo is the real app**: point it at a folder of your own music, or connect a real adapter from Chrome or Edge, Bluetooth LE or on a USB cable
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

Every platform and packaging target is in [BUILD.md](BUILD.md). The optional Python tools around the app (dev scripts, docs builders) install into a virtualenv next to the checkout:

```bash
python3 -m venv venv && venv/bin/pip install -r requirements.txt
```

Until October 2026 OCTAVE also had a Python / PySide6 backend. It is frozen, with its own wiki, on the [`python-final`](https://github.com/RobDeGeorge/OCTAVE/tree/python-final) branch.

</details>

<details>
<summary><b>Dev tooling: how the clips above were made</b></summary>

Every clip and picture in this README is scripted, and one command makes them all again. `./build/octave --dev --profile` (a Debug build) runs OCTAVE with the simulator and a local command server; a story script drives it scene by scene (navigate, play, floor the throttle, tap buttons by their label, type into fields), captures the window from inside Qt, and writes WebP and PNG. The same run fills the black box with two months of mock drives, shoots the browser pages in headless Chromium, renders the boards with `kicad-cli` from the committed design, and then checks that every picture this file points at exists. The same channel is exposed as MCP tools, so a coding agent can drive the app, change settings, read performance counters and record clips. The in-app half is in `src/dev/`; see the [Development guide](https://robdegeorge.com/OCTAVE/wiki/development.html).

</details>

## What's next

- **The OBD adapter and the pods as real boards**: a review, a first order, and firmware on hardware ([plan](TODO/diy-obd-adapter.md))
- **Android on the Play Store**: the APK runs today as a sideload ([plan](TODO/android-cpp-port.md))
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
| [OpenSSL](https://www.openssl.org/) | The built-in Android Auto receiver's encryption; the AirPlay receiver |
| [libplist](https://libimobiledevice.org/) | The AirPlay receiver (Linux), with FFmpeg and OpenSSL |
| [CMake](https://cmake.org/) | Build system |

**Python helper tools (optional, `requirements.txt`)**

| Package | Used for |
|---|---|
| [MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk), [Pillow](https://python-pillow.org/), [mss](https://github.com/BoboTiG/python-mss), [NumPy](https://numpy.org/), [psutil](https://github.com/giampaolo/psutil) | Dev tooling: MCP servers, screenshots and clips, perf scripts |
| [python-OBD](https://github.com/brendan-w/python-OBD) | Dev scripts for reading and clearing trouble codes |
| [Ruff](https://docs.astral.sh/ruff/) | Lint |

**Android**

| Library | Used for |
|---|---|
| Qt for Android, Gradle, Android SDK / NDK | Building the APK |
| [OpenSSL](https://www.openssl.org/) | HTTPS for Qt Network (`android/openssl/`) |

**Code bundled in this repository**

| Component | License | Where |
|---|---|---|
| [scrcpy](https://github.com/Genymobile/scrcpy) server, forked at v3.3.4 | Apache 2.0 | `phone_server/`, prebuilt jar in `tools/phone-server/` |
| [UxPlay](https://github.com/FDH2/UxPlay)'s AirPlay library, 1.74 | LGPL 2.1+, with GPL 3.0 and MIT parts (see its README) | `third_party/uxplay/` |
| [QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki | MIT | `src/phone_mirror/qrcodegen.*` |
| Android platform-tools (`adb`) | See its `NOTICE.txt` | `tools/platform-tools/` |

**External tools and services:** adb (phone mirroring and Android Auto), Google's Android Auto Desktop Head Unit (the Android Auto view).

**The boards (`hardware/`):** [KiCad](https://www.kicad.org/) (the project, `kicad-cli` checks, fab output and the renders above), [Freerouting](https://github.com/freerouting/freerouting) (autorouting), [ngspice](https://ngspice.sourceforge.io/) (simulations), `arm-none-eabi-gcc` (firmware).

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

The service clients (`sdk/`), starter projects (`templates/`) and example frontends (`examples/frontends/`) are MIT, so a frontend you build on them can use any license ([Your Frontend, Your License](wiki/writing-a-frontend.html#license)).

Code published before 2026-10-12 (v0.9.4 and earlier) was released under the MIT License and remains available under those terms.
