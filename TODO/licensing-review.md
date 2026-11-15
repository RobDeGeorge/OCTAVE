# Licensing review: make the notices match what ships

**Goal:** every bundled or linked third-party component is named in `NOTICE` under the right license, the project's own license statement is true for every build, and the one real decision (shipping the FairPlay handshake) is made on purpose.

**Status:** parked. Found by the 2026-11-15 codebase review; the owner chose to deal with it another day.

**Last updated:** 2026-11-15

**Why parked:** none of this blocks the code clean-up, and one item is a risk decision for the owner rather than an edit. Nothing here is legal advice; where an item says "check", a person has to read the license text.

## What the review found

| # | Finding | Where |
|---|---|---|
| 1 | The project says GPL-3.0-or-later, but `playfair` is GPL-3.0 with no "or later". A build that contains AirPlay may not be offerable under a later GPL. | `NOTICE:9` and `NOTICE:31`, `third_party/uxplay/README.md:11`, `third_party/uxplay/playfair/LICENSE.md` |
| 2 | `playfair` is a reverse-engineered FairPlay handshake. A license notice does not address that; it is a decision about what the release binaries contain. | `third_party/uxplay/playfair/`, `OCTAVE_HAVE_AIRPLAY` in `CMakeLists.txt` |
| 3 | `README.md` and `NOTICE` describe UxPlay differently ("LGPL 2.1+, with GPL 3.0 and MIT parts" against "the whole GPL-3.0"). | `README.md:346`, `NOTICE:29-32` |
| 4 | Google Sans is declared OFL-1.1. Confirm the bundled TTF is the 2025 open release and not the older proprietary font of the same name. | `NOTICE:37-39`, `frontend/assets/fonts/` |
| 5 | The vendor STEP models have no recorded redistribution terms and are not in `NOTICE`. | `hardware/obd-adapter/lib/3dmodels/` (6 files) |
| 6 | `licenses/` has no BSD-3-Clause text, but `cmake/FindWrapOpenGL.cmake` is BSD-3-Clause (Qt's file, modified). | `cmake/FindWrapOpenGL.cmake:1-2`, `licenses/` |
| 7 | Apache-2.0 section 4(b) asks for a notice in each changed file. Most changed scrcpy files carry an `OCTAVE:` comment, not all (the review named the `Completion` rewrite in `Server.java`). | `phone_server/src/main/java/org/octave/phoneserver/` |
| 8 | The aarch64 AppImage bundles Ubuntu's GPL FFmpeg 4.4 with x264 / x265. `NOTICE` discloses it; the source-offer wording should be re-read. | `NOTICE:56-62` |

Already dealt with on 2026-11-15: the leftover `android/assets/python-packages/ytmusic-bundle.zip` (third-party code that was in no notice) was deleted.

## Steps, easiest first

1. **Align the UxPlay wording.** Make `README.md:346` and `NOTICE:29-32` say the same thing. **Effort:** S. **Needs:** desktop.
2. **Add the BSD-3-Clause text.** `licenses/BSD-3-Clause.txt`, a `NOTICE` line for `cmake/FindWrapOpenGL.cmake`; check `tst_licensetexts` and the in-app licenses screen pick it up. **Effort:** S. **Needs:** desktop.
3. **Check the Google Sans file.** Read the license and version records in the TTF's name table (`fontTools`) and compare with the googlefonts/googlesans release. If it is the old font, replace the file. **Effort:** S. **Needs:** desktop.
4. **Mark the remaining changed scrcpy files.** `git diff` the fork against scrcpy v3.3.4 (`git log -- phone_server`), add an `OCTAVE:` comment to every changed file that lacks one. Comment-only, but the jar is rebuilt from source, so rebuild and commit it together (`phone_server/build_without_gradle.sh`). **Effort:** S. **Needs:** desktop.
5. **STEP models.** Find each vendor's terms (LCSC / the part maker's download page). Where redistribution is not allowed, remove the file and document where to download it in `hardware/obd-adapter/README.md`. **Effort:** S to M. **Needs:** desktop.
6. **Decide the license statement for AirPlay builds (finding 1).** Options: state in `NOTICE` that binaries containing AirPlay are GPL-3.0-only while the OCTAVE source stays GPL-3.0-or-later; or make the whole project GPL-3.0-only. Owner's choice; then edit `NOTICE`, `README.md` and the About page text. **Effort:** S once decided. **Needs:** the owner.
7. **Decide whether release binaries contain `playfair` (finding 2).** The cautious option is to build releases without AirPlay (`OCTAVE_HAVE_AIRPLAY` off in the CI packagers) and leave it as a build-it-yourself option; the other is to keep shipping it knowingly. Either way, write the decision into `NOTICE` and `TODO/iphone-airplay.md`. **Effort:** S to act on, the decision is the work. **Needs:** the owner.
8. **Re-read the aarch64 FFmpeg source offer (finding 8).** This goes away if `TODO/arm64-qt-ffmpeg-lgpl.md` is done first. **Effort:** S. **Needs:** desktop.

## Cross-references

- `TODO/arm64-qt-ffmpeg-lgpl.md`: removes the GPL FFmpeg from the aarch64 AppImage, which closes finding 8.
- `TODO/iphone-airplay.md`: the AirPlay feature itself; steps 6 and 7 decide how it ships.
- `TODO/android-cpp-port.md`: its Play Store section needs the license statement settled (step 6) before a store listing.
- Not a license question but found by the same review: the prebuilt OpenSSL 3.1.8 in `android/openssl/` has no recorded hash and is on an out-of-support branch.

## Order

1 to 5 in any order (all desktop, all small), then 6 and 7 together since both are about AirPlay, then 8 unless the arm64 plan has landed.

Delete this file (and its README row) when every step is done.
