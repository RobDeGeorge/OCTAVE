# Frontend platform: what is left

**Status:** active; the desktop work is done, what is left needs other devices, a release run, or a decision
**Last updated:** 2026-10-23

**Goal:** someone who has never built OCTAVE can download one file, get a simulated car, write a frontend in any language against a published contract that won't break under them, and install it on a head unit where the user picks it as the screen.

## Done (2026-10-23)

All seven steps landed on the desktop: the enforced contract (`tools/api/check_compat.py` in CI, stability and deprecation in `describe`, `docs/api/CHANGELOG.md`); `--sim` and the `sim` object in every build, with CI running each packaged artifact under `--headless --sim --smoke`; `docs/api/protocol.json` with the TypeScript and Go SDKs generated from it and `tools/api/fake_server.py` for SDK authors; installable frontends (`octave-frontend.json`, `FrontendManager`, `FrontendSupervisor`, Settings > Device > Frontends); scopes per paired device (`forbidden`); the `templates/` hello-car projects in JavaScript, Python and Go, each run by ctest against `--headless --sim`; the many-clients harness and the limits per connection (`rateLimited`, ctest `service_load_check`). Where each is documented: `docs/api/README.md`, `wiki/writing-a-frontend.html`, `wiki/utilities.html#service-protocol` and `#many-clients`. The five overnight frontends that proved the API stay on the `frontends-experiment` branch.

## Left

1. **Watch the first release run.** The `--headless --sim --smoke` steps (`packaging/smoke-sim.sh` for the x86_64 and aarch64 AppImages and the macOS app, PowerShell on Windows) have not run on the runners yet; check the first tag or dispatch run and fix what the runners differ in. **Effort:** S. **Device:** CI.
2. **An exec frontend on Windows and macOS.** Only Linux was run. On Windows `terminate()` is WM_CLOSE (a console program may need the `kill()` the supervisor sends after the grace period); on macOS the relaunch uses `applicationFilePath()` inside the `.app`. **Effort:** S. **Device:** Windows, macOS.
3. **An exec frontend as the screen on the Pi:** start-up time, a crash loop and the fall-back to QML, `USE_QML` from the boot volume. **Effort:** M. **Device:** Pi.
4. **Many clients on the Pi**, the slowest head unit: `tools/stream/service_load.py` headless and with `--ui` (all scenarios, `--seconds 30`), its table next to the desktop one in `wiki/utilities.html#many-clients`. Check the limits' defaults against it, above all whether 200 ops a second from one client or 100 new local connections a second still leave the poll rate and the GUI round trip alone. **Effort:** S. **Device:** Pi.
5. **Zip install memory (accepted for now).** `QZipReader` inflates a whole entry in memory; entries whose directory sizes say over 256 MB unpacked or 64 MB packed are refused first, but a zip whose sizes lie can still use a lot of memory before the unpacked check refuses it (nothing is written or run, and installing needs the `system` scope or the head unit's own screen). A bounded inflater would close it; official Qt kits don't export zlib. **Effort:** S. **Device:** desktop.
6. **A sandbox for exec frontends (decision for Rob).** Scopes hold an exec frontend on the service socket, but the program runs as OCTAVE's own user: a hostile one could edit `<config>` (install itself another frontend with `full`, or rewrite `settingsConfigure.json`) or read a sibling's `OCTAVE_TOKEN` from `/proc/<pid>/environ`. Today the docs say "install only frontends you trust". A real fix runs the frontend as another user (systemd `DynamicUser=` or a dedicated uid on the Pi image, the config dir not writable by it) or in a sandbox (bubblewrap / Flatpak portal style); on Windows and macOS an app sandbox. Pick one per platform before installs from strangers are encouraged. **Effort:** L. **Device:** Pi, desktop.
7. **`--pair-code` on Windows and macOS.** The admin channel's `QLocalServer::UserAccessOption` ACL is unverified on Windows (another account must get access denied, non-elevated must work); the peer-uid check on macOS uses `getpeereid`. **Effort:** S. **Device:** Win11 VM, a Mac.

## Order

1 as soon as a release is cut, then 3 and 4 together on the Pi (both want the head unit on the bench), 2 and 7 when a Windows or macOS machine is at hand. 6 is a decision to make before installs from strangers are encouraged; 5 only then too.

Related: the process split in `TODO/open-backend.md` builds on item 4's numbers.

Delete this file and its row in `TODO/README.md` when the last item is done.
