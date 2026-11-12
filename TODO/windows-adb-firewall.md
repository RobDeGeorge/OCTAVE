# Windows adb firewall rule

**Status:** deferred; two checks on the Win11 VM left (the rule shipped in 0.9.6 and was widened for 0.9.7)
**Last updated:** 2026-11-12

**Goal:** No Windows firewall prompt on the first QR pairing.

`build_scripts/installer_windows.iss` adds an "OCTAVE adb" inbound rule for `{app}\platform-tools\adb.exe` (`[Run]`: every inbound rule for that program is deleted, then the rule is added; `[UninstallRun]` removes them all; admin installs only). 0.9.6 shipped it for the Private profile, and its release testing on the kora Win11 VM confirmed that much: the rule is there after an install (Inbound, Allow, Private), a fresh install on a Private network shows the QR page with no prompt, the uninstall removes it and Windows' own "adb" rules (a before / after comparison of all 495 rules showed nothing else), and no adb starts, so no prompt appears, until a phone feature is opened (`tst_boot` `startingAndQuittingRunsNoAdbCommand`). `wiki/building.html` describes all of it.

Rob's decision on 2026-11-12: the prompt should not appear on a Public network either. The rule is now `profile=private,public`, and an install first deletes the rules Windows made when its prompt was answered under 0.9.6 (Cancel leaves Block rules, which win over an Allow). The cost: adb's mDNS listener is reachable from other devices on public Wi-Fi. A domain network keeps the prompt.

## Left

Both on the Win11 VM, with the first build that has the change (its network is Public, which is the case to check). **Effort:** S each.

1. **The widened rule works.** After an install: `netsh advfirewall firewall show rule name="OCTAVE adb"` shows one rule, Inbound, Allow, Profiles Private,Public; opening the QR pairing page shows no firewall prompt.
2. **An install over 0.9.6 cleans up.** Install 0.9.6, open the QR page on the Public network, answer the prompt with Cancel (two Block rules for `adb.exe` appear), install the new build over it: the Block rules are gone, there is exactly one "OCTAVE adb" rule, and the QR page shows no prompt. Install once more: still one rule.

Related: a phone-mirror item list is in `TODO/phone-mirror-robustness.md`; what the Windows first-launch stall turned out to be (Windows busy in its first minutes, not OCTAVE) is in `TODO/qml-precompile.md`.

Delete this file and its row in `TODO/README.md` when both are checked.
