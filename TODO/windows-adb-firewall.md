# Windows adb firewall rule

**Status:** deferred; one check and one decision left (the rule shipped in 0.9.6)
**Last updated:** 2026-11-11

**Goal:** No Windows firewall prompt on the first QR pairing.

`build_scripts/installer_windows.iss` adds a private-profile "OCTAVE adb" inbound rule for `{app}\platform-tools\adb.exe` (deleted and re-added in `[Run]`, removed in `[UninstallRun]` together with the rules Windows itself made for that path; admin installs only). 0.9.6's release testing on the kora Win11 VM confirmed it: the rule is there after an install (Inbound, Allow, Private), a fresh install on a Private network shows the QR page with no prompt, the uninstall removes it and Windows' own "adb" rules (a before / after comparison of all 495 rules showed nothing else), and no adb starts, so no prompt appears, until a phone feature is opened (`tst_boot` `startingAndQuittingRunsNoAdbCommand`). `wiki/building.html` describes all of it.

## Left

1. **One rule after an install over the top.** The script deletes before it adds, so there should be exactly one; nobody counted. On the Win11 VM: install, install again, `netsh advfirewall firewall show rule name="OCTAVE adb"` shows one rule. Put it in the next release's Windows pass. **Effort:** S. **Device:** Win11 VM.
2. **Decision for Rob: the Public profile.** On a network Windows classes as Public the prompt still appears at the first QR pairing (the release notes and the wiki say so). Widening the rule to `profile=private,public` ends the prompt everywhere, and allows adb's mDNS listener inbound on public Wi-Fi. Not done; the default stays private-only. **Effort:** S. **Device:** Win11 VM, a phone.

Related: a phone-mirror item list is in `TODO/phone-mirror-robustness.md`; what the Windows first-launch stall turned out to be (Windows busy in its first minutes, not OCTAVE) is in `TODO/qml-precompile.md`.

Delete this file and its row in `TODO/README.md` when 1 is checked and 2 is decided.
