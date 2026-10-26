# Launcher: Discord-style install, auto-update and rollback

**Status:** deferred (design agreed 2026-10-26, nothing built yet)
**Last updated:** 2026-10-26

**Goal:** on Linux, Windows and macOS, OCTAVE is started by a small separate `octave-launcher` that keeps the app up to date by itself, the way Discord does: it starts the newest installed version, downloads updates in the background, applies them on the next start, and rolls back to the previous version if a new one keeps crashing.

## Why it's parked

Nothing is broken today: the Jeep boots through `deployment/orange-pi-5-plus/home/.local/bin/octave-start.sh` (crash-restart loop), desktops through `scripts/octave-desktop.sh` (build from the checkout), and packaged builds are updated by hand from GitHub Releases. The launcher replaces all three with one supported path, and adds the one thing none of them has: going back to the last good version when a new one crashes on start. It is a new program plus CI work across three OSes, so it waits until the release pipeline is the way the Jeep gets its builds.

Two of today's scripts have drifted and are the reason to do this rather than patch them: `scripts/octave-update.sh` still names `/home/rob/Roxy` and an i3 config (the box runs Hyprland and `~/octave-cpp`), so updates on the Pi are not actually wired into boot any more.

## Why separate, and why "Discord-style"

Discussed and decided 2026-10-26:

- **Separate binary, not built into `octave`.** A version that crashes at start-up never runs its own update or rollback code, and a running program cannot swap its own executable (Windows refuses outright). The launcher is the one piece that must keep working when OCTAVE doesn't, so it is small and changes rarely.
- **No version picker as the main UI** (unlike the Minecraft launcher). Like Discord, the user never chooses a version: shortcuts, the menu entry and Hyprland's `exec-once` start the launcher, which starts the current version. The only window it owns is a small splash ("Checking for updates…") on the desktop and a fallback screen when every version fails.
- **Settings live in the app.** Channel, "update ready, restart to apply" and the release notes go in OCTAVE's own Settings > About > Software Updates, which writes the launcher's state file and exits with a restart request.

## Design

**Layout** (per user, no admin rights; Linux `~/.local/share/OCTAVE/`, Windows `%LOCALAPPDATA%\OCTAVE\`, macOS `~/Library/Application Support/OCTAVE/`):

```
octave-launcher[.exe]      the launcher (shortcuts point here, never at app-*)
app-0.9.5/                 previous version, kept for rollback
app-0.9.6/                 current version
packages/                  downloads in progress and verified, not yet applied
launcher.json              channel, current, previous, pending, bad versions, crash record
```

User data stays where it is today (`<config>`, `settingsConfigure.json`, trips, dashboards); versions share it. Only `app-*` folders are replaced. Keep the current version and the one before it, and delete older ones after a clean start.

**Start sequence**

1. If `packages/` holds a verified newer version, unpack it to `app-<v>/` and make it current (previous = old current).
2. Desktop: show the splash and check the feed for at most ~3 s; if a newer version is found and downloads quickly, apply it now, else start the current one and keep downloading. **Car mode** (`--car`, used on the Pi): never wait on the network. Start the current version at once and fetch in the background for the next boot, as `octave-update.sh --apply` / `--fetch` meant to.
3. Start `app-<current>/octave` with the arguments the launcher was given, and supervise it:
   - exit 0 → done (Shut Down, logout);
   - the app's "restart to apply" exit code → go back to step 1;
   - a crash → restart, the rule `octave-start.sh` uses today (5 crashes in 3 min → give up);
   - a new version that hits the crash limit before it has run cleanly for 2 min → mark it bad in `launcher.json`, make `previous` current again, start that, and skip the bad version until a newer one appears. The app shows "Rolled back from X: it crashed on start" (read from `launcher.json`) with the log tail from `octave-cpp-error.log`.
4. If every installed version fails: the fallback screen (log tail, "Try again", "Reinstall latest"). On the Pi it must work with a touchscreen only.

**Feed and downloads**

- Stable: GitHub Releases `releases/latest` (`v*` tags, which CI already attaches every artifact to). Nightly (later): a rolling `nightly` prerelease built from `main`.
- Per platform asset: Linux the AppImage (x86_64 / aarch64, already built: an AppImage is one file, so `app-<v>/` holds just it); Windows a new portable `OCTAVE-<v>-windows-x86_64.zip` (the `windeployqt` folder, next to the Inno Setup installer); macOS a new `OCTAVE-<v>-macos.zip` of `OCTAVE.app` (next to the `.dmg`).
- The `release` job publishes `SHA256SUMS`; the launcher refuses a download whose hash doesn't match. A detached signature (minisign or similar) is a later step, before nightlies are on by default.
- Full downloads only. Deltas (zsync for the AppImage, as AppImageUpdate does) only if download size turns out to matter.

**The launcher updating itself:** each release also carries the launcher. When an applied version's launcher is newer, it replaces the running one (Linux / macOS: write beside it and rename over it; Windows: rename the running `.exe` to `.old`, copy the new one in, delete `.old` on the next start).

**The app's side:** `octave` learns it runs under the launcher from `OCTAVE_LAUNCHER=<path to launcher.json>` in its environment. With it, Settings > About > Software Updates shows the channel, the installed and pending versions and the release notes, and "Restart to update" (exit with the restart code). Without it (a git checkout, an `.apk`, the browser demo) the page behaves as today (`NetworkManager::checkForUpdates()`'s git check, or the release check in `TODO/update-checker-packaged-builds.md`).

**Not covered:** Android, iOS and the browser demo cannot run a launcher (one install per package, no downloaded executables, the site serves one version). Their updates stay with `TODO/update-checker-packaged-builds.md`. Flatpak updates itself and is left alone.

## Steps

1. **Launcher core, Linux, no network.** `src/launcher/` as its own `qt_add_executable(octave-launcher ...)` (QtCore + QtGui for the splash; no managers, no `AppCore`), behind an `OCTAVE_BUILD_LAUNCHER` option that is off for Android, iOS and `EMSCRIPTEN`. `launcher.json` read and write, the start / supervise / restart / rollback state machine as pure functions, QtTest in `tests/cpp/tst_launcher.cpp` driving it with a fake app script that exits 0, crashes, or crashes only for one version. **Effort:** M. **Device:** desktop.
2. **Feed, download, verify, install, prune.** GitHub Releases API, `SHA256SUMS` check, AppImage into `app-<v>/`, keep two versions. CI: `SHA256SUMS` asset in the `release` job of `.github/workflows/build.yml`. Test against a local feed (`file://` or a tiny HTTP server in the test). **Effort:** M. **Device:** desktop, CI.
3. **The app side.** `OCTAVE_LAUNCHER` handling, the restart exit code, the rollback notice, the Software Updates section (`frontend/settings/AboutPage.qml`). Public surface changes → regenerate `docs/api` and the SDKs per `CLAUDE.md`. Wiki: `network-manager.html`, `settings-manager.html` if a setting is added, `building.html`, `development.html`. **Effort:** M. **Device:** desktop.
4. **The Jeep.** Decide first: the Pi moves from the `~/octave-cpp` source build to the aarch64 AppImage from releases (recommended), or the launcher gets a "local build" version type that points at a build folder. Then `exec-once` in `deployment/orange-pi-5-plus/home/.config/hypr/hyprland.conf` starts `octave-launcher --car`; keep the audio routing from `octave-start.sh` (move it into a small pre-start hook or the launcher's car mode). Delete `octave-start.sh`, `scripts/octave-update.sh` and the two "Moved" stubs in `scripts/`; update `deployment/orange-pi-5-plus/README.md`. Test a deliberately broken release on the bench before the car. **Effort:** M. **Device:** Pi.
5. **Windows.** Portable zip artifact in the `cpp-build-windows` job, the `.old` self-update dance, and the Inno Setup installer installing the launcher into `%LOCALAPPDATA%\OCTAVE\` with Start Menu / desktop shortcuts pointing at it. No code signing yet, so SmartScreen warns on the first run of the launcher (downloaded versions are not marked as from the internet). **Effort:** M. **Device:** Win11 VM.
6. **macOS.** `.app` zip artifact, launcher as its own small `.app`. Needs signing and notarization (Apple Developer account, `BUILD.md` iOS section) or Gatekeeper blocks it and every version change re-asks for Bluetooth / microphone permission. **Effort:** M once the account exists. **Device:** a Mac.
7. **Nightly channel.** A workflow on pushes to `main` that updates a `nightly` prerelease, the channel switch in the app, and signatures on by then. **Effort:** S–M. **Device:** CI, desktop.

## Order

1 → 2 → 3 on the desktop, then 4 on the Pi (the payoff: unattended updates with rollback in the car). 5 and 6 when a Windows / macOS machine is at hand (6 also waits on the Apple account). 7 last.

Related: `TODO/update-checker-packaged-builds.md` covers the platforms the launcher can't (Android, packaged builds run without the launcher); do it first or alongside step 3, since both touch the Software Updates section. `TODO/frontend-platform.md` item 3 (an exec frontend as the screen on the Pi) runs under whatever starts OCTAVE, so test it again after step 4.

Delete this file and its row in `TODO/README.md` when step 4 has run in the Jeep and 5 and 6 are done (7 can move to its own file).
