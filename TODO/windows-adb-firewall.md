# Windows adb firewall rule

**Status:** deferred (found in 0.9.5 RC testing on 2026-10-17; does not block a release)
**Last updated:** 2026-11-11

**Goal:** Stop the Windows firewall prompt on the first QR pairing.

The other 0.9.5 RC follow-ups have moved out of this file. The phone-mirror items (USB-reset reattach, dead Wi-Fi transport, reattach keyed on the phone, the "no adb" hint) are now in `TODO/phone-mirror-robustness.md`. The Windows first-launch stall is in `TODO/qml-precompile.md`.

## Windows Firewall prompt for adb on the first QR pairing

The first QR pairing on Windows shows "Do you want to allow public and private networks to access this app? adb, Publisher: Unknown". mDNS pairing opens a listening socket, and adb.exe is unsigned.

Done in code on 2026-10-18: `build_scripts/installer_windows.iss` deletes and re-adds a private-profile "OCTAVE adb" inbound rule for `{app}\platform-tools\adb.exe` in `[Run]`, and removes it in `[UninstallRun]` (admin installs only). What is left is checking it on Windows:

1. Dispatch CI (or tag an RC) to get a Windows installer; ISCC is the first real compile of the new entries.
2. On the kora Win11 VM: interactive admin install; `netsh advfirewall firewall show rule name="OCTAVE adb"` shows one rule; QR-pair a phone with no prompt.
3. Install again over the top: still one rule. Uninstall: the rule is gone.
4. If the VM's network is classed Public, the prompt can still appear (the rule is private-only on purpose; allowing adb inbound on public Wi-Fi isn't wanted). Note it in the wiki if so.

0.9.6 RC testing (kora win11, a Public network, 2026-11-10/11) ran steps 1 and 2 and the uninstall half of 3 (rc3): the rule is there after install (Inbound, Allow, Private), gone after uninstall, and on a Private network a fresh install shows the QR page with no prompt. Installing over the top (still one rule) was not looked at. Step 4 is as written: on a Public network the prompt appears at the first QR pairing (`wiki/building.html` says so). Two things it found, both changed in 0.9.6-rc5:

- **The prompt appeared at every quit, with no phone feature opened** (rc4, N1). `AndroidAutoManager::cleanup()` ran `adb forward --remove tcp:5277` unconditionally, and any adb command starts the adb server. It now runs only when this run set the forward (`m_dhuForwardSet`); `tst_boot` `startingAndQuittingRunsNoAdbCommand` holds it. Left to check on win11: start, switch to an exec frontend (a quit and a restart), quit: no `adb.exe` process, no prompt.
- **Windows' own rules outlived the uninstall.** Windows adds its own rules for the `adb.exe` path when its prompt is answered or dismissed (kora saw two "adb" Block rules stay after uninstall), and a Block rule wins over "OCTAVE adb" after a reinstall. `[UninstallRun]` now also deletes every inbound rule whose program is `{app}\platform-tools\adb.exe` (admin installs). Left to check on win11: answer the prompt once, uninstall, `netsh advfirewall firewall show rule name=all dir=in | findstr /i adb` shows nothing for the OCTAVE path.

Still Rob's call: widening "OCTAVE adb" to the Public profile (no prompt anywhere, but adb's mDNS listener allowed inbound on public Wi-Fi). Not done.

**Effort:** S (verification only). **Needs:** the kora Win11 VM (an interactive install, then QR pairing with no prompt; uninstall, then confirm the rule is gone) and an Android phone for the pairing.

The ESP32 port auto-select item from this file was fixed on 2026-10-18 (only an ESP32-S3 is auto-selected; other ports get no writes until the knob sends a command).

Delete this file when the firewall rule is done.
