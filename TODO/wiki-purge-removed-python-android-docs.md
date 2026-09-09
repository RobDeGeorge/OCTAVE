# Purge removed Python-on-Android docs from the wiki

**Status:** deferred
**Last updated:** 2026-05-24

## Why it's parked

The Python-on-Android backend was removed (CLAUDE.md: "Mobile is C++-only … do
not reintroduce `backend/platform_config.py`, `backend/stubs.py`,
`backend/android_obd_manager.py`, `backend/android_sensors.py`,
`deployment/buildozer.spec`, or `requirements-android.txt`"). Several wiki pages
still document that removed infrastructure as if it were current. A docs-truth-up
pass fixed the headline pages; this remaining rot lives in deep reference pages,
is interconnected (shared TOC/nav anchors, cross-page consistency), and the
Android-OBD topic needs a **C++-source-accurate rewrite** (Android OBD-over-BLE is
a real shipped feature in the C++ backend) rather than a guess — so it was split
out to be done deliberately instead of rushed.

## Already fixed (companion pass, 2026-05-24)

- `dev/dev_main.py` → `python -m dev.main_dev` everywhere (README, BUILD.md, CLAUDE.md, development.html, getting-started.html)
- `getting-started.html` + `building.html` Android sections rewritten from Buildozer/p4a to the C++/`qt-cmake` path (point to BUILD.md)
- "Python-primary" framing corrected to dual-backend (C++ shipped + Python dev peer) in `index.html`, `architecture.html` (intro/diagram/table), `frontend-overview.html`, `settings-ui.html`
- `dashboards-roadmap.md` status un-staled (Phase 1 done, Phase 2 substantially landed)

## Remaining — sections that document deleted files

All reference files that no longer exist. Prefer **removal** for purely-obsolete
sections (Python is desktop-only now — there is no platform detection or stub
system to document); rewrite the Android-OBD section against the real C++ source.

1. **`wiki/utilities.html`**
   - "Platform Config" section (~L141–172) — documents deleted `backend/platform_config.py`. **Remove** + drop the `#platform-config` nav/TOC anchor.
   - "Android Stubs" section (~L278–~325) — documents deleted `backend/stubs.py` (Stub* classes, QQuickItem stubs, `main.py` conditional import). **Remove** + drop the `#stubs` nav/TOC anchor.
2. **`wiki/architecture.html`**
   - "Platform Detection & Stubs" section (~L494–~545) — deleted modules + `android_obd_manager`/`android_sensors` conditional imports. **Remove/replace** + drop `#platform-detection` nav anchor.
   - L~289 inline `backend.platform_config` reference in the startup prose — fix.
3. **`wiki/obd-manager.html`** — Android OBD is a real shipped C++ feature; do NOT just delete.
   - "AndroidOBDManager" section (L~432–487), nav link `#android-variant` (L35), intro mention (L51), ASCII diagram label (L74). **Rewrite** to describe the C++ Android OBD path (Bluetooth RFCOMM + BLE) — read the actual C++ implementation in `src/managers/` (the OBD-over-BLE work behind `#ifdef Q_OS_ANDROID`) first so the rewrite is accurate, or trim to a concise pointer to BUILD.md.
4. **`wiki/troubleshooting.html`** — "Android-Specific Issues" section (L323–342): "Stub Managers" (deleted `stubs.py`) + "File Paths … via `platform_config.py`". Rewrite to reflect the C++ Android reality (no Python stubs; paths via Qt `QStandardPaths`).
5. **`wiki/network-manager.html`** (L~214–221) — `_check_can_self_update()` example imports `backend.platform_config`. Self-update is desktop-from-source only; simplify the example to drop the deleted import.
6. **`wiki/settings-manager.html`** (L~74–83) — `get_app_data_dir()` example imports `backend.platform_config`. Either drop the Android branch or frame it as the Python desktop dev backend; the shipped path logic is C++ `SettingsManager::getAppDataDir()`.

## After editing

Run `python build_search_index.py` **from inside `wiki/`** (it globs `*.html` in
its own directory — running it from the repo root wipes the index to 0 pages).

## Cross-references

- Companion to the settings-icon conversion (same 2026-05-24 cohesion/cleanup pass), which shipped 2026-08-08 — `TODO/settings-icons-svg.md` was deleted then.
- Enforces the CLAUDE.md "mobile is C++-only / Python is desktop-only" rule at the docs layer.

Delete this file when all six items are done.
