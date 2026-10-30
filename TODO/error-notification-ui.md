# Error notification: the remaining off-page cases

**Goal:** one small app-wide notice that tells the driver when the OBD link drops mid-drive (and, later, when phone mirror fails) while they're on a different page.

**Status:** mostly done (2026-10-18); see "Left". Errors go to the logs plus per-page notices by design (car UX: nothing chatty in the driver's peripheral vision). This is the plan for the one gap those don't cover.

**Last updated:** 2026-10-24

---

## What already exists (don't rebuild)

| Error | Where the user sees it today |
|---|---|
| Phone mirror error / dropped link | `PhoneMirrorView.qml`: dismissable notice banner (`phoneMirrorManager.mirrorNotice`, `dismissNotice()`), and on a dropped link it holds the last frame and reconnects quietly (`onScrcpyError`) |
| Dashboard editor / share errors | `DashboardEditor.qml` `snackbar` (`_showError`, also "removed · Undo"), `OBDMenu.qml` `shareStatusBanner` |
| Update available | Pulsing dot on the Settings About card (`SettingsMenu.qml` `updateAvailable`, `AboutPage.qml`) |
| "What went wrong earlier?" | Settings > About > Diagnostics: recent log lines, copy, export (`DiagnosticsManager`) |
| OBD status | `OBDHome.qml`, `OBDSettingsPage.qml`, `OBDMenu.qml`: only visible on those pages |

**The gap:** if the OBD adapter drops while you're on the media player or the home screen, nothing tells you. Same for a mirror failure when you're not on that page.

## Done 2026-10-18

`frontend/ErrorToast.qml` (top centre, fade, 8 s or tap, queue + dedupe; `tst_errortoast`), shown from `Main.qml` for `obdManager.connectionLostUnexpectedly(reason)` (a new C++ signal, emitted only when a live link drops on its own) unless an OBD page or Settings > OBD is showing (it also covered `downloadManager.downloadError` until the downloader was removed on 2026-10-24). Checked end to end in a sandboxed `--dev` run with `sim_link {"up": false}`: no notice on the OBD page, "OBD connection lost: …" on the media page. Decisions taken: no chime, one severity, top centre.

## Left

1. **Phone mirror failures off-page.** **Effort:** S. `phoneMirrorManager.scrcpyError(message)` → `errorToast.show(...)` only when `PhoneMirrorView` isn't current, and only for a dropped session, not every start-up error (reuse the view's `deviceErrorPattern` filtering). Check how a mirror behaves while its view isn't current first (it keeps streaming into the cached view).
2. **On the Pi:** confirm the notice is readable at a glance on the 1280x800 screen and not distracting; adjust size or duration if not.

Delete this file when both items above are done.
