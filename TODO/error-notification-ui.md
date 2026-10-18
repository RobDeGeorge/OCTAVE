# Error Notification UI (Parked)

**Goal:** one small app-wide notice that tells the driver when the OBD link drops mid-drive (and, later, when phone mirror or a download fails) while they're on a different page.

**Status:** deferred. Errors go to the logs plus per-page notices by design (car UX: nothing chatty in the driver's peripheral vision). This is the plan for the one gap those don't cover.

**Last updated:** 2026-10-18

---

## What already exists (don't rebuild)

| Error | Where the user sees it today |
|---|---|
| Phone mirror error / dropped link | `PhoneMirrorView.qml`: dismissable notice banner (`phoneMirrorManager.mirrorNotice`, `dismissNotice()`), and on a dropped link it holds the last frame and reconnects quietly (`onScrcpyError`) |
| Download failed | `DownloadPage.qml` `onDownloadError`: status text + queue row |
| Dashboard editor / share errors | `DashboardEditor.qml` `errorBanner`, `OBDMenu.qml` `shareStatusBanner` |
| Update available | Pulsing dot on the Settings About card (`SettingsMenu.qml` `updateAvailable`, `AboutPage.qml`) |
| "What went wrong earlier?" | Settings > About > Diagnostics: recent log lines, copy, export (`DiagnosticsManager`) |
| OBD status | `OBDHome.qml`, `OBDSettingsPage.qml`, `OBDMenu.qml`: only visible on those pages |

**The gap:** if the OBD adapter drops while you're on the media player or the home screen, nothing tells you. Same for a mirror or download failure when you're not on that page.

## Plan

### 1. `frontend/ErrorToast.qml` component

**Effort:** S–M

1. Top-centre rectangle (the bottom belongs to `BottomBar.qml`), fade in/out only (no slide-in), auto-dismiss (~5 s; ~8 s for errors the driver should act on).
2. API `show(message, severity)`; an internal queue, consecutive-duplicate dedupe, tap to dismiss.
3. Style from `Style.qml` / `Spacing.qml` tokens, text ~1.5× normal size via `dp()`.
4. Instantiate once in `Main.qml` as the last child of the root, so it paints over everything; give it an `objectName` so the dev command server / tests can find it.
5. Add a `tests/fixtures/ErrorToastSmoke.qml` for `tst_qmlfixtures` (show, dedupe, auto-dismiss).

### 2. OBD lost mid-drive (the first and most important wiring)

**Effort:** S–M

Signals: `obdManager.connectionStatusChanged(status)` with the reason in `obdManager.connectionDetail` (`src/managers/obdmanager.h:44,47,177`); the worker's `connectionLost(reason)` reaches the manager as `onWorkerConnectionLost`, which emits `"Disconnected"` plus the reason (`obdmanager.cpp:~1051`).

1. In a `Connections { target: obdManager }` block in `Main.qml`, show the toast when the status goes from `"Connected"` to `"Disconnected"`.
2. Only on an *unexpected* loss: skip user-initiated disconnects, diagnostic-mode enter/exit and app shutdown. If QML can't tell these apart, add a C++ signal such as `connectionLostUnexpectedly(reason)` to `OBDManager` emitted from `onWorkerConnectionLost`.
3. Skip it when the current page already shows OBD status (`stackView.currentItem.objectName` is `"obdMenu"` or the OBD settings page).
4. Check with `./build/octave --dev --profile`: on the media page, unplug the simulated adapter with the `sim_link` command (`{"up": false}`) or `sim_dropout_now` (`src/dev/devruntime.cpp:204-206`).

### 3. Phone mirror and download failures off-page

**Effort:** S

1. `phoneMirrorManager.scrcpyError(message)`: toast only when `PhoneMirrorView` isn't the current page, and only for unexpected disconnects (reuse the view's `deviceErrorPattern` filtering, not every error).
2. `downloadManager.downloadError(songId, displayName, errorMsg)`: toast only when `DownloadPage` isn't current and the download was user-started.

## Open decisions

1. **Audible chime** for the OBD-lost toast: yes/no, and how to avoid clashing with music.
2. **Severity tiers:** probably just one (error) to start.
3. **Top-centre vs top-right** placement on the 1280x800 Pi screen.

Delete this file when the toast and steps 2–3 have landed.
