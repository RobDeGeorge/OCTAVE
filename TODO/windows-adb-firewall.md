# Windows adb firewall rule

**Status:** deferred (found in 0.9.5 RC testing on 2026-10-17; does not block a release)
**Last updated:** 2026-10-18

**Goal:** Stop the Windows firewall prompt on the first QR pairing.

The other 0.9.5 RC follow-ups have moved out of this file. The phone-mirror items (USB-reset reattach, dead Wi-Fi transport, reattach keyed on the phone, the "no adb" hint) are now in `TODO/phone-mirror-robustness.md`. The Windows first-launch stall is in `TODO/qml-precompile.md`.

## Windows Firewall prompt for adb on the first QR pairing

The first QR pairing on Windows shows "Do you want to allow public and private networks to access this app? adb, Publisher: Unknown". mDNS pairing opens a listening socket, and adb.exe is unsigned.

Steps:
1. In `build_scripts/installer_windows.iss`, add a `[Run]` entry next to the launch entry (line 64-65): `netsh advfirewall firewall add rule name="OCTAVE adb" dir=in action=allow program="{app}\platform-tools\adb.exe" profile=private` (`Flags: runhidden`).
2. Add an `[UninstallRun]` section that runs `netsh advfirewall firewall delete rule name="OCTAVE adb"`.
3. This only works for all-users (admin) installs. A per-user install keeps the prompt, which is acceptable.

**Effort:** S. **Needs:** the kora Win11 VM (an interactive install, then QR pairing with no prompt; uninstall, then confirm the rule is gone) and an Android phone for the pairing.

The ESP32 port auto-select item from this file was fixed on 2026-10-18 (only an ESP32-S3 is auto-selected; other ports get no writes until the knob sends a command).

Delete this file when the firewall rule is done.
