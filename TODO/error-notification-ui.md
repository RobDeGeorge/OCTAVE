# Error Notification UI (Parked)

**Status:** Deferred. OCTAVE currently routes all errors to logs + terminal output only, by design. This document captures the plan for adding an in-app notification layer later, if and when that decision changes.

**Last updated:** 2026-10-10

---

## Why this was deferred

Car infotainment UX is not desktop UX. Anything that flickers into the driver's peripheral vision while they're driving is a distraction risk, and most automotive HMI guidelines discourage chatty notifications. Routing errors to logs keeps the UI clean and puts the errors where the person who actually needs to see them (the developer debugging after the fact) will look for them. Tesla, Toyota, and Ford all do essentially this.

The backend is already set up to support an in-app layer later without any further backend work — see "What's already done" below.

---

## Terminology refresher: toast vs modal

- **Toast** — small transient notification that appears, auto-dismisses after a few seconds, non-blocking. Named after Android's `Toast` class (pops up like toast from a toaster). Material Design calls these "Snackbars"; iOS calls them "banners." Used for FYI messages.
- **Modal** — popup with an explicit "OK" / "Cancel" that blocks the UI until acknowledged. Used only when the user MUST decide something right now.

For car UX, both should be used sparingly. Modals almost never. Toasts only for driver-actionable errors, not chatty info.

---

## The recommended approach (if we ever do this)

Do NOT wire every `errorOccurred` signal from every manager to a toast. That's the desktop-style "show everything" pattern and it's wrong for a car.

Instead, curate a short list of driver-actionable errors — things the driver can and should respond to while seated in the car — and only wire those. Everything else stays in logs.

### Curated driver-actionable error list

Target: ~4–6 errors total. Initial list:

1. **OBD adapter disconnected mid-trip** — driver might want to know their vehicle diagnostics just went offline
2. **Phone mirror disconnected unexpectedly** — driver may want to reconnect
3. **Download failed** (only if the user just initiated one — not background retries)
4. **Update available** — informational, but driver-relevant

Everything else — the ~100 `logger.error(...)` call sites we added in Phase 2 — stays logs-only.

---

## Implementation sketch

### New QML components

1. **`frontend/ErrorToast.qml`** — top-anchored rectangle, auto-dismiss timer, severity levels (`"error"` / `"warning"` / `"info"`), internal message queue, consecutive-duplicate dedupe. API: `function show(message, severity)`.

   Style it to match `Style.qml` / `Spacing.qml` tokens so it picks up theme colors automatically. Size text larger than typical desktop toasts (automotive readability rule of thumb: ~1.5× desktop).

2. **`frontend/LoadingOverlay.qml`** (optional — not strictly part of error UI but pairs naturally) — semi-transparent full-screen overlay with `BusyIndicator` + optional status label. `show(text)` / `hide()` slots. Only useful for operations where you want to block the view (OBD diagnostic mode entry, library scan).

### Wiring

Instantiate both components **once** in `Main.qml` as the last children of the root item, so they paint on top of everything. Connect the curated list via `Connections` blocks in the same file:

```qml
Connections {
    target: phoneMirrorManager
    function onScrcpyError(msg) {
        // Filter to unexpected disconnects only, skip the noisy ones
        errorToast.show(msg, "error")
    }
}
```

This is the "global error bus" approach — all wiring in one place in `Main.qml`, easy to audit, easy to reason about. Alternative is per-view `Connections`, which fragments the wiring across the codebase.

### Automotive UX decisions

- **Position:** top-center, not bottom. The bottom has `BottomBar.qml` with persistent volume/navigation controls and we don't want errors fighting with those.
- **Duration:** ~5 seconds default, longer for errors that need driver action (~8s).
- **Font size:** 1.5× typical desktop toast (use `App.Spacing.dp()` so it scales with screen).
- **Audible cue:** consider a subtle chime for high-severity errors. Most car software does this.
- **Do NOT** animate slide-in from the side — it's a known distraction. Fade in/out only.

---

## What's already done (Phase 2 legacy)

The backend side is already wired up. Every silent-failure fix from Phase 2 emits through an `errorOccurred` signal on the relevant manager, with rate-limiting where appropriate. These signals currently fire into the void (nothing listens), but the plumbing exists:

| Manager | Signal | Notes |
|---|---|---|
| `obd_manager` | `connectionError(str)` | Already fires on connection failures. |
| `download_manager` | `downloadError(song_id, name, msg)` | Per-download; needs the song name to be useful in a toast. |
| `phone_mirror.manager` | `scrcpyError(str)` | Fires on scrcpy start/stop failures. |
| `android_auto.manager` | `error(str)` | Fires on DHU process errors. |
| `network_manager` | `updateStatusChanged(str)` | Not strictly an error — but used for update notifications. |

To enable the full toast UI, no backend changes needed. Just build the two QML components and wire the `Connections` blocks in `Main.qml`.

---

## Open decisions (for whenever this resumes)

1. **Full curated list or smaller subset?** Start with 2–3 errors (OBD disconnect + phone mirror disconnect + download failed) and expand based on user feedback?
2. **Toast position:** top-center vs top-right? Top-center is more noticeable but covers more screen real estate.
3. **Persistent error indicator:** should there be a small badge somewhere (e.g. on `BottomBar`) that shows "there are recent errors in the log" so the driver can check later without interrupting driving?
4. **Audible chime:** yes/no? If yes, how to avoid it conflicting with music playback?
5. **Severity levels:** do we need three tiers (info/warning/error) or just one (error)?
6. **Dismiss gesture:** auto-dismiss only, or allow swipe-to-dismiss? Swipe adds complexity but gives driver control.

---

## Why not just rip out the `errorOccurred` emissions since nothing listens?

They cost nothing. Qt signals with no connected slots compile to a check "any listeners?" → no → return. No allocation, no queue, no CPU. Meanwhile they preserve optionality: if we ever want in-app notifications, we don't have to go back through every error path in the backend and re-thread signal emission.

The "dead code is dead code" principle applies when dead code costs maintenance, confusion, or test burden. These emissions cost zero on all three fronts, and the intent is obvious (there's a `logger.error(...)` right above every emission). Leave them.
