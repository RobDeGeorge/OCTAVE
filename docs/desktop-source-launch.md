# Launch the current source from the desktop

On Rhea's Hyprland desktop, press **Mod+D (Super+D)**, search **OCTAVE**, and select **OCTAVE**.

The desktop entry at `~/.local/share/applications/octave-dropbox.desktop` runs `scripts/octave-desktop.sh` in `/home/rhea/Dropbox/Oasis/_OCTAVE`. Every launch checks the native C++ build and compiles changed files before opening the app. QML and assets load directly from `frontend/`. Saved, uncommitted changes are included; no Git commit or package install is needed.

On Zion, the equivalent entry is `~/.local/share/applications/octave-source.desktop`, shown as **OCTAVE (Source)**.

The first launch builds the app. Later launches use an incremental build. A desktop notification indicates the source check; a failed build reports the log location and does not start an older binary. Simultaneous launcher clicks share a build lock.

Build output is machine-specific: `build-desktop-$(uname -n)/`. This avoids reusing the CMake cache synced from another computer. Launch and application output goes to `~/.local/state/octave/desktop-launch.log` (or `$XDG_STATE_HOME/octave/desktop-launch.log`).

This entry runs the **native C++ backend**. To test changes specifically to the alternative Python backend, run `venv/bin/python main.py` from the checkout. Close the existing app before relaunching to test a fresh session.

On Linux, both backends disable GPU acceleration inside the embedded wiki browser before WebEngine initializes. This avoids a Mesa crash during Chromium's Wayland/GBM-to-Vulkan fallback. Qt Quick keeps its normal hardware rendering. Existing `QTWEBENGINE_CHROMIUM_FLAGS` are preserved.

The native downloader also finds `venv/bin/yt-dlp` in the checkout (Windows: `venv/Scripts/yt-dlp.exe`) before checking system installations. You do not need to activate the Python environment in a terminal before using search or downloads from the desktop launch.
