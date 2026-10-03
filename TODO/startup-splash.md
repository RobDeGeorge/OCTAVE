# Startup splash (archived)

**Status:** parked. Restore only if the in-car brownouts come back.
**Last updated:** 2026-10-03

## What it was

Commit `48a291d` ("Startup splash: pre-build every page behind an OCTAVE
splash screen") added `frontend/StartupSplash.qml`. It covered the window
while the nav-page pre-build queue ran at startup: the OCTAVE wordmark, a
progress bar per page, and a "Preparing <page>" line. Main.qml got
`startupPreloading`, `finishStartup(why)`, `_pageLabels`, the `startupSplash`
Loader and a 45 s `startupCapTimer`. MediaRoom.qml held the first track's
audio analysis (`_analyzeTrack` / `_heldAnalysisPath`) until the splash was
gone, so the decode and FFT didn't land on top of the Quick3D page builds.
It was QML only, so it covered both backends.

## Why it was archived

It was built because the Orange Pi 5 Plus in the Jeep died 15–38 s after
boot (2026-10-02), and the stacked startup load looked like it was browning
out the 12V-to-5V supply. On 2026-10-03 Rob found the real cause: a badly
soldered power cable. After the re-solder, the Pi agent restarted OCTAVE
three times with the splash skipped at once (pre-builds and audio analysis
overlapping, about the old 0.9.4 load). Peaks were 330% CPU, 100% GPU and
73 °C, with no power gaps, crashes or freezes. The one death seen that day
was a manual power pull. The splash added about 14 s to every Pi startup
for no remaining benefit, so it was removed.

The pre-build pacing from `c8de12c` (settle, then rest 2x each page's
build time, with touch back-off) was kept. Only the splash and the analysis
hold-back were removed.

## Bringing it back

Signs it's needed again: boots dying 15–40 s after start with nothing in
the journal, only a gap in `/var/log/powerwatch.log` on the Pi.

```bash
git show 48a291d:frontend/StartupSplash.qml > frontend/StartupSplash.qml
git diff 48a291d^ 48a291d -- frontend/Main.qml frontend/MediaRoom.qml wiki/frontend-overview.html | git apply
```

Expect conflicts if Main.qml's `pagePrewarmTimer` has changed since then.
Look first at whether it's enough to just hold back the audio analysis
(the MediaRoom part); it's much smaller than the full splash.

Related: `TODO/kiosk-eglfs-pi.md` (GPU-hang evidence from the same day).

Delete this file once the splash is clearly never coming back (e.g. a few
months of clean in-car boots).
