# Windows adb firewall rule and ESP32 serial-port auto-select

**Status:** deferred (found in 0.9.5 RC testing on 2026-10-17; neither blocks a release)
**Last updated:** 2026-10-18

**Goal:** Stop the Windows firewall prompt on the first QR pairing, and stop the volume-knob manager writing to whatever serial port it finds first.

The other 0.9.5 RC follow-ups have moved out of this file. The phone-mirror items (USB-reset reattach, dead Wi-Fi transport, reattach keyed on the phone, the "no adb" hint) are now in `TODO/phone-mirror-robustness.md`. The Windows first-launch stall is in `TODO/qml-precompile.md`.

## 1. ESP32 knob auto-connects to the first serial port (do this first)

With no port saved (`esp32VolumePort` = "" and `esp32VolumeEnabled` defaulting to true, `settingsmanager.cpp:244`), `ESP32VolumeManager` falls back to the first serial port that isn't the OBD port (`src/managers/esp32volumemanager.cpp:297-309`). On the Win11 and Pi OS VMs it picked COM1 / ttyS0 and wrote `M0`, `C0,255,255` and `V32` to it. Settings then showed "Volume Knob: Connected" with no knob attached, because the status is set as soon as the port opens (`esp32volumemanager.cpp:398-402`). This can send bytes to unrelated serial hardware.

Steps:
1. Delete the "first available port" fallback (lines 297-309). Keep only the ESP32-S3 VID/PID auto-select (`m_esp32S3Port`, lines 291-296; detected at ~141-172).
2. Report "Connected" only after the knob answers. Add a short handshake timeout, and until then show "Port open, waiting for knob".
3. Add a QtTest in `tests/cpp/` for the "no port saved, no ESP32-S3 present" case (no auto-select, no write). Give it a temp `XDG_CONFIG_HOME`.

**Effort:** S. **Needs:** a VM with a plain serial port (kora Win11 or Pi OS) to confirm nothing is written, and the real ESP32-S3 knob to confirm auto-select and the handshake still work.

## 2. Windows Firewall prompt for adb on the first QR pairing

The first QR pairing on Windows shows "Do you want to allow public and private networks to access this app? adb, Publisher: Unknown". mDNS pairing opens a listening socket, and adb.exe is unsigned.

Steps:
1. In `build_scripts/installer_windows.iss`, add a `[Run]` entry next to the launch entry (line 64-65): `netsh advfirewall firewall add rule name="OCTAVE adb" dir=in action=allow program="{app}\platform-tools\adb.exe" profile=private` (`Flags: runhidden`).
2. Add an `[UninstallRun]` section that runs `netsh advfirewall firewall delete rule name="OCTAVE adb"`.
3. This only works for all-users (admin) installs. A per-user install keeps the prompt, which is acceptable.

**Effort:** S. **Needs:** the kora Win11 VM (an interactive install, then QR pairing with no prompt; uninstall, then confirm the rule is gone) and an Android phone for the pairing.

## Order

1, then 2. Delete this file when both are done.
