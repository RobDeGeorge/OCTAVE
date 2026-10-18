# Update checker for packaged builds (AppImage / Windows / Android)

**Status:** deferred (found in 0.9.4 pre-release testing, not a regression)
**Last updated:** 2026-10-18

**Goal:** Packaged builds should compare themselves against the latest GitHub release and offer a download link, instead of showing "Could not determine local version".

## Problem

On every packaged build, Settings > About > Software Updates shows "Could not determine local version" with a red dot, while the page header shows the right version. `NetworkManager::checkForUpdates()` (`src/managers/networkmanager.cpp`) gets the local version from `git rev-parse --short HEAD` (`:274`) and compares it with `commits/main` (`:180`). Packaged builds have no `.git`, so the check stops at "Could not determine local version" (`:170-176` and `:278-280`). This only affects the About page: the bottom-bar dot pulses for `update-available` only. Comparing a release binary with `main` would be wrong anyway, so packaged builds need a release check.

The About page already shows the Update button only when `canSelfUpdate` is true (`frontend/settings/AboutPage.qml:347, 369`).

## Steps

1. **C++ (`networkmanager.cpp`):** when `git rev-parse` fails or `repoDir()` has no `.git`, call `GET https://api.github.com/repos/<GITHUB_REPO>/releases/latest` instead. Parse `tag_name` (e.g. `v0.9.5`), strip the `v`, and compare it numerically with the compiled-in `OCTAVE_VERSION` (split on `.` and `-rc`).
   - Newer: status `update-available`, message "Version X.Y.Z is available", and expose the release `html_url` (a new `Q_PROPERTY QString releaseUrl` or similar). `canSelfUpdate` stays false.
   - Equal or older: `up-to-date`.
   - Network or HTTP error: the existing offline message.
   - Pull the version comparison out as a pure function and add a QtTest in `tests/cpp/`.
2. **About page (`AboutPage.qml:520-528`):** replace the "Pull the latest changes from GitHub to update." text (and its stale "PyInstaller builds" comment) with a "Download" link that opens `releaseUrl` when an update is available, and nothing extra when the build is up to date.
3. **Wiki:** `network-manager.html` (update checker section), plus `signals-slots-reference.html` for the new property.

**Effort:** M. **Needs:** the kora VMs (AppImage and Windows installer), the Galaxy S22 (APK), and a machine with a checkout (e.g. the Orange Pi) to confirm the git path is unchanged.

Delete this file when done.
