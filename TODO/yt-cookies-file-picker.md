**Status:** deferred (post-v0.9)
**Last updated:** 2026-10-18

# In-app file picker for YouTube cookies

## Background

As of 2026, YouTube's bot detection (`Sign in to confirm you're not a bot`) walls off most yt-dlp downloads on a fresh install. The reliable bypass is to pass a Netscape-format `cookies.txt` exported from a logged-in YouTube session via `yt-dlp --cookies <file>`.

v0.9 ships a **zero-UI auto-detection** of this file at a single canonical path:

| Platform | Path |
|----------|------|
| Android  | `/storage/emulated/0/Download/youtube_cookies.txt` |
| Linux    | `~/Downloads/youtube_cookies.txt` |
| macOS    | `~/Downloads/youtube_cookies.txt` |
| Windows  | `%USERPROFILE%/Downloads/youtube_cookies.txt` |

This is `QStandardPaths::DownloadLocation + "/youtube_cookies.txt"` — universal, user-accessible, no permissions needed beyond what's already granted for downloaded music.

If the file is present, `DownloadManager` appends `--cookies <path>` to every yt-dlp invocation. If absent, downloads proceed without it (and most will hit the bot wall).

User instructions for v0.9 (to be added to README):
1. Install **Get cookies.txt LOCALLY** in any Chromium browser (Chrome / Brave / Kiwi on Android).
2. Log into <https://youtube.com> in that browser.
3. Click the extension → **Export → cookies.txt**.
4. Save it as `youtube_cookies.txt` in the **Downloads** folder.
5. Restart OCTAVE — downloads will use the cookies on every attempt.

## Why a follow-up TODO

The auto-detect approach gets us 100% of the *functional* value with zero new UI surface, but the UX has rough edges:

- User has to know the exact filename (`youtube_cookies.txt`) and exact location.
- No in-app confirmation that the file was found.
- No visible status when the file is present-but-malformed (e.g. cookies expired).
- Renaming the exported file is a manual step — extensions usually export it as `cookies.txt` or `youtube.com_cookies.txt`.

## What "done" looks like

A new section in `frontend/settings/DownloadSettingsPage.qml` (or wherever download settings live) with:

1. **Status row** — shows `Cookies imported (Last refreshed: <date>)` in green when present, or `No YouTube cookies imported` in muted text when absent.
2. **"Import cookies file" button** — opens an Android Storage Access Framework file picker (`ACTION_OPEN_DOCUMENT`, `application/octet-stream` or `*/*`). On Linux/macOS/Windows: native `QFileDialog`.
3. On pick, the file is **copied** (not just referenced — Storage Access Framework URIs aren't readable to subprocesses) into the canonical path: `<DownloadLocation>/youtube_cookies.txt`. The current implementation auto-picks it up from there.
4. **"Test cookies" button** — runs a quick `yt-dlp --cookies <path> --simulate <known-walled-videoId>` to confirm the cookies actually bypass the wall. Display result in a status line.
5. **"Clear cookies" button** — deletes the file. Useful if cookies expired and the user wants to re-import.
6. **"How to export cookies" link** — pops a help dialog with step-by-step extension instructions and a one-line Chromium extension store search hint.

## Where the code lives

- C++: `src/managers/downloadmanager.{h,cpp}` — `_getYoutubeCookiesPath()` already in place. Add `Q_INVOKABLE void importCookiesFile()` that triggers the SAF picker via JNI on Android, native `QFileDialog::getOpenFileName` elsewhere.
- Android: `android/src/org/octave/app/OctaveMediaBridge.java` — add a static `pickCookiesFile(Activity)` helper using `QtAndroidPrivate::startActivity` with a result callback that copies the picked file to `<DownloadLocation>/youtube_cookies.txt`.
- QML: `frontend/settings/DownloadSettingsPage.qml` (or equivalent) — UI section described above.

## Cross-references

- `TODO/android-cpp-port.md` — broad Android feature work; this fits in there.

## Estimated effort

~2-3 hours focused work (file picker JNI + QML UI + cookies validity check + help dialog).

## Delete this file when done.
