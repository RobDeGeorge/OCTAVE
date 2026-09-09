# OCTAVE Build Guide

OCTAVE ships as a **C++ / Qt 6** application. Every distributed binary — `.exe`, `.dmg`, `.AppImage`, `.apk`, and (planned) `.ipa` — is built from `src/` via CMake. The Python tree in `backend/` is a developer/tinkerer backend, not a distribution path.

This guide covers:

1. [Quick start](#quick-start)
2. [Desktop builds](#desktop-builds-linux--macos--windows) (Linux / macOS / Windows)
3. [Android build](#android-build) (full toolchain setup + APK)
4. [iOS build](#ios-build) (planned, not yet wired)
5. [Python developer backend](#python-developer-backend) (`pip install` and run)
6. [CI parity](#ci-parity) (how local commands map to GitHub Actions)

---

## Quick start

```bash
# Configure + build (Debug, current platform)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Run
./build/octave            # Linux/macOS
./build/Debug/octave.exe  # Windows MSVC multi-config
```

App-store builds (downloads feature stripped):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOCTAVE_ENABLE_DOWNLOADS=OFF
cmake --build build -j
```

---

## Desktop builds (Linux / macOS / Windows)

### Common requirements

- **CMake 3.21+**
- **Qt 6.7.3** (CI-pinned). Other 6.7.x / 6.8.x will likely work but aren't tested in CI.
- **Qt Quick 3D** — backs the 3D vehicle-attitude view (`frontend/CarMenu.qml`). Optional at
  build time: CMake probes for it and prints `QtQuick3D NOT found` if absent, in which case the
  app still builds and the "3D View" card on the Sensors hub greys itself out at runtime.
  Note that `RuntimeLoader` also needs Qt's **assimp asset-import plugin** to parse `cam.glb`.
  Official Qt (online installer / aqtinstall) bundles it with the `qtquick3d` module; distro Qt
  packages often split it out — on Arch, `qt6-quick3d` additionally needs `assimp` installed, or
  the model silently fails to load with `Failed to load asset import plugin with key: "assimp"`.
- **Ninja** (recommended on Linux/macOS for faster builds — `cmake -G Ninja`).
- A C++17 compiler (GCC 11+/Clang 14+/MSVC 2022).

### Linux

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
                    libgl1-mesa-dev libxkbcommon-dev libfontconfig1 libdbus-1-3 \
                    libxcb-cursor0 libxcb-xinerama0 libxcb-icccm4 libxcb-image0 \
                    libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 \
                    libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1
# Qt 6.7.3 — install via the Qt online installer or aqtinstall:
pipx install aqtinstall
aqt install-qt linux desktop 6.7.3 linux_gcc_64 -O ~/Qt \
    -m qtmultimedia qtnetworkauth qtserialport qtconnectivity qtsensors qtquick3d qt5compat qtshadertools

export PATH="$HOME/Qt/6.7.3/gcc_64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Outputs: `build/octave`. CI packages this into a portable `OCTAVE-<version>-x86_64.AppImage` via `packaging/linux/build-appimage.sh` (see [CI parity](#ci-parity)) — no local installer needed for development.

To produce an AppImage locally (matches CI's layout, but linked against your distro's glibc — only safe to run on the same distro):

```bash
# Ubuntu 22.04 prereqs (see the script's header for the full list)
sudo apt-get install -y libtag1-dev libavcodec-dev libavutil-dev libfuse2 wget file

bash packaging/linux/build-appimage.sh
# -> dist/OCTAVE-<version>-x86_64.AppImage
```

On Arch the bundled `linuxdeploy` `strip` rejects modern `.relr.dyn` ELF sections; pass `NO_STRIP=1` and expect a slightly larger artifact. AppImages built on Arch will not run on Ubuntu/Debian (newer glibc) — for distributable builds, let CI on `ubuntu-22.04` produce the AppImage.

### macOS

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Qt 6.7.3 (Apple Silicon and Intel both supported)
pipx install aqtinstall
aqt install-qt mac desktop 6.7.3 -O ~/Qt \
    -m qtmultimedia qtnetworkauth qtserialport qtconnectivity qtsensors qtquick3d qt5compat qtshadertools

export PATH="$HOME/Qt/6.7.3/macos/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Outputs: `build/octave.app`. CI runs `macdeployqt` and `hdiutil` to produce the `.dmg`.

### Windows

CI uses **vcpkg** to provide `taglib` and `ffmpeg[core,avcodec]` and pins Qt 6.7.3 via `jurplel/install-qt-action`.

> **Linux aarch64:** CI builds an `OCTAVE-<v>-linux-aarch64.AppImage` on an arm64 runner against the same
> Qt 6.7.3 (`cpp-build-linux-arm64`), for Orange Pi / Raspberry Pi class dash hardware. Google ships no
> aarch64 `adb`, so that build uses the distro `adb` on PATH (`sudo apt install adb`).
>
> **Qt modules the QML frontend needs at runtime:** `QtQuick.Effects` (Qt ≥ 6.5) and
> `Qt5Compat.GraphicalEffects` (`qt5compat`), both imported by `frontend/MainMenu.qml`. A build
> against a Qt without them links fine and then fails at `Main.qml` load. Distro Qt 6.2 (Ubuntu
> 22.04) cannot run OCTAVE for this reason; use the pinned 6.7.3 (aqtinstall offers `linux_arm64`
> desktop builds for ARM boards: `aqt install-qt linux_arm64 desktop 6.7.3 -O ~/Qt -m …`).
>
> **The phone-side mirror server is ours.** `phone_server/` is OCTAVE's fork of the scrcpy server
> (Apache-2.0, forked at v3.3.4, Java package `org.octave.phoneserver`). The prebuilt jar lives in
> `tools/phone-server/octave-phone-server` and is embedded in the C++ binary; rebuild it after
> changing the source with
> `ANDROID_HOME=~/Android/Sdk ANDROID_PLATFORM=36 ANDROID_BUILD_TOOLS=36.0.0 bash phone_server/build_without_gradle.sh`
> (needs `platforms;android-36`, `build-tools;36.0.0`, a JDK). The `phone-server` CI job rebuilds it
> on every push and fails if the committed jar differs from the source.
>
> **adb is bundled, not required from the user.** `python scripts/fetch_platform_tools.py`
> downloads Google's pinned platform-tools (SHA-256 verified) into `tools/platform-tools/<os>/`;
> CI runs it and ships `adb` next to the binary (`platform-tools/` beside the executable, or
> `Contents/Resources/platform-tools/` in the macOS bundle). Google publishes x86_64 only, so on
> ARM boards OCTAVE falls back to the distro's `adb` on PATH.
>
> **FFmpeg (libavcodec/libavutil) is optional on every platform.** It provides the H.264
> decoder for the built-in phone-mirror client (`phoneMirrorManager.nativeAvailable`).
> Without it OCTAVE still builds; phone mirroring then needs the external scrcpy binary.
> CMake prints `Built-in phone mirror decoder (libavcodec): ON/OFF` at configure time.

```powershell
# Visual Studio 2022 with "Desktop development with C++" workload required.
# Qt 6.7.3 — install via the Qt online installer (msvc2019_64 or msvc2022_64).
# vcpkg — bootstrap once if you don't have it.

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

Outputs: `build/Release/octave.exe`. CI runs `windeployqt` then `iscc.exe build_scripts/installer_windows.iss` to produce `OCTAVE_Setup_<version>.exe`.

---

## Android build

Produces an installable `.apk` for arm64-v8a (Android 11+). Tested on Samsung Galaxy Tab S6 and S22 Ultra.

**Distribution model:** LGPL Qt, dynamically linked, shared `.so`s bundled in APK. No commercial Qt license required at this stage.

**Scope note:** Spotify is **excluded from the Android build**. The Spotify experience is desktop-only — simplifies OAuth (no custom URI scheme gymnastics) and shrinks dependency surface for store review.

### Toolchain prerequisites

```bash
java -version          # OpenJDK 17.x — required by Android Gradle Plugin 8.x. Not 11, not 21.
cmake --version        # 3.21+
adb version            # sudo pacman -S android-tools  (or apt install android-tools-adb)
```

You'll need roughly **40–60 GB free** on the disk hosting `$HOME` (Qt for Android + SDK + NDK + build outputs).

### Step 1 — Install Qt for Android 6.7.3

We use **Qt 6.7.3** specifically — Qt 6.8+ moved behind a login-gated channel, while 6.7.3 is available through aqtinstall's public-mirror CLI path.

```bash
pipx install aqtinstall

# Android target build (~1 GB)
aqt install-qt linux android 6.7.3 android_arm64_v8a -O ~/Qt \
    -m qtmultimedia qtnetworkauth qtserialport qtconnectivity qtsensors qtquick3d qt5compat qtshadertools

# Desktop host tools Qt (needed for cross-compile, ~1 GB)
aqt install-qt linux desktop 6.7.3 linux_gcc_64 -O ~/Qt \
    -m qtmultimedia qtnetworkauth qtserialport qtconnectivity qtsensors qtquick3d qt5compat qtshadertools
```

Verify:

```bash
ls ~/Qt/6.7.3/android_arm64_v8a/bin/qt-cmake       # Android cross-compile wrapper
ls ~/Qt/6.7.3/gcc_64/bin/androiddeployqt           # APK packaging tool (runs on host)
```

**OpenSSL for Android:** wired in at build time via the KDAB `android_openssl` CMake project (already integrated). No separate install step needed.

### Step 2 — Install Android SDK + NDK

Pick one route.

**Route A (recommended): Android Studio.** Easier; keeps SDK up to date.

```bash
yay -S android-studio   # or download from https://developer.android.com/studio
```

Launch Android Studio once, accept licenses. Then via SDK Manager:
- **SDK Platforms:** Android 14 (API 34)
- **SDK Tools:** NDK (Side by side) — version `26.1.10909125` (what Qt 6.7.3 expects)
- **SDK Tools:** Android SDK Command-line Tools (latest), Build-Tools 35.0.0

**Route B: cmdline-tools only (lean).**

```bash
mkdir -p ~/Android/Sdk/cmdline-tools
cd ~/Downloads
curl -L -O "https://dl.google.com/android/repository/commandlinetools-linux-14742923_latest.zip"
unzip commandlinetools-linux-14742923_latest.zip
mv cmdline-tools ~/Android/Sdk/cmdline-tools/latest

yes | ~/Android/Sdk/cmdline-tools/latest/bin/sdkmanager --licenses

~/Android/Sdk/cmdline-tools/latest/bin/sdkmanager \
    "platform-tools" \
    "platforms;android-34" \
    "build-tools;35.0.0" \
    "ndk;26.1.10909125"
```

`cmdline-tools/` **must** live at `~/Android/Sdk/cmdline-tools/latest/` (not `~/Android/cmdline-tools/latest/`) or sdkmanager produces "inconsistent location" warnings.

### Step 3 — Environment variables

Add to `~/.bashrc` (or `~/.zshrc`):

```bash
# OCTAVE Android build toolchain
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export ANDROID_NDK_ROOT="$ANDROID_HOME/ndk/26.1.10909125"
export JAVA_HOME="/usr/lib/jvm/java-17-openjdk"
export QT_ANDROID_ROOT="$HOME/Qt/6.7.3/android_arm64_v8a"
export QT_HOST_PATH="$HOME/Qt/6.7.3/gcc_64"
export PATH="$QT_ANDROID_ROOT/bin:$QT_HOST_PATH/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
```

`QT_HOST_PATH` is required — `qt-cmake` uses it to locate the host toolchain during cross-compile. Without it, `find_package(Qt6 ...)` will set `Qt6_FOUND=FALSE` at configure time.

Reload and verify:

```bash
source ~/.bashrc
echo $ANDROID_NDK_ROOT
ls $ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake   # must exist
ls $QT_ANDROID_ROOT/lib/cmake/Qt6/qt.toolchain.cmake       # must exist
```

### Step 4 — Build the APK

```bash
qt-cmake -S . -B build-android \
    -DCMAKE_BUILD_TYPE=Debug \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-30 \
    -DOCTAVE_ENABLE_DOWNLOADS=OFF \
    -DQT_ANDROID_SIGN_APK=OFF

cmake --build build-android --target apk -j
adb install build-android/android-build/build/outputs/apk/debug/android-build-debug.apk
```

If `INSTALL_FAILED_UPDATE_INCOMPATIBLE`: a release-signed APK of the same package is already installed. `adb uninstall org.OCTAVE` first.

### Enabling USB debugging on devices

**Tab S6 / S22 Ultra:** Settings → About → Software information → tap **Build number** 7 times → back out → Developer options → USB debugging → ON. Plug in cable, accept the "Allow USB debugging?" prompt.

Verify both devices:

```bash
adb devices
# expect "device" (not "unauthorized") for each
```

### Android troubleshooting

| Symptom | Fix |
|---|---|
| `qt-cmake: command not found` | `QT_ANDROID_ROOT/bin` not on PATH — re-check Step 3 + `source ~/.bashrc` |
| `Could not find NDK` | `ANDROID_NDK_ROOT` points to a missing version — `ls ~/Android/Sdk/ndk/` |
| `Could not find Java 17` | `update-alternatives --config java` (Arch) or set `JAVA_HOME` explicitly |
| `adb devices` → `unauthorized` | Trust prompt was dismissed — unplug, replug, watch device screen |
| Gradle complains about JDK 21 | Something on PATH resolves to 21 — switch via `update-alternatives` |

---

## iOS build

**Status:** Not yet wired. iOS is the planned 5th target. CMakeLists.txt does not currently set up iOS toolchain or Info.plist generation, and there is no iOS job in CI. Implementing it requires:

- An Apple Developer account (US$99/yr) for code-signing certs.
- `qt-cmake` from `~/Qt/6.7.3/ios/` (separate aqtinstall pull).
- `qt_add_executable` + `set_target_properties(... MACOSX_BUNDLE_INFO_PLIST ...)` wiring.
- A signing identity exported as a CI secret (or local-only for sideload).

When this work happens, it will be added here as section 4 alongside Android.

---

## Python developer backend

The Python tree in `backend/` + `main.py` is a peer backend for tinkering — same QML frontend, same managers in pure Python. Useful for rapid prototyping, hardware hacking on a Pi, or running with a REPL attached.

```bash
python -m venv venv
source venv/bin/activate           # Windows: venv\Scripts\activate
pip install -r requirements.txt
python main.py
```

Flags:

```bash
python main.py --debug             # debug logging
python -m dev.main_dev             # developer mode: simulated OBD + keyboard controls
```

Lint and headless smoke tests (run by CI):

```bash
ruff check .
QT_QPA_PLATFORM=offscreen pytest tests/
```

The Python backend is **never** packaged or shipped. There is no PyInstaller pipeline. If you want a personal Python-side desktop binary for a Pi, run `python main.py` directly from the checkout.

**Mobile (Android/iOS) is C++-only.** Do not attempt to package the Python backend for mobile.

---

## CI parity

`.github/workflows/build.yml` is the source of truth. Each platform job runs the same CMake commands documented above.

| Platform | Runner | Build command | Output |
|---|---|---|---|
| Windows | `windows-latest` | `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=…/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release` then `iscc.exe build_scripts/installer_windows.iss` | `OCTAVE_Setup_<v>.exe` |
| macOS | `macos-latest` | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `macdeployqt` + `hdiutil` | `OCTAVE.dmg` |
| Linux | `ubuntu-22.04` | `bash packaging/linux/build-appimage.sh` (CMake Release + `linuxdeploy` + `linuxdeploy-plugin-qt`) | `OCTAVE-<v>-x86_64.AppImage` |
| Android | `ubuntu-22.04` | `qt-cmake -S . -B build-android …` then `cmake --build build-android --target apk` | `OCTAVE.apk` |

**Pinned versions** (matched locally for parity):

| Tool | Version |
|---|---|
| Qt | 6.7.3 |
| JDK | 17 |
| Android NDK | 26.1.10909125 |
| Android SDK platform | 34 |
| Android Build-Tools | 35.0.0 |
| CMake | 3.21+ (latest stable in CI) |

If a local build matches the pinned versions above, the same commands will produce the same artifacts as CI. Releases are cut on `v*` tag push and attach all 4 installers to a single GitHub Release.
