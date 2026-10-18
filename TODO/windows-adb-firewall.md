# Windows adb firewall rule

**Status:** deferred (found in 0.9.5 RC testing on 2026-10-17; does not block a release)
**Last updated:** 2026-10-18

**Goal:** Stop the Windows firewall prompt on the first QR pairing.

The other 0.9.5 RC follow-ups have moved out of this file. The phone-mirror items (USB-reset reattach, dead Wi-Fi transport, reattach keyed on the phone, the "no adb" hint) are now in `TODO/phone-mirror-robustness.md`. The Windows first-launch stall is in `TODO/qml-precompile.md`.

## Windows Firewall prompt for adb on the first QR pairing

The first QR pairing on Windows shows "Do you want to allow public and private networks to access this app? adb, Publisher: Unknown". mDNS pairing opens a listening socket, and adb.exe is unsigned.

Done in code on 2026-10-18: `build_scripts/installer_windows.iss` deletes and re-adds a private-profile "OCTAVE adb" inbound rule for `{app}\platform-tools\adb.exe` in `[Run]`, and removes it in `[UninstallRun]` (admin installs only). What is left is checking it on Windows:

1. Dispatch CI (or tag an RC) to get a Windows installer; ISCC is the first real compile of the new entries.
2. On the kora Win11 VM: interactive admin install; `netsh advfirewall firewall show rule name="OCTAVE adb"` shows one rule; QR-pair a phone with no prompt.
3. Install again over the top: still one rule. Uninstall: the rule is gone.
4. If the VM's network is classed Public, the prompt can still appear (the rule is private-only on purpose; allowing adb inbound on public Wi-Fi isn't wanted). Note it in the wiki if so.

**Effort:** S (verification only). **Needs:** the kora Win11 VM (an interactive install, then QR pairing with no prompt; uninstall, then confirm the rule is gone) and an Android phone for the pairing.

The ESP32 port auto-select item from this file was fixed on 2026-10-18 (only an ESP32-S3 is auto-selected; other ports get no writes until the knob sends a command).

Delete this file when the firewall rule is done.
