**Status:** parked — the build matrix exists; what remains is the release process itself
**Last updated:** 2026-09-09

# First real release (v0.9.0) and nightly builds

## What is already done (do not redo)

`.github/workflows/build.yml` now has, on every push/PR to `main`: `lint` (ruff, warn-only),
`test` (headless pytest smoke + boot), `phone-server` (jar/source invariants),
`cpp-compile-check` (Linux CMake build + qmllint). On `v*` tags or manual dispatch the full
C++ matrix runs — `cpp-build-windows`, `cpp-build-macos` (macos-14), `cpp-build-linux`
(x86_64 AppImage), `cpp-build-linux-arm64` (aarch64 AppImage for the Orange Pi),
`cpp-build-android` (APK) — and `release` attaches every artifact to one GitHub Release.
Python is dev-only and is not packaged (see CLAUDE.md); the PyInstaller pipeline is gone.
The full-matrix dispatch was green on 2026-09-09 (arm64 needed the GStreamer plugin drop).

## What is left

1. **Cut `v0.9.0`.** Nothing technical blocks it: push the tag, check the release page has all
   five artifacts, write release notes. Decide first whether the phone-mirror hardware tests
   that are still open (cable pull, unplugged start, audio ducking in the car —
   `docs/PHONE_MIRROR_NATIVE_PLAN.md`) are release blockers or notes.
2. **Rolling nightly.** Add a cron (`schedule:`) trigger that runs the matrix and publishes to a
   fixed `nightly` prerelease tag via `softprops/action-gh-release` (`prerelease: true`,
   overwrite). README links "bleeding edge" to `…/releases/tag/nightly`.
3. **Workflow split** (optional, when the file gets unwieldy): `ci.yml` (push/PR checks),
   `release.yml` (tag), `nightly.yml` (cron), with per-target reusable workflows so one
   platform can be re-run alone.
4. **Deferred targets:** iOS `.ipa` (needs an Apple Developer account and signing —
   `TODO/android-signing-keystore.md` covers the Android side), Flatpak, app-store variants
   (`OCTAVE_ENABLE_DOWNLOADS=OFF`). None are needed for v0.9.

## Why it's parked

The remaining work is a decision (when to tag) plus small YAML. It is parked until the
phone-mirror hardware validation is finished so v0.9.0 does not ship with a known in-car
regression.

## Cross-references

- `TODO/android-signing-keystore.md` — release-signed APKs instead of debug-signed
- `TODO/android-openssl-bundling.md` — needed before the APK is store-ready
- `docs/PHONE_MIRROR_NATIVE_PLAN.md` — open hardware tests

Delete this file when v0.9.0 is tagged and the nightly job exists.
