#ifdef OCTAVE_ENABLE_DOWNLOADS

#include "downloadmanager.h"
#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QLoggingCategory>

#ifndef Q_OS_WIN
#include <csignal>
#endif

#ifdef Q_OS_ANDROID
#include "../platform/androidmediabridge.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#endif

#ifndef Q_OS_MOBILE
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/id3v2frame.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/oggfile.h>
#include <taglib/vorbisfile.h>
#include <taglib/opusfile.h>
#include <taglib/xiphcomment.h>
#endif // Q_OS_MOBILE

Q_LOGGING_CATEGORY(lcDownload, "octave.download")

const QStringList DownloadManager::s_audioExtensions = {
    QStringLiteral(".mp3"),
    QStringLiteral(".flac"),
    QStringLiteral(".ogg"),
    QStringLiteral(".opus"),
    QStringLiteral(".m4a"),
    // yt-dlp names YouTube's m4a audio streams with .mp4 extension (same
    // container). Treat as audio for post-download detection + library scan.
    QStringLiteral(".mp4"),
};

// ═══════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &DownloadManager::_onCoverDownloadFinished);

#ifdef Q_OS_MOBILE
    m_mobileProgressPoller = new QTimer(this);
    m_mobileProgressPoller->setInterval(250);
    connect(m_mobileProgressPoller, &QTimer::timeout,
            this, &DownloadManager::_pollMobileProgress);
#endif
}

DownloadManager::~DownloadManager()
{
    cleanup();
}

// ═══════════════════════════════════════════════════════════════════
// Property getters
// ═══════════════════════════════════════════════════════════════════

bool DownloadManager::isSearching() const
{
    return m_isSearching;
}

int DownloadManager::activeDownloads() const
{
    return m_activeDownloads;
}

QString DownloadManager::downloadPath() const
{
    return _getDownloadPath();
}

bool DownloadManager::hasStorageAccess() const
{
#ifdef Q_OS_ANDROID
    return OctaveAndroid::hasAllFilesAccess();
#else
    return true;
#endif
}

bool DownloadManager::requestStorageAccess()
{
#ifdef Q_OS_ANDROID
    return OctaveAndroid::requestAllFilesAccess();
#else
    return true;
#endif
}

// ═══════════════════════════════════════════════════════════════════
// Settings integration
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::connect_settings_manager(QObject *sm)
{
    m_settingsManager = qobject_cast<SettingsManager *>(sm);
    if (m_settingsManager) {
        qCInfo(lcDownload) << "DownloadManager connected to SettingsManager";
    } else {
        qCWarning(lcDownload) << "connect_settings_manager: invalid object passed";
    }
}

QString DownloadManager::_getDownloadPath() const
{
    QString mediaFolder;
#ifdef Q_OS_ANDROID
    // Android 11+: if the user has granted All-Files-Access
    // (MANAGE_EXTERNAL_STORAGE), respect their configured media folder —
    // POSIX writes into /storage/emulated/0/Music/... work, and downloads
    // show up in any file explorer. Otherwise fall back to the app's
    // external files dir, which is scoped-storage-safe but hidden from
    // most file explorers (under Android/data/org.octave.app/files/Music).
    if (OctaveAndroid::hasAllFilesAccess() && m_settingsManager) {
        mediaFolder = m_settingsManager->mediaFolder();
    }
    if (mediaFolder.isEmpty()) {
        mediaFolder = OctaveAndroid::getDownloadsDir();
    }
#endif
    if (mediaFolder.isEmpty() && m_settingsManager) {
        mediaFolder = m_settingsManager->mediaFolder();
    }
    if (mediaFolder.isEmpty()) {
        mediaFolder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (mediaFolder.isEmpty()) {
            mediaFolder = QDir::homePath() + QStringLiteral("/Music");
        }
    }

    // Use target subfolder override if set, otherwise fall back to setting
    QString subfolder;
    if (!m_targetSubfolder.isEmpty()) {
        subfolder = m_targetSubfolder;
    } else if (m_settingsManager) {
        QVariant val = m_settingsManager->get_setting_with_default(
            QStringLiteral("downloadSubfolder"), QStringLiteral("Downloads"));
        subfolder = val.toString();
    } else {
        subfolder = QStringLiteral("Downloads");
    }

    QString downloadPath = QDir(mediaFolder).filePath(subfolder);

    // Validate subfolder to prevent path traversal
    QDir mediaDir(mediaFolder);
    QString canonicalMedia = mediaDir.canonicalPath();
    if (canonicalMedia.isEmpty()) {
        canonicalMedia = QDir::cleanPath(mediaFolder);
    }
    QString cleanDownload = QDir::cleanPath(downloadPath);
    if (!cleanDownload.startsWith(canonicalMedia)) {
        qCWarning(lcDownload) << "Download subfolder escapes media folder, using 'Downloads'";
        downloadPath = QDir(mediaFolder).filePath(QStringLiteral("Downloads"));
    }

    QDir().mkpath(downloadPath);
    return downloadPath;
}

QString DownloadManager::_getDownloadFormat() const
{
#ifdef Q_OS_ANDROID
    return QStringLiteral("m4a");
#else
    if (!m_settingsManager) {
        return QStringLiteral("mp3");
    }
    QVariant val = m_settingsManager->get_setting_with_default(
        QStringLiteral("downloadFormat"), QStringLiteral("mp3"));
    return val.toString();
#endif
}

QString DownloadManager::_getDownloadBitrate() const
{
    if (!m_settingsManager) {
        return QStringLiteral("auto");
    }
    QVariant val = m_settingsManager->get_setting_with_default(
        QStringLiteral("downloadBitrate"), QStringLiteral("auto"));
    return val.toString();
}

// ═══════════════════════════════════════════════════════════════════
// yt-dlp binary discovery
// ═══════════════════════════════════════════════════════════════════

QString DownloadManager::_findYtDlp() const
{
    if (!m_ytDlpPath.isEmpty()) {
        return m_ytDlpPath;
    }

#ifdef Q_OS_ANDROID
    // On Android, yt-dlp runs through the JNI bridge (youtubedl-android AAR).
    // There's no binary to find — return a sentinel path so the caller treats
    // it as "available". The AndroidProcessAdapter ignores the program arg.
    m_ytDlpPath = QStringLiteral("jni://yt-dlp");
    return m_ytDlpPath;
#endif

    // Source launches from a desktop menu do not activate the Python venv.
    // Use the checkout's pinned yt-dlp, just as search uses its Python.
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString venvBin = QStringLiteral("/venv/Scripts/yt-dlp.exe");
#else
    const QString venvBin = QStringLiteral("/venv/bin/yt-dlp");
#endif
    const QStringList sourceCandidates = {
        QDir::cleanPath(appDir + QStringLiteral("/..") + venvBin),
        appDir + venvBin,
    };
    for (const QString &candidate : sourceCandidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            m_ytDlpPath = candidate;
            return m_ytDlpPath;
        }
    }

    // Installed builds and checkouts without a venv can use PATH.
    QString path = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (!path.isEmpty()) {
        m_ytDlpPath = path;
        return m_ytDlpPath;
    }

    // Check common locations
    const QStringList candidates = {
#ifdef Q_OS_WIN
        QCoreApplication::applicationDirPath() + QStringLiteral("/yt-dlp.exe"),
        QDir::homePath() + QStringLiteral("/.local/bin/yt-dlp.exe"),
#else
        QCoreApplication::applicationDirPath() + QStringLiteral("/yt-dlp"),
        QStringLiteral("/usr/local/bin/yt-dlp"),
        QStringLiteral("/usr/bin/yt-dlp"),
        QDir::homePath() + QStringLiteral("/.local/bin/yt-dlp"),
#endif
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            m_ytDlpPath = candidate;
            return m_ytDlpPath;
        }
    }

    qCWarning(lcDownload) << "yt-dlp binary not found";
    return QString();
}

// ═══════════════════════════════════════════════════════════════════
// YouTube cookies bypass
//
// As of 2026 YouTube's bot wall ("Sign in to confirm you're not a bot")
// rejects most yt-dlp client+IP combinations. The reliable bypass is to
// pass a cookies file harvested from a logged-in YouTube session. The
// user exports cookies.txt from a browser (e.g. "Get cookies.txt LOCALLY"
// extension) and drops the file in their Downloads folder.
//
// We auto-detect a single canonical path so there's no setting/UI churn:
//   <DownloadLocation>/youtube_cookies.txt
// On Android: /storage/emulated/0/Download/youtube_cookies.txt
// On Linux:   ~/Downloads/youtube_cookies.txt
// On macOS:   ~/Downloads/youtube_cookies.txt
// On Win:     %USERPROFILE%/Downloads/youtube_cookies.txt
// ═══════════════════════════════════════════════════════════════════

QString DownloadManager::_getYoutubeCookiesPath() const
{
    QString candidate = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                      + QStringLiteral("/youtube_cookies.txt");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
    return QString();
}

// ═══════════════════════════════════════════════════════════════════
// Search
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::search(const QString &query)
{
    QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QString ytDlp = _findYtDlp();
    if (ytDlp.isEmpty()) {
        emit searchError(QStringLiteral("yt-dlp not found. Please install yt-dlp."));
        emit statusMessage(QStringLiteral("yt-dlp not found"));
        return;
    }

    // Kill any running search process
    if (m_searchProcess) {
        m_searchProcess->disconnect();
        m_searchProcess->kill();
        m_searchProcess->waitForFinished(3000);
        m_searchProcess->deleteLater();
        m_searchProcess = nullptr;
    }

    // Reset pagination for new search
    m_lastQuery = trimmed;
    m_searchOffset = 0;
    m_allResults = QJsonArray();
    m_searchBuffer.clear();

    m_isSearching = true;
    emit isSearchingChanged(true);
    emit searchInProgress(true);
    emit statusMessage(QStringLiteral("Searching: ") + trimmed);

    m_searchProcess = new YtDlpProcess(this);
    m_searchProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_searchProcess, QOverload<int, QProcess::ExitStatus>::of(&YtDlpProcess::finished),
            this, &DownloadManager::_onSearchProcessFinished);
    connect(m_searchProcess, &YtDlpProcess::errorOccurred,
            this, &DownloadManager::_onSearchProcessError);

#ifdef Q_OS_ANDROID
    // On Android, run the bundled ytmusic_search.py against the youtubedl-android
    // Python runtime directly. Bypasses the yt-dlp process machinery entirely
    // — the ytmusicapi script returns pre-formatted JSON-per-line in ~1-2s,
    // and the results are curated songs (not reaction videos / podcasts / tab
    // pages the way ytsearch: and music.youtube.com/search do).
    // Discard the process we prepared above; the JNI path doesn't need it.
    m_searchProcess->disconnect();
    m_searchProcess->deleteLater();
    m_searchProcess = nullptr;

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        const QString output = watcher->result();
        watcher->deleteLater();

        m_isSearching = false;
        emit isSearchingChanged(false);
        emit searchInProgress(false);

        if (output.isEmpty()) {
            emit searchError(QStringLiteral("YouTube Music search failed"));
            emit statusMessage(QStringLiteral("Search failed"));
            return;
        }

        m_searchBuffer = output.toUtf8();
        QJsonArray results = _parseSearchOutput(m_searchBuffer);
        m_searchBuffer.clear();
        _markDownloaded(results);
        m_allResults = results;
        m_searchOffset = results.size();

        QJsonDocument doc(m_allResults);
        emit searchResults(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        emit statusMessage(QStringLiteral("Showing %1 result(s)").arg(m_allResults.size()));
    });
    const QString q = trimmed;
    watcher->setFuture(QtConcurrent::run([q]() {
        return OctaveAndroid::runYtMusicSearch(q, 25);
    }));
    qCInfo(lcDownload) << "Starting search (Android ytmusicapi):" << q;
    return;
#endif

    // Use ytmusicapi via helper script for curated YouTube Music results.
    // yt-dlp's ytsearch returns generic YouTube videos (wrong thumbnails,
    // uncurated results); ytmusicapi returns proper song metadata with
    // real album art, matching the Python backend's behavior.
    QString appDir = QCoreApplication::applicationDirPath();
    QDir scriptDir(appDir);
    QString scriptPath;
    // In dev: binary is in build/, scripts/ is one level up
    if (QFile::exists(appDir + QStringLiteral("/../scripts/ytmusic_search.py"))) {
        scriptPath = QDir::cleanPath(appDir + QStringLiteral("/../scripts/ytmusic_search.py"));
    } else if (QFile::exists(appDir + QStringLiteral("/scripts/ytmusic_search.py"))) {
        scriptPath = appDir + QStringLiteral("/scripts/ytmusic_search.py");
    }

    // Find Python with ytmusicapi — prefer venv
    QString pythonPath;
    QString venvPython = QDir::cleanPath(appDir + QStringLiteral("/../venv/bin/python3"));
    if (QFile::exists(venvPython)) {
        pythonPath = venvPython;
    } else {
        pythonPath = QStandardPaths::findExecutable(QStringLiteral("python3"));
    }

    if (scriptPath.isEmpty() || pythonPath.isEmpty()) {
        qCWarning(lcDownload) << "ytmusic_search.py or python3 not found, falling back to yt-dlp";
        // Fallback to yt-dlp generic search
        QStringList args = {
            QStringLiteral("--dump-json"),
            QStringLiteral("--flat-playlist"),
            QStringLiteral("--no-download"),
            QStringLiteral("--no-warnings"),
            QStringLiteral("--ignore-errors"),
            QStringLiteral("ytsearch25:") + trimmed,
        };
        qCInfo(lcDownload) << "Starting search (fallback):" << ytDlp << args;
        m_searchProcess->start(ytDlp, args);
        return;
    }

    QStringList args = { scriptPath, trimmed, QStringLiteral("25") };
    qCInfo(lcDownload) << "Starting search:" << pythonPath << args;
    m_searchProcess->start(pythonPath, args);
}

void DownloadManager::next_search_page()
{
    // yt-dlp flat-playlist search doesn't support offset pagination
    if (!m_lastQuery.isEmpty()) {
        emit statusMessage(QStringLiteral("All results shown"));
    }
}

void DownloadManager::search_more()
{
    next_search_page();
}

void DownloadManager::_onSearchProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)

    auto *process = qobject_cast<YtDlpProcess *>(sender());
    if (!process) {
        return;
    }

    // Accumulate any remaining data
    m_searchBuffer.append(process->readAll());

    if (process == m_searchProcess) {
        m_searchProcess = nullptr;
    }
    process->deleteLater();

    m_isSearching = false;
    emit isSearchingChanged(false);
    emit searchInProgress(false);

    if (exitCode != 0 && m_searchBuffer.isEmpty()) {
        QString errorText = QStringLiteral("Search failed (yt-dlp exit code %1)").arg(exitCode);
        emit searchError(errorText);
        emit statusMessage(errorText);
        return;
    }

    QJsonArray results = _parseSearchOutput(m_searchBuffer);
    m_searchBuffer.clear();

    // Mark which results are already downloaded
    _markDownloaded(results);

    // Replace accumulated results (new search)
    m_allResults = results;
    m_searchOffset = results.size();

    QJsonDocument doc(m_allResults);
    emit searchResults(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    emit statusMessage(QStringLiteral("Showing %1 result(s)").arg(m_allResults.size()));
}

void DownloadManager::_onSearchProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)

    auto *process = qobject_cast<YtDlpProcess *>(sender());
    if (!process) {
        return;
    }

    if (process == m_searchProcess) {
        m_searchProcess = nullptr;
    }
    process->deleteLater();

    m_isSearching = false;
    emit isSearchingChanged(false);
    emit searchInProgress(false);
    emit searchError(QStringLiteral("Failed to start yt-dlp search process"));
    emit statusMessage(QStringLiteral("Search process error"));
}

QJsonArray DownloadManager::_parseSearchOutput(const QByteArray &rawOutput)
{
    QJsonArray results;
    const QList<QByteArray> lines = rawOutput.split('\n');

    for (const QByteArray &line : lines) {
        QByteArray trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(trimmedLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        QJsonObject obj = doc.object();

        QString videoId = obj.value(QStringLiteral("id")).toString();
        if (videoId.isEmpty()) {
            continue;
        }

        QString title = obj.value(QStringLiteral("title")).toString();

        QString channel = obj.value(QStringLiteral("channel")).toString();
        if (channel.isEmpty()) {
            channel = obj.value(QStringLiteral("uploader")).toString();
        }
        // Try additional artist fields returned by YouTube Music extractor.
        if (channel.isEmpty()) {
            channel = obj.value(QStringLiteral("artist")).toString();
        }
        if (channel.isEmpty()) {
            channel = obj.value(QStringLiteral("creator")).toString();
        }

        // Duration in seconds (yt-dlp gives numeric or null)
        int duration = 0;
        QJsonValue durVal = obj.value(QStringLiteral("duration"));
        if (durVal.isDouble()) {
            duration = static_cast<int>(durVal.toDouble());
        }

        // Thumbnail URL — prefer the last (highest resolution) thumbnail
        QString coverUrl;
        QJsonArray thumbnails = obj.value(QStringLiteral("thumbnails")).toArray();
        if (!thumbnails.isEmpty()) {
            coverUrl = thumbnails.last().toObject().value(QStringLiteral("url")).toString();
        }
        if (coverUrl.isEmpty()) {
            coverUrl = obj.value(QStringLiteral("thumbnail")).toString();
        }
        // Request high-res from Google's CDN if applicable
        if (coverUrl.contains(QStringLiteral("lh3.googleusercontent.com"))) {
            static const QRegularExpression resCropRe(QStringLiteral("=w\\d+-h\\d+.*$"));
            coverUrl.replace(resCropRe, QStringLiteral("=w800-h800-l90-rj"));
        }
#ifdef Q_OS_ANDROID
        // Qt's OpenSSL backend isn't shipped in our APK (see ANDROID_BUILD_SETUP.md
        // OpenSSL deferral), so QML QQuickImage can't load https URLs. Fetch the
        // thumbnail via Java's HttpURLConnection (uses Android's SSL stack) and
        // swap cover_url to the local file path.
        if (!coverUrl.isEmpty() && coverUrl.startsWith(QStringLiteral("http"))) {
            const QString localPath = OctaveAndroid::cacheHttpImage(coverUrl);
            if (!localPath.isEmpty()) {
                coverUrl = QUrl::fromLocalFile(localPath).toString();
            }
        }
#endif

        // Album name — yt-dlp flat-playlist may include album in metadata
        QString albumName = obj.value(QStringLiteral("album")).toString();

        // Year — extract from upload_date (YYYYMMDD) or release_year
        int year = 0;
        QJsonValue releaseYear = obj.value(QStringLiteral("release_year"));
        if (releaseYear.isDouble()) {
            year = static_cast<int>(releaseYear.toDouble());
        }
        if (year == 0) {
            QString uploadDate = obj.value(QStringLiteral("upload_date")).toString();
            if (uploadDate.length() >= 4) {
                bool ok = false;
                int y = uploadDate.left(4).toInt(&ok);
                if (ok) {
                    year = y;
                }
            }
        }

        // Build the result object matching Python's format
        QJsonObject result;
        result.insert(QStringLiteral("name"), title);
        result.insert(QStringLiteral("artist"), channel);
        result.insert(QStringLiteral("artists"), QJsonArray({channel}));
        result.insert(QStringLiteral("album_name"), albumName);
        result.insert(QStringLiteral("duration"), duration);
        result.insert(QStringLiteral("year"), year);
        result.insert(QStringLiteral("cover_url"), coverUrl);
        result.insert(QStringLiteral("url"),
                       QStringLiteral("https://music.youtube.com/watch?v=") + videoId);
        result.insert(QStringLiteral("song_id"), videoId);
        result.insert(QStringLiteral("explicit"), false);
        result.insert(QStringLiteral("track_number"), 0);
        result.insert(QStringLiteral("disc_number"), 0);

        results.append(result);
    }

    return results;
}

// ═══════════════════════════════════════════════════════════════════
// Downloaded file detection
// ═══════════════════════════════════════════════════════════════════

QMap<QString, QString> DownloadManager::_getDownloadedFiles() const
{
    QMap<QString, QString> names;

    // Collect roots to scan. On Android the user-configured mediaFolder is
    // typically /sdcard/Music while downloads land in the app-external Music
    // tree (scoped-storage requirement) — MediaManager already knows to scan
    // both; we mirror that here so is_downloaded tagging sees files wherever
    // they actually live.
    QStringList roots;
    if (m_settingsManager) {
        const QString mediaFolder = m_settingsManager->mediaFolder();
        if (!mediaFolder.isEmpty()) {
            roots.append(mediaFolder);
        }
    }
#ifdef Q_OS_ANDROID
    const QString androidMusicRoot = OctaveAndroid::getDownloadsDir();
    if (!androidMusicRoot.isEmpty() && !roots.contains(androidMusicRoot)) {
        roots.append(androidMusicRoot);
    }
#endif

    for (const QString &root : roots) {
        QDir rootDir(root);
        if (!rootDir.exists()) continue;

        QDirIterator it(root, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (!fi.isFile()) continue;

            const QString suffix = QStringLiteral(".") + fi.suffix().toLower();
            if (!s_audioExtensions.contains(suffix)) continue;

            QString folder = fi.dir().dirName();
            // Root-level files belong to "Unsorted"
            if (QDir::cleanPath(fi.dir().absolutePath()) == QDir::cleanPath(root)) {
                folder = QStringLiteral("Unsorted");
            }
            names.insert(fi.completeBaseName().toLower(), folder);
        }
    }

    // Last-resort fallback if neither root yielded anything (e.g. very first
    // download on a fresh install before the media library has been scanned):
    // probe the configured download subfolder directly.
    if (names.isEmpty()) {
        const QString dlPath = _getDownloadPath();
        const QString folder = QFileInfo(dlPath).fileName();
        QDir dlDir(dlPath);
        if (dlDir.exists()) {
            const QFileInfoList entries = dlDir.entryInfoList(QDir::Files);
            for (const QFileInfo &fi : entries) {
                const QString suffix = QStringLiteral(".") + fi.suffix().toLower();
                if (s_audioExtensions.contains(suffix)) {
                    names.insert(fi.completeBaseName().toLower(), folder);
                }
            }
        }
    }

    return names;
}

QString DownloadManager::_sanitizeForMatch(const QString &text)
{
    // Mirror sanitize_string from music_dl/utils/formatter.py
    QString result;
    result.reserve(text.length());
    for (const QChar &c : text) {
        if (c == QLatin1Char('/') || c == QLatin1Char('?') || c == QLatin1Char('\\') ||
            c == QLatin1Char('*') || c == QLatin1Char('|') || c == QLatin1Char('<') ||
            c == QLatin1Char('>')) {
            continue;
        }
        if (c == QLatin1Char('"')) {
            result.append(QLatin1Char('\''));
        } else if (c == QLatin1Char(':')) {
            result.append(QLatin1Char('-'));
        } else {
            result.append(c);
        }
    }
    return result.toLower();
}

void DownloadManager::_markDownloaded(QJsonArray &results)
{
    QMap<QString, QString> downloaded = _getDownloadedFiles();

    for (int i = 0; i < results.size(); ++i) {
        QJsonObject obj = results[i].toObject();
        QString artist = obj.value(QStringLiteral("artist")).toString();
        QString name = obj.value(QStringLiteral("name")).toString();
        QString expected = _sanitizeForMatch(artist + QStringLiteral(" - ") + name);

        if (downloaded.contains(expected)) {
            obj.insert(QStringLiteral("is_downloaded"), true);
            obj.insert(QStringLiteral("downloaded_playlist"), downloaded.value(expected));
        } else {
            obj.insert(QStringLiteral("is_downloaded"), false);
            obj.insert(QStringLiteral("downloaded_playlist"), QString());
        }

        results[i] = obj;
    }
}

// ═══════════════════════════════════════════════════════════════════
// Download
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::download(const QString &songId, const QString &songName)
{
    // Build a minimal JSON and delegate to download_song
    QJsonObject obj;
    obj.insert(QStringLiteral("song_id"), songId);
    obj.insert(QStringLiteral("name"), songName);
    obj.insert(QStringLiteral("artist"), QString());
    obj.insert(QStringLiteral("url"),
               QStringLiteral("https://music.youtube.com/watch?v=") + songId);

    QJsonDocument doc(obj);
    download_song(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void DownloadManager::download_song(const QString &jsonString)
{
    QString ytDlp = _findYtDlp();
    if (ytDlp.isEmpty()) {
        emit downloadError(QString(), QString(),
                           QStringLiteral("yt-dlp not found. Please install yt-dlp."));
        emit statusMessage(QStringLiteral("yt-dlp not found"));
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcDownload) << "Invalid song JSON:" << parseError.errorString();
        emit downloadError(QString(), QStringLiteral("Unknown"),
                           QStringLiteral("Invalid song data: ") + parseError.errorString());
        return;
    }

    QJsonObject songData = doc.object();
    QString songId = songData.value(QStringLiteral("song_id")).toString();
    QString name = songData.value(QStringLiteral("name")).toString();
    QString artist = songData.value(QStringLiteral("artist")).toString();
    QString displayName = artist.isEmpty() ? name : (artist + QStringLiteral(" - ") + name);

    // Prevent duplicate download of the same song
    if (!songId.isEmpty() && m_downloadTasks.contains(songId)) {
        const DownloadTask &existing = m_downloadTasks[songId];
        if (existing.status == QStringLiteral("queued") ||
            existing.status == QStringLiteral("downloading")) {
            emit statusMessage(QStringLiteral("Already downloading: ") + displayName);
            return;
        }
        // If it previously failed, clear the failed state for retry
        _emitStatusPatch(songId, QStringLiteral("is_failed"), false);
        _emitStatusPatch(songId, QStringLiteral("error_message"), QString());
    }

    // Build the download task
    DownloadTask task;
    task.songId = songId;
    task.songName = name;
    task.artist = artist;
    task.albumName = songData.value(QStringLiteral("album_name")).toString();
    task.coverUrl = songData.value(QStringLiteral("cover_url")).toString();
    task.url = songData.value(QStringLiteral("url")).toString();
    if (task.url.isEmpty() && !songId.isEmpty()) {
        task.url = QStringLiteral("https://music.youtube.com/watch?v=") + songId;
    }
    task.duration = songData.value(QStringLiteral("duration")).toInt();
    task.progress = 0.0f;
    task.status = QStringLiteral("queued");

    m_downloadTasks.insert(songId, task);
    if (!m_downloadQueue.contains(songId)) {
        m_downloadQueue.enqueue(songId);
    }

    emit statusMessage(QStringLiteral("Queuing download: ") + displayName);
    emit downloadStarted(songId, displayName);
    emit downloadQueueChanged();

    m_activeDownloads++;
    emit activeDownloadsChanged(m_activeDownloads);

    // Start downloading if under the concurrency limit
    _startNextDownload();
}

void DownloadManager::cancel_download(const QString &songId)
{
    // Kill the running process if any
    if (m_downloadProcesses.contains(songId)) {
        YtDlpProcess *proc = m_downloadProcesses.take(songId);
        proc->disconnect();
        proc->kill();
        proc->waitForFinished(3000);
        proc->deleteLater();
        m_downloadStdout.remove(songId);
    }

    // Remove from queue
    m_downloadQueue.removeAll(songId);

    if (m_downloadTasks.contains(songId)) {
        DownloadTask &task = m_downloadTasks[songId];
        QString displayName = task.artist.isEmpty()
            ? task.songName
            : (task.artist + QStringLiteral(" - ") + task.songName);
        task.status = QStringLiteral("cancelled");

        m_activeDownloads = qMax(0, m_activeDownloads - 1);
        emit activeDownloadsChanged(m_activeDownloads);
        emit downloadError(songId, displayName, QStringLiteral("Download cancelled"));
        emit statusMessage(QStringLiteral("Cancelled: ") + displayName);
        emit downloadQueueChanged();
    }

    // Kick off the next queued download
    _startNextDownload();
}

void DownloadManager::pause_download(const QString &songId)
{
    if (m_downloadProcesses.contains(songId)) {
        YtDlpProcess *proc = m_downloadProcesses.value(songId);
#ifdef Q_OS_WIN
        // Windows doesn't have SIGSTOP; kill and re-queue
        proc->disconnect();
        proc->kill();
        proc->waitForFinished(3000);
        proc->deleteLater();
        m_downloadProcesses.remove(songId);
        m_downloadStdout.remove(songId);
#elif defined(Q_OS_ANDROID)
        // No pause support via JNI bridge — leaving the download running.
        Q_UNUSED(proc);
#else
        // Unix: send SIGSTOP
        ::kill(static_cast<pid_t>(proc->processId()), SIGSTOP);
#endif
        if (m_downloadTasks.contains(songId)) {
            m_downloadTasks[songId].status = QStringLiteral("paused");
        }
        _emitStatusPatch(songId, QStringLiteral("status"), QStringLiteral("paused"));
        emit statusMessage(QStringLiteral("Download paused"));
    }
}

void DownloadManager::resume_download(const QString &songId)
{
    if (!m_downloadTasks.contains(songId)) {
        return;
    }

    DownloadTask &task = m_downloadTasks[songId];
    if (task.status != QStringLiteral("paused")) {
        return;
    }

    if (m_downloadProcesses.contains(songId)) {
#ifdef Q_OS_WIN
        // On Windows the process was killed on pause; restart the download
        m_downloadProcesses.remove(songId);
        task.status = QStringLiteral("queued");
        if (!m_downloadQueue.contains(songId)) {
            m_downloadQueue.prepend(songId);
        }
        _startNextDownload();
#elif defined(Q_OS_ANDROID)
        // No resume support via JNI bridge — the download is still running.
        task.status = QStringLiteral("downloading");
        _emitStatusPatch(songId, QStringLiteral("status"), QStringLiteral("downloading"));
        emit statusMessage(QStringLiteral("Download resumed"));
#else
        // Unix: send SIGCONT
        YtDlpProcess *proc = m_downloadProcesses.value(songId);
        ::kill(static_cast<pid_t>(proc->processId()), SIGCONT);
        task.status = QStringLiteral("downloading");
        _emitStatusPatch(songId, QStringLiteral("status"), QStringLiteral("downloading"));
        emit statusMessage(QStringLiteral("Download resumed"));
#endif
    } else {
        // Process gone, re-queue
        task.status = QStringLiteral("queued");
        if (!m_downloadQueue.contains(songId)) {
            m_downloadQueue.prepend(songId);
        }
        _startNextDownload();
    }
}

void DownloadManager::_startNextDownload()
{
    while (m_downloadProcesses.size() < m_maxConcurrentDownloads &&
           !m_downloadQueue.isEmpty()) {

        QString songId = m_downloadQueue.dequeue();
        if (!m_downloadTasks.contains(songId)) {
            continue;
        }

        DownloadTask &task = m_downloadTasks[songId];
        if (task.status == QStringLiteral("cancelled") ||
            task.status == QStringLiteral("complete")) {
            continue;
        }

        task.status = QStringLiteral("downloading");

        QString ytDlp = _findYtDlp();
        if (ytDlp.isEmpty()) {
            task.status = QStringLiteral("error");
            task.errorMessage = QStringLiteral("yt-dlp not found");
            emit downloadError(songId, task.songName, task.errorMessage);
            m_activeDownloads = qMax(0, m_activeDownloads - 1);
            emit activeDownloadsChanged(m_activeDownloads);
            continue;
        }

        // Build output path. Matches Python music_dl's "{artists} - {title}.ext"
        // template so the shared QML's play_downloaded_song fuzzy match against
        // "artist - name" stems succeeds regardless of which backend wrote the
        // file. yt-dlp's %(artist,uploader)s falls back to uploader when
        // YouTube Music's artist field is missing — avoids " - Title.m4a".
        QString dlPath = _getDownloadPath();
        QString dlFormat = _getDownloadFormat();
        QString outputTemplate = QDir(dlPath).filePath(
            QStringLiteral("%(artist,uploader)s - %(title)s.%(ext)s"));

        // Build yt-dlp arguments
        QStringList args;
        args << QStringLiteral("-f")
             << QStringLiteral("bestaudio[ext=m4a]/bestaudio/best");
        args << QStringLiteral("--no-playlist");
        args << QStringLiteral("--newline");   // Force progress on separate lines
        args << QStringLiteral("--no-warnings");

        // Format conversion via ffmpeg (if not m4a)
        if (dlFormat == QStringLiteral("mp3")) {
            args << QStringLiteral("--extract-audio");
            args << QStringLiteral("--audio-format") << QStringLiteral("mp3");
            QString bitrate = _getDownloadBitrate();
            if (bitrate != QStringLiteral("auto") &&
                bitrate != QStringLiteral("disable")) {
                args << QStringLiteral("--audio-quality") << bitrate;
            }
        } else if (dlFormat == QStringLiteral("flac")) {
            args << QStringLiteral("--extract-audio");
            args << QStringLiteral("--audio-format") << QStringLiteral("flac");
        } else if (dlFormat == QStringLiteral("opus")) {
            args << QStringLiteral("--extract-audio");
            args << QStringLiteral("--audio-format") << QStringLiteral("opus");
        } else if (dlFormat == QStringLiteral("ogg")) {
            args << QStringLiteral("--extract-audio");
            args << QStringLiteral("--audio-format") << QStringLiteral("vorbis");
        }
        // m4a: download natively, no conversion needed

        // YouTube cookies bypass — applies to both Android and desktop. Drop
        // a Netscape-format cookies.txt at <DownloadLocation>/youtube_cookies.txt
        // (exported from a logged-in YouTube session via "Get cookies.txt
        // LOCALLY" or similar) and yt-dlp will use it to bypass the bot wall.
        const QString cookiesPath = _getYoutubeCookiesPath();
        if (!cookiesPath.isEmpty()) {
            args << QStringLiteral("--cookies") << cookiesPath;
            qCInfo(lcDownload) << "Using YouTube cookies from" << cookiesPath;
        } else {
            qCInfo(lcDownload).noquote()
                << "No YouTube cookies file found; downloads may hit bot wall. "
                   "Drop a cookies.txt at"
                << (QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                    + QStringLiteral("/youtube_cookies.txt"));
        }

#ifdef Q_OS_ANDROID
        // TagLib is not available on Android, so ask yt-dlp to embed metadata
        // and thumbnail into the file itself. On desktop this is done by
        // _embedMetadata() post-download.
        args << QStringLiteral("--embed-metadata");
        args << QStringLiteral("--add-metadata");
        // Thumbnail embedding is done post-download via mutagen in
        // _onDownloadProcessFinished — the AAR-bundled ffmpeg is a stripped
        // build without -vf filters, so it can't center-crop YouTube's 16:9
        // thumbnails. Instead we re-use the square ytmusicapi cover JPEG that
        // cacheHttpImage already staged during search parsing.
        // Force .m4a extension + enable the metadata embed postprocessors.
        // --extract-audio runs the AudioExtractor postprocessor which also
        // makes --embed-metadata / --embed-thumbnail actually apply. Without
        // it, remux-video bypasses the metadata pass on some yt-dlp versions.
        // AAC → m4a is a no-op rewrap (no re-encode, no quality loss).
        if (dlFormat == QStringLiteral("m4a") ||
            dlFormat == QStringLiteral("auto") ||
            dlFormat.isEmpty()) {
            args << QStringLiteral("--extract-audio");
            args << QStringLiteral("--audio-format") << QStringLiteral("m4a");
        }
        // No player_client override. Until v0.9.2 this forced
        // "android_vr,ios,tv_embedded,android"; by September 2026 that set
        // yields nothing (android_vr wants a GVS PO token, tv_embedded is no
        // longer a client) and every download ended in HTTP 403, while
        // yt-dlp's own default client list downloads the same videos fine.
        // The bundled yt-dlp self-updates on launch, so its defaults track
        // whatever YouTube currently accepts — leave the choice to it.
#endif

        args << QStringLiteral("-o") << outputTemplate;
        args << task.url;

        YtDlpProcess *proc = new YtDlpProcess(this);
        proc->setProperty("songId", songId);

        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&YtDlpProcess::finished),
                this, &DownloadManager::_onDownloadProcessFinished);
        connect(proc, &YtDlpProcess::errorOccurred,
                this, &DownloadManager::_onDownloadProcessError);
        connect(proc, &YtDlpProcess::readyReadStandardOutput,
                this, &DownloadManager::_onDownloadReadyReadStdout);
        connect(proc, &YtDlpProcess::readyReadStandardError,
                this, &DownloadManager::_onDownloadReadyReadStderr);

        m_downloadProcesses.insert(songId, proc);
        m_downloadStdout.insert(songId, QByteArray());

        qCInfo(lcDownload) << "Starting download:" << ytDlp << args;
        proc->start(ytDlp, args);

#ifdef Q_OS_MOBILE
        // Start the polling timer so the UI sees live progress. The JNI
        // yt-dlp call blocks its worker thread; only a main-thread poll can
        // give the UI intermediate progress values.
        if (m_mobileProgressPoller && !m_mobileProgressPoller->isActive())
            m_mobileProgressPoller->start();
#endif
    }
}

// ═══════════════════════════════════════════════════════════════════
// Download process signal handlers
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::_onDownloadReadyReadStdout()
{
    auto *proc = qobject_cast<YtDlpProcess *>(sender());
    if (!proc) {
        return;
    }

    QString songId = proc->property("songId").toString();
    QByteArray data = proc->readAllStandardOutput();
    m_downloadStdout[songId].append(data);

    // Parse progress lines like: [download]  45.2% of ~5.32MiB at 1.23MiB/s ETA 00:03
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        QString lineStr = QString::fromUtf8(line).trimmed();
        float progress = _parseProgressLine(lineStr);
        if (progress >= 0.0f) {
            if (m_downloadTasks.contains(songId)) {
                m_downloadTasks[songId].progress = progress;
            }
            emit downloadProgress(songId, progress);
        }
    }
}

void DownloadManager::_onDownloadReadyReadStderr()
{
    auto *proc = qobject_cast<YtDlpProcess *>(sender());
    if (!proc) {
        return;
    }

    // Log stderr but don't treat as fatal (yt-dlp emits warnings to stderr)
    QByteArray data = proc->readAllStandardError();
    if (!data.trimmed().isEmpty()) {
        qCDebug(lcDownload) << "yt-dlp stderr:" << data.trimmed();
    }
}

float DownloadManager::_parseProgressLine(const QString &line)
{
    // Match: [download]  45.2% of ~5.32MiB ...
    // Also: [download]  100% of 5.32MiB
    static const QRegularExpression progressRe(
        QStringLiteral(R"(\[download\]\s+(\d+\.?\d*)%)"));

    QRegularExpressionMatch match = progressRe.match(line);
    if (match.hasMatch()) {
        bool ok = false;
        float pct = match.captured(1).toFloat(&ok);
        if (ok) {
            return qBound(0.0f, pct / 100.0f, 1.0f);
        }
    }

    return -1.0f; // No progress found
}

#ifdef Q_OS_MOBILE
void DownloadManager::_pollMobileProgress()
{
    // The JNI ProgressFn stores progress in a single static field per the
    // youtubedl-android API; we can't distinguish per-song here, so broadcast
    // the same value to every active download task. Acceptable until the
    // rare case of concurrent downloads becomes worth a per-task callback.
    //
    // youtubedl-android's ProgressFn delivers a percentage (0–100). The
    // downstream QML and the desktop stdout parser both speak fractions
    // (0.0–1.0), so divide here to match — otherwise the UI renders "10000%".
    const float progress = qBound(0.0f, OctaveAndroid::pollProgress() / 100.0f, 1.0f);
    if (progress <= 0.0f)
        return;

    for (auto it = m_downloadProcesses.constBegin();
         it != m_downloadProcesses.constEnd(); ++it) {
        const QString &songId = it.key();
        if (!m_downloadTasks.contains(songId))
            continue;
        DownloadTask &task = m_downloadTasks[songId];
        // Monotonic: don't regress if the poll caught a stale value.
        if (progress > task.progress) {
            task.progress = progress;
            emit downloadProgress(songId, progress);
        }
    }
}
#endif

void DownloadManager::_onDownloadProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)

    auto *proc = qobject_cast<YtDlpProcess *>(sender());
    if (!proc) {
        return;
    }

    QString songId = proc->property("songId").toString();

    // Read any remaining output
    m_downloadStdout[songId].append(proc->readAllStandardOutput());

    m_downloadProcesses.remove(songId);
    proc->deleteLater();

    m_activeDownloads = qMax(0, m_activeDownloads - 1);
    emit activeDownloadsChanged(m_activeDownloads);

#ifdef Q_OS_MOBILE
    // Stop polling once the queue is drained so we're not spinning a timer
    // when nothing's downloading.
    if (m_activeDownloads == 0 && m_mobileProgressPoller && m_mobileProgressPoller->isActive())
        m_mobileProgressPoller->stop();
#endif

    if (!m_downloadTasks.contains(songId)) {
        m_downloadStdout.remove(songId);
        _startNextDownload();
        return;
    }

    DownloadTask &task = m_downloadTasks[songId];

    if (exitCode != 0) {
        // Extract error info from stderr or stdout
        QString errorMsg = QStringLiteral("Download failed (exit code %1)").arg(exitCode);
        task.status = QStringLiteral("error");
        task.errorMessage = errorMsg;
        QString displayName = task.artist.isEmpty()
            ? task.songName
            : (task.artist + QStringLiteral(" - ") + task.songName);
        emit downloadError(songId, displayName, errorMsg);
        emit statusMessage(QStringLiteral("Failed: ") + displayName);
        _emitStatusPatch(songId, QStringLiteral("is_failed"), true);
        _emitStatusPatch(songId, QStringLiteral("error_message"), errorMsg);

        qCWarning(lcDownload) << "Download failed for" << songId << "exit code:" << exitCode;
    } else {
        // Parse the output to find the downloaded file path
        // yt-dlp prints: [download] Destination: /path/to/file.ext
        // or after conversion: [ExtractAudio] Destination: /path/to/file.mp3
        QString filePath;
        QString allOutput = QString::fromUtf8(m_downloadStdout.value(songId));
        const QStringList outputLines = allOutput.split(QLatin1Char('\n'));

        // Search in reverse — the last "Destination:" line is the final file
        for (int i = outputLines.size() - 1; i >= 0; --i) {
            const QString &outLine = outputLines[i].trimmed();

            // [ExtractAudio] Destination: /path/to/song.mp3
            if (outLine.contains(QStringLiteral("Destination:"))) {
                int idx = outLine.indexOf(QStringLiteral("Destination:"));
                filePath = outLine.mid(idx + 12).trimmed();
                break;
            }
        }

        // Fallback: scan download directory for most recently modified file
        if (filePath.isEmpty() || !QFile::exists(filePath)) {
            QString dlPath = _getDownloadPath();
            QDir dlDir(dlPath);
            QFileInfoList files = dlDir.entryInfoList(QDir::Files, QDir::Time);
            for (const QFileInfo &fi : files) {
                QString suffix = QStringLiteral(".") + fi.suffix().toLower();
                if (s_audioExtensions.contains(suffix)) {
                    filePath = fi.absoluteFilePath();
                    break;
                }
            }
        }

        if (!filePath.isEmpty() && QFile::exists(filePath)) {
            task.status = QStringLiteral("complete");
            task.filePath = filePath;

#ifndef Q_OS_ANDROID
            // Embed metadata using TagLib (desktop only — Android uses
            // yt-dlp's --embed-metadata / --add-metadata flags at download time
            // and a mutagen post-pass for the cover).
            _embedMetadata(filePath, task);

            // Download cover art and embed if available
            if (!task.coverUrl.isEmpty()) {
                _downloadCoverArt(songId, task.coverUrl, filePath);
            }
#else
            // Swap yt-dlp's 16:9 YouTube thumbnail for the square ytmusicapi
            // cover we already cached locally during search parsing. task.coverUrl
            // is typically a file:// URL pointing into the thumb cache.
            if (!task.coverUrl.isEmpty()) {
                const QString coverLocal = task.coverUrl.startsWith(QStringLiteral("file:"))
                    ? QUrl(task.coverUrl).toLocalFile()
                    : OctaveAndroid::cacheHttpImage(task.coverUrl);
                if (!coverLocal.isEmpty() && QFile::exists(coverLocal)) {
                    const bool ok = OctaveAndroid::runEmbedCover(filePath, coverLocal);
                    if (!ok) {
                        qCWarning(lcDownload)
                            << "runEmbedCover failed for" << filePath
                            << "(cover:" << coverLocal << ")";
                    }
                }
            }
#endif

            QString displayName = task.artist.isEmpty()
                ? task.songName
                : (task.artist + QStringLiteral(" - ") + task.songName);

            emit downloadComplete(songId, filePath);
            emit statusMessage(QStringLiteral("Downloaded: ") + displayName);
            _emitStatusPatch(songId, QStringLiteral("is_downloaded"), true);

            qCInfo(lcDownload) << "Download complete:" << displayName << "->" << filePath;
        } else {
            task.status = QStringLiteral("error");
            QString errorMsg = QStringLiteral("Download completed but file not found");
            task.errorMessage = errorMsg;
            QString displayName = task.artist.isEmpty()
                ? task.songName
                : (task.artist + QStringLiteral(" - ") + task.songName);
            emit downloadError(songId, displayName, errorMsg);
            emit statusMessage(QStringLiteral("Failed: ") + displayName);
            _emitStatusPatch(songId, QStringLiteral("is_failed"), true);
            _emitStatusPatch(songId, QStringLiteral("error_message"), errorMsg);

            qCWarning(lcDownload) << "Download file not found for" << songId;
        }
    }

    m_downloadStdout.remove(songId);
    emit downloadQueueChanged();

    // Kick off the next queued download
    _startNextDownload();
}

void DownloadManager::_onDownloadProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)

    auto *proc = qobject_cast<YtDlpProcess *>(sender());
    if (!proc) {
        return;
    }

    QString songId = proc->property("songId").toString();

    m_downloadProcesses.remove(songId);
    m_downloadStdout.remove(songId);
    proc->deleteLater();

    m_activeDownloads = qMax(0, m_activeDownloads - 1);
    emit activeDownloadsChanged(m_activeDownloads);

#ifdef Q_OS_MOBILE
    if (m_activeDownloads == 0 && m_mobileProgressPoller && m_mobileProgressPoller->isActive())
        m_mobileProgressPoller->stop();
#endif

    if (m_downloadTasks.contains(songId)) {
        DownloadTask &task = m_downloadTasks[songId];
        task.status = QStringLiteral("error");
        task.errorMessage = QStringLiteral("Failed to start yt-dlp process");
        QString displayName = task.artist.isEmpty()
            ? task.songName
            : (task.artist + QStringLiteral(" - ") + task.songName);
        emit downloadError(songId, displayName, task.errorMessage);
        emit statusMessage(QStringLiteral("Download process error: ") + displayName);
        _emitStatusPatch(songId, QStringLiteral("is_failed"), true);
        _emitStatusPatch(songId, QStringLiteral("error_message"), task.errorMessage);
    }

    emit downloadQueueChanged();
    _startNextDownload();
}

void DownloadManager::_processDownloadQueue()
{
    _startNextDownload();
}

// ═══════════════════════════════════════════════════════════════════
// Metadata embedding (TagLib — desktop only; Android uses yt-dlp's
// --embed-metadata / --embed-thumbnail at download time)
// ═══════════════════════════════════════════════════════════════════

#ifdef Q_OS_MOBILE
void DownloadManager::_embedMetadata(const QString &, const DownloadTask &) {}
void DownloadManager::_downloadCoverArt(const QString &, const QString &, const QString &) {}
void DownloadManager::_onCoverDownloadFinished(QNetworkReply *reply) { reply->deleteLater(); }
#else

void DownloadManager::_embedMetadata(const QString &filePath, const DownloadTask &task)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return;
    }

    // Use TagLib's generic FileRef for basic tag writing
    TagLib::FileRef fileRef(filePath.toUtf8().constData());
    if (fileRef.isNull() || !fileRef.tag()) {
        qCWarning(lcDownload) << "TagLib could not open file:" << filePath;
        return;
    }

    TagLib::Tag *tag = fileRef.tag();

    if (!task.songName.isEmpty()) {
        tag->setTitle(TagLib::String(task.songName.toUtf8().constData(), TagLib::String::UTF8));
    }
    if (!task.artist.isEmpty()) {
        tag->setArtist(TagLib::String(task.artist.toUtf8().constData(), TagLib::String::UTF8));
    }
    if (!task.albumName.isEmpty()) {
        tag->setAlbum(TagLib::String(task.albumName.toUtf8().constData(), TagLib::String::UTF8));
    }

    if (!fileRef.save()) {
        qCWarning(lcDownload) << "TagLib failed to save metadata for:" << filePath;
    } else {
        qCDebug(lcDownload) << "Metadata embedded:" << filePath;
    }
}

// ═══════════════════════════════════════════════════════════════════
// Cover art download + embed
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::_downloadCoverArt(const QString &songId, const QString &coverUrl,
                                         const QString &filePath)
{
    if (coverUrl.isEmpty() || filePath.isEmpty()) {
        return;
    }

    QNetworkRequest request{QUrl(coverUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // Store songId and filePath as properties on the reply for retrieval in the finished handler
    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("songId", songId);
    reply->setProperty("filePath", filePath);
}

void DownloadManager::_onCoverDownloadFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qCDebug(lcDownload) << "Cover art download failed:" << reply->errorString();
        return;
    }

    QString songId = reply->property("songId").toString();
    QString filePath = reply->property("filePath").toString();
    QByteArray imageData = reply->readAll();

    if (imageData.isEmpty() || filePath.isEmpty()) {
        return;
    }

    // Determine MIME type from data
    QString mimeType = QStringLiteral("image/jpeg");
    if (imageData.startsWith("\x89PNG")) {
        mimeType = QStringLiteral("image/png");
    } else if (imageData.startsWith("RIFF")) {
        mimeType = QStringLiteral("image/webp");
    }

    // Embed cover art based on file format
    QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == QStringLiteral("mp3")) {
        // ID3v2 APIC frame
        auto *mpegFile = new TagLib::MPEG::File(filePath.toUtf8().constData());
        if (mpegFile->isValid()) {
            TagLib::ID3v2::Tag *id3v2 = mpegFile->ID3v2Tag(true);
            if (id3v2) {
                // Remove existing APIC frames
                id3v2->removeFrames("APIC");

                auto *picFrame = new TagLib::ID3v2::AttachedPictureFrame();
                picFrame->setMimeType(TagLib::String(mimeType.toUtf8().constData(),
                                                      TagLib::String::UTF8));
                picFrame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
                picFrame->setPicture(TagLib::ByteVector(imageData.constData(),
                                                        static_cast<unsigned int>(imageData.size())));
                id3v2->addFrame(picFrame);
                mpegFile->save(TagLib::MPEG::File::ID3v2);
            }
        }
        delete mpegFile;

    } else if (suffix == QStringLiteral("m4a") || suffix == QStringLiteral("mp4")) {
        // MP4 covr atom
        auto *mp4File = new TagLib::MP4::File(filePath.toUtf8().constData());
        if (mp4File->isValid() && mp4File->tag()) {
            TagLib::MP4::CoverArt::Format fmt = TagLib::MP4::CoverArt::JPEG;
            if (mimeType == QStringLiteral("image/png")) {
                fmt = TagLib::MP4::CoverArt::PNG;
            }

            TagLib::MP4::CoverArt art(fmt,
                TagLib::ByteVector(imageData.constData(),
                                   static_cast<unsigned int>(imageData.size())));
            TagLib::MP4::CoverArtList artList;
            artList.append(art);
            mp4File->tag()->setItem("covr", TagLib::MP4::Item(artList));
            mp4File->save();
        }
        delete mp4File;

    } else if (suffix == QStringLiteral("flac")) {
        // FLAC picture block
        auto *flacFile = new TagLib::FLAC::File(filePath.toUtf8().constData());
        if (flacFile->isValid()) {
            // Remove existing pictures
            flacFile->removePictures();

            auto *pic = new TagLib::FLAC::Picture();
            pic->setMimeType(TagLib::String(mimeType.toUtf8().constData(),
                                             TagLib::String::UTF8));
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            pic->setData(TagLib::ByteVector(imageData.constData(),
                                            static_cast<unsigned int>(imageData.size())));
            flacFile->addPicture(pic);
            flacFile->save();
        }
        delete flacFile;

    } else if (suffix == QStringLiteral("ogg") || suffix == QStringLiteral("opus")) {
        // Xiph comment METADATA_BLOCK_PICTURE
        TagLib::FileRef fileRef(filePath.toUtf8().constData());
        if (!fileRef.isNull()) {
            TagLib::Ogg::XiphComment *xiph = nullptr;

            if (suffix == QStringLiteral("ogg")) {
                auto *vorbisFile = dynamic_cast<TagLib::Ogg::Vorbis::File *>(fileRef.file());
                if (vorbisFile) {
                    xiph = vorbisFile->tag();
                }
            } else {
                auto *opusFile = dynamic_cast<TagLib::Ogg::Opus::File *>(fileRef.file());
                if (opusFile) {
                    xiph = opusFile->tag();
                }
            }

            if (xiph) {
                auto *pic = new TagLib::FLAC::Picture();
                pic->setMimeType(TagLib::String(mimeType.toUtf8().constData(),
                                                 TagLib::String::UTF8));
                pic->setType(TagLib::FLAC::Picture::FrontCover);
                pic->setData(TagLib::ByteVector(imageData.constData(),
                                                static_cast<unsigned int>(imageData.size())));
                xiph->addPicture(pic);
                fileRef.save();
            }
        }
    }

    qCDebug(lcDownload) << "Cover art embedded for:" << songId;
}

#endif // Q_OS_MOBILE — TagLib metadata embedding block

// ═══════════════════════════════════════════════════════════════════
// Queue management
// ═══════════════════════════════════════════════════════════════════

QVariantList DownloadManager::get_download_queue()
{
    QVariantList list;
    for (auto it = m_downloadTasks.constBegin(); it != m_downloadTasks.constEnd(); ++it) {
        const DownloadTask &task = it.value();
        QVariantMap item;
        item.insert(QStringLiteral("song_id"), task.songId);
        item.insert(QStringLiteral("name"), task.songName);
        item.insert(QStringLiteral("artist"), task.artist);
        item.insert(QStringLiteral("album_name"), task.albumName);
        item.insert(QStringLiteral("cover_url"), task.coverUrl);
        item.insert(QStringLiteral("progress"), static_cast<double>(task.progress));
        item.insert(QStringLiteral("status"), task.status);
        item.insert(QStringLiteral("error_message"), task.errorMessage);
        item.insert(QStringLiteral("file_path"), task.filePath);
        list.append(item);
    }
    return list;
}

void DownloadManager::clear_download_queue()
{
    // Cancel all active downloads
    const QList<QString> activeIds = m_downloadProcesses.keys();
    for (const QString &songId : activeIds) {
        cancel_download(songId);
    }

    m_downloadQueue.clear();
    m_downloadTasks.clear();
    m_downloadStdout.clear();

    m_activeDownloads = 0;
    emit activeDownloadsChanged(0);
    emit downloadQueueChanged();
    emit statusMessage(QStringLiteral("Download queue cleared"));
}

// ═══════════════════════════════════════════════════════════════════
// Playlist / subfolder
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::set_download_playlist(const QString &name)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        m_targetSubfolder.clear();
        qCInfo(lcDownload) << "Download playlist reset to default";
    } else {
        m_targetSubfolder = trimmed;
        qCInfo(lcDownload) << "Download playlist set to:" << m_targetSubfolder;
    }
}

QString DownloadManager::get_download_playlist()
{
    if (!m_targetSubfolder.isEmpty()) {
        return m_targetSubfolder;
    }
    if (m_settingsManager) {
        QVariant val = m_settingsManager->get_setting_with_default(
            QStringLiteral("downloadSubfolder"), QStringLiteral("Downloads"));
        return val.toString();
    }
    return QStringLiteral("Downloads");
}

// ═══════════════════════════════════════════════════════════════════
// Status patch helper
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::_emitStatusPatch(const QString &songId, const QString &field,
                                        const QVariant &value)
{
    if (songId.isEmpty()) {
        return;
    }

    // Update the stored result in m_allResults
    for (int i = 0; i < m_allResults.size(); ++i) {
        QJsonObject obj = m_allResults[i].toObject();
        if (obj.value(QStringLiteral("song_id")).toString() == songId) {
            obj.insert(field, QJsonValue::fromVariant(value));
            m_allResults[i] = obj;
            break;
        }
    }

    // Emit the targeted patch as a single JSON value. QJsonDocument's top
    // level only supports object/array, so wrap the scalar in a one-element
    // array and strip the brackets — that way bools, ints, doubles, strings,
    // and null all serialize uniformly to valid JSON the QML JSON.parse()
    // receiver can consume.
    QJsonArray arr;
    arr.append(QJsonValue::fromVariant(value));
    QString jsonStr = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (jsonStr.startsWith(QLatin1Char('[')) && jsonStr.endsWith(QLatin1Char(']'))) {
        jsonStr = jsonStr.mid(1, jsonStr.length() - 2);
    }
    emit downloadStatusChanged(songId, field, jsonStr);
}

// ═══════════════════════════════════════════════════════════════════
// Cleanup
// ═══════════════════════════════════════════════════════════════════

void DownloadManager::cleanup()
{
    // Kill search process
    if (m_searchProcess) {
        m_searchProcess->disconnect();
        m_searchProcess->kill();
        m_searchProcess->waitForFinished(3000);
        m_searchProcess->deleteLater();
        m_searchProcess = nullptr;
    }

    // Kill all download processes
    for (auto it = m_downloadProcesses.begin(); it != m_downloadProcesses.end(); ++it) {
        YtDlpProcess *proc = it.value();
        proc->disconnect();
        proc->kill();
        proc->waitForFinished(3000);
        proc->deleteLater();
    }
    m_downloadProcesses.clear();
    m_downloadStdout.clear();

    qCInfo(lcDownload) << "DownloadManager cleaned up";
}

#endif // OCTAVE_ENABLE_DOWNLOADS
