# In-app import for the YouTube cookies file

**Status:** deferred (post-v0.9)
**Last updated:** 2026-10-18

**Goal:** Let users import, check, test and clear their YouTube cookies file from the app, instead of putting a file with an exact name in Downloads by hand.

## Where it stands

`DownloadManager` already passes `--cookies` to yt-dlp when `<DownloadLocation>/youtube_cookies.txt` exists (`_getYoutubeCookiesPath()`, `src/managers/downloadmanager.cpp:312`; used at `:924-937`). The instructions are in README.md (~line 239) and `wiki/download-manager.html#bot-wall`. What's missing is UI: the user has to know the exact filename and folder, gets no confirmation the file was found, and extensions export it as `cookies.txt` or `youtube.com_cookies.txt`, so it has to be renamed.

## Steps

1. **C++ (`src/managers/downloadmanager.{h,cpp}`):**
   - `Q_INVOKABLE bool importCookiesFile(const QString &fileOrUrl)`. Copy the same pattern as `DashboardManager::importDashboard()` (`src/managers/dashboardmanager.cpp:496-511`): `QUrl::isLocalFile()` gives a local path, and anything else (Android `content://`) goes to `QFile` as-is, since Qt's Android file engine opens content URIs. Read it (size cap, e.g. 1 MB), check for a Netscape header or tab-separated lines with a `.youtube.com` domain, then write it to `_getYoutubeCookiesPath()`. Copy it rather than keeping a reference, because yt-dlp can't read a content URI.
   - `Q_PROPERTY bool cookiesPresent` / `QDateTime cookiesModified` (file mtime), refreshed after import or clear.
   - `Q_INVOKABLE void clearCookies()` (deletes the file).
   - `Q_INVOKABLE void testCookies()` runs `yt-dlp --cookies <path> --simulate <known-walled-videoId>` on the existing yt-dlp process path, and emits `cookiesTestFinished(bool ok, QString message)`.
2. **QML:** add a "YouTube cookies" section to the download UI. There is no download settings page today: either add a card to `frontend/settings/MediaSettingsPage.qml` or put it in `frontend/DownloadPage.qml`.
   - Status row: "Cookies imported (updated <date>)" or "No YouTube cookies imported".
   - "Import cookies file" opens a `QtQuick.Dialogs` `FileDialog`, exactly like the dashboard import in `frontend/OBDMenu.qml:189-194` (`onAccepted: downloadManager.importCookiesFile(selectedFile.toString())`, `nameFilters: ["Cookies (*.txt)", "All files (*)"]`). On Android this is the system picker, so no JNI or Java is needed.
   - Buttons for Test (with a result line) and Clear.
   - A "How to export cookies" help popup (the steps from the README).
3. Wrap all of it in `#ifdef`/`OCTAVE_ENABLE_DOWNLOADS` like the rest of the downloads feature. App-store builds have no `downloadManager`.
4. Add a QtTest in `tests/cpp/` for `importCookiesFile` (valid file, junk file, too big), using a temp download location.
5. Wiki: `download-manager.html` (cookies section), `signals-slots-reference.html`.

**Effort:** M (about 2–3 h). **Needs:** desktop for the main pass, plus the Galaxy S22 to confirm the Android picker's `content://` copy and that yt-dlp then uses the cookies.

Related: `TODO/android-cpp-port.md`. Delete this file when done.
