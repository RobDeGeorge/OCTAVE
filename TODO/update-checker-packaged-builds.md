# Update checker for packaged builds (AppImage / Windows / Android)

**Status:** deferred (found in 0.9.4 pre-release testing, not a regression)
**Last updated:** 2026-09-27

## Problem

Settings > About > Software Updates shows **"Could not determine local version"** with a red dot on every packaged build: the AppImage, the Windows installer and the Android APK. The same page's header shows the correct version (0.9.4), so users see a contradiction.

`NetworkManager::checkForUpdates()` (`src/managers/networkmanager.cpp`, around line 140) gets the local version by running `git rev-parse --short HEAD` in `repoDir()`, then compares it with `https://api.github.com/repos/<GITHUB_REPO>/commits/main`. That only works in a git checkout: the Orange Pi, and anyone running from source. Packaged builds have no `.git`, so git fails and the check stops at "Could not determine local version" (lines 169 and 263). The Python peer (`backend/network_manager.py`, around line 326) does the same, but Python only ever runs from a checkout.

It's cosmetic in the About page only. The bottom-bar Settings dot pulses only for `update-available`, never for `error`. 0.9.3 behaves the same way.

## Why it's parked

The proper fix is new behaviour, not a bug fix: packaged builds should be compared against the latest **release**, not `main` HEAD. Comparing a release binary against `main` would report "update available" as soon as anyone pushed to main. It touches both backends (parity) and the About page's messaging, and it needs its own round of testing. That was too much to add while cutting 0.9.4.

## Plan

1. C++: if `git rev-parse` fails (or `repoDir()` has no `.git`), fall back to the release check. `GET https://api.github.com/repos/<GITHUB_REPO>/releases/latest`, parse `tag_name` (e.g. `v0.9.5`), and compare it numerically with the compiled-in `OCTAVE_VERSION`.
   - Newer → `update-available`, with the message "Version X.Y.Z is available", and link to the release page. `canSelfUpdate` stays false: packaged builds can't `git pull`.
   - Equal or older → `up-to-date`.
   - Network error → the existing offline message, not "Could not determine local version".
2. Python: mirror the fallback in `backend/network_manager.py` for parity, even though Python normally runs from git (`backend/version.py` has `__version__`).
3. About page (`frontend/settings/AboutPage.qml`): check the Update button only appears when `canSelfUpdate`, and that packaged builds get a "Download" link to the release instead.
4. Wiki: `network-manager.html` (update checker section), plus `signals-slots-reference.html` if any property or signal changes.
5. Test on the clean VMs (kora): AppImage and Windows. On a machine with a checkout, confirm the git path is unchanged.

Delete this file when done.
