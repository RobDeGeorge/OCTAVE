# Launch the current source from the desktop

On Rhea's Hyprland desktop, press **Mod+D (Super+D)**, search **OCTAVE**, and select **OCTAVE**.

The desktop entry at `~/.local/share/applications/octave-dropbox.desktop` runs `scripts/octave-desktop.sh` in `/home/rhea/Dropbox/Oasis/_OCTAVE`. Every launch checks the native C++ build and compiles changed files before opening the app. QML and assets load directly from `frontend/`. Saved, uncommitted changes are included; no Git commit or package install is needed.

On Zion, the equivalent entry is `~/.local/share/applications/octave-source.desktop`, shown as **OCTAVE (Source)**.

The first launch builds the app. Later launches use an incremental build. A desktop notification indicates the source check; a failed build reports the log location and does not start an older binary. Simultaneous launcher clicks share a build lock.

Build output is machine-specific: `build-desktop-$(uname -n)/`. This avoids reusing the CMake cache synced from another computer. Launch and application output goes to `~/.local/state/octave/desktop-launch.log` (or `$XDG_STATE_HOME/octave/desktop-launch.log`).

This entry runs the C++ app. Close the existing app before relaunching to test a fresh session.

## First-run testing: OCTAVE (Fresh)

**OCTAVE (Fresh)** (`~/.local/share/applications/octave-fresh.desktop`) runs the same script with `OCTAVE_FRESH=1`. The build is identical; the app then starts with `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME` and `XDG_STATE_HOME` pointed at a new empty `/tmp/octave-fresh.*` folder, so every launch is a brand-new user's first run: no settings, no music library, no trip history, no logs. The folder is deleted when the app exits. The launch log still goes to the real `~/.local/state/octave/desktop-launch.log` (its first line names the throwaway folder). From a terminal: `OCTAVE_FRESH=1 scripts/octave-desktop.sh`.

On Linux, the app disables GPU acceleration inside the embedded wiki browser before WebEngine initializes. This avoids a Mesa crash during Chromium's Wayland/GBM-to-Vulkan fallback. Qt Quick keeps its normal hardware rendering. Existing `QTWEBENGINE_CHROMIUM_FLAGS` are preserved.
