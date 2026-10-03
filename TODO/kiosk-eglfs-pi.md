# Run OCTAVE on the Pi without a desktop (eglfs / KMS kiosk)

**Status:** parked (user tabled it 2026-10-02 in favour of an OS upgrade first)
**Last updated:** 2026-10-02

## Why it came up

On 2026-10-02 the Jeep's Orange Pi 5 Plus froze with looping audio and needed a power pull. The QA session on the Pi (foxy-roxy) traced it to a GPU hang in **Xorg's own GL context** (glamor). `gdm-x-session` logged "syncobj wait timeout", "CSI ... CS_EXTRACT" and "MESA: error: Context reset". The kernel and the rest of the system stayed up, so the hardware watchdog never fired. OCTAVE's UI thread then blocked, and pipewire's queue filled (the looping audio).

GPU stack on the Pi at the time: kernel 5.10.0-1012-rockchip, vendor `mali` kbase (bifrost_kbase g18p0, CSF firmware), "panfork" Mesa 23.0.0-devel (Dec 2022 snapshot) running over kbase, Xorg 21.1.4 (Ubuntu 22.04). Native GL is 3.3, so the `MESA_GL_VERSION_OVERRIDE=3.3` in the start scripts is a no-op and not the cause.

Running OCTAVE with Qt's `eglfs` platform plugin (`QT_QPA_PLATFORM=eglfs`, KMS/GBM) removes Xorg and glamor entirely. That component is the one that hung. It also saves memory and boot time.

## Why it's parked

The user prefers to upgrade the Pi's OS first, to a current kernel with mainline `panthor` and Mesa 24/25. That may fix the hang on its own, and eglfs on the new stack is then a smaller, cleaner change. Re-evaluate after the upgrade: if Xorg/GPU hangs persist, do this.

## What it involves

- A systemd unit (or an autologin TTY) that runs `octave` with `QT_QPA_PLATFORM=eglfs`, `QT_QPA_EGLFS_KMS_CONFIG` pointing to a JSON that selects the HDMI/DSI connector, and the `seat`/`video`/`input`/`render` groups. Drop the i3 `exec` lines and keep i3 installable as a fallback.
- Input: eglfs reads evdev/libinput directly; check that the touchscreen maps correctly (`QT_QPA_EGLFS_ROTATION`, `QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS`).
- Things that assume X11 and need checking: phone mirror (`VideoOutput` is fine), Android Auto DHU embedding (Python/Windows only, n/a), any `xdotool`/`DISPLAY` use in scripts (`scripts/octave-start.sh` exports `DISPLAY=:0`), and the screenshot tooling (already uses grabWindow).
- QML popups and multiple windows: eglfs allows one fullscreen window. NowPlayingStudio and the popups are in-window, so they should be fine; verify.
- `scripts/octave-update.sh --apply` must still run before launch (ExecStartPre).

## Order

1. Finish the OS upgrade (separate effort) and re-test for GPU hangs.
2. If hangs persist, or for boot-time/RAM gains, prototype eglfs on the Pi with i3 left installed.
3. Document the setup in `wiki/hardware-setup.html`.

Related: `TODO/sensor-driven-ui-cost.md` (idle GPU/CPU cost on the Pi).

Delete this file when done.
