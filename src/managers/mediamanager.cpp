#include "mediamanager.h"
#include "settingsmanager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QThread>
#include <QCoreApplication>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QDateTime>
#include <QJsonArray>
#include <QColor>

#ifndef Q_OS_MOBILE
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/oggfile.h>
#include <taglib/vorbisfile.h>
#include <taglib/xiphcomment.h>
#endif // Q_OS_MOBILE

#ifdef Q_OS_ANDROID
#include "../platform/androidmediabridge.h"
#include <QUrl>
#endif

#include <algorithm>
#include <cmath>
#include <random>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMedia, "octave.media")

// ─── Static members ────────────────────────────────────────────────

const QStringList MediaManager::s_audioExtensions = {
    QStringLiteral(".mp3"),
    QStringLiteral(".m4a"),
    // YouTube serves audio-only streams in an MP4 container that yt-dlp names
    // with a literal .mp4 extension. Treat those as audio — they're the same
    // format as .m4a, just a different extension.
    QStringLiteral(".mp4"),
    QStringLiteral(".flac"),
    QStringLiteral(".ogg"),
    QStringLiteral(".opus"),
    QStringLiteral(".wav"),
};

QRegularExpression MediaManager::s_junkRe(
    QStringLiteral(
        "\\(Official\\s+Audio\\)"
        "|\\(Official\\s+Video\\)"
        "|\\(Official\\s+Music\\s+Video\\)"
        "|\\(Official\\s+Lyric\\s+Video\\)"
        "|\\(Lyrics?\\)"
        "|\\(Audio\\)"
        "|\\(Visuali[sz]er\\)"
        "|\\(Music\\s+Video\\)"
        "|\\(Live\\)"
        "|\\[Official\\]"
        "|\\[Official\\s+Audio\\]"
        "|\\[Official\\s+Video\\]"
        "|\\[HD\\]"
        "|\\[HQ\\]"
        "|\\[4K\\]"
        "|\\[Lyrics?\\]"
        "|\\(feat\\.\\s*[^)]*\\)"
        "|\\(ft\\.\\s*[^)]*\\)"
        "|\\[feat\\.\\s*[^\\]]*\\]"
        "|\\[ft\\.\\s*[^\\]]*\\]"
    ),
    QRegularExpression::CaseInsensitiveOption
);

QRegularExpression MediaManager::s_trackNumRe(
    QStringLiteral("^\\d{1,3}\\s*[-._)\\s]\\s*")
);

// ─── Helper: does a filename end with a known audio extension? ─────
// Keep in sync with MediaManager::s_audioExtensions above.
static const QStringList s_audioExts = {
    QStringLiteral(".mp3"), QStringLiteral(".m4a"), QStringLiteral(".mp4"),
    QStringLiteral(".flac"), QStringLiteral(".ogg"), QStringLiteral(".opus"),
    QStringLiteral(".wav")
};

static bool hasAudioExtension(const QString &filename)
{
    const QString lower = filename.toLower();
    for (const QString &ext : s_audioExts) {
        if (lower.endsWith(ext))
            return true;
    }
    return false;
}

// ─── Helper: sort key (strip punctuation, lowercase) ───────────────
static QString cleanForSort(const QString &filename)
{
    static QRegularExpression re(QStringLiteral("[^\\w\\s]|_"));
    return QString(filename).toLower().replace(re, QString());
}

// ─── Constructor ───────────────────────────────────────────────────

MediaManager::MediaManager(QObject *parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.5f);

    // Ducking ramp: fast attack so a prompt is not stepped on, slower release
    // so music does not jump back up between sentences.
    m_duckAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_duckAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_duckCurrent = v.toFloat();
        applyOutputVolume();
    });

    // Directories — use application dir as base, like the Python backend/ dir.
    // On Android, applicationDirPath is /data/app/.../lib (not writable by us),
    // so use the standard Music location for default library + app's
    // writable data dir for temp.
    m_backendDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_ANDROID
    m_defaultMediaDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (m_defaultMediaDir.isEmpty())
        m_defaultMediaDir = QStringLiteral("/sdcard/Music");
    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/temp");
#else
    m_defaultMediaDir = m_backendDir + QStringLiteral("/media");
    m_tempDir = m_backendDir + QStringLiteral("/temp");
#endif
    m_mediaDir = m_defaultMediaDir;

    // Connect player signals
    connect(m_player, &QMediaPlayer::durationChanged, this, &MediaManager::durationChanged);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MediaManager::positionChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &MediaManager::_handle_media_status);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MediaManager::_handle_player_error);

    // Position timer (100 ms) as a safety net for backends whose
    // QMediaPlayer::positionChanged is coarse. It only runs while playing so
    // an idle app does not wake ten times a second for nothing.
    m_positionTimer.setInterval(100);
    connect(&m_positionTimer, &QTimer::timeout, this, &MediaManager::_update_position);
    connect(this, &MediaManager::playStateChanged, this, [this](bool playing) {
        if (playing)
            m_positionTimer.start();
        else
            m_positionTimer.stop();
    });

    // Debounce timer for saving playback state (1s after last track change)
    m_saveStateTimer.setInterval(1000);
    m_saveStateTimer.setSingleShot(true);
    connect(&m_saveStateTimer, &QTimer::timeout, this, &MediaManager::_save_playback_state_now);

    // Pre-cache timer (80ms after track change, cache neighbors)
    m_precacheTimer.setInterval(80);
    m_precacheTimer.setSingleShot(true);
    connect(&m_precacheTimer, &QTimer::timeout, this, &MediaManager::_precache_neighbors_start);

    _ensure_directories();
    // Extracted covers are content-addressed (album id hash) and stay in the
    // temp dir across runs; wiping them here made every cover a fresh TagLib
    // read + JPEG write on each boot. _manage_cache() only bounds the covers
    // this run knows about, so trim the oldest leftovers once at startup.
    _prune_cover_dir();

    // Display names
    m_displayNamesPath = SettingsManager::getAppDataDir() + QStringLiteral("/display_names.json");
    m_metaStorePath = SettingsManager::getAppDataDir() + QStringLiteral("/metadata_cache.json");
    _load_meta_store();
    m_metaStoreSaveTimer.setSingleShot(true);
    m_metaStoreSaveTimer.setInterval(2000);
    connect(&m_metaStoreSaveTimer, &QTimer::timeout, this, &MediaManager::_save_meta_store);
    _load_display_names();

    // Connect own signal for Album Art Capture theme updates
    connect(this, &MediaManager::currentMediaChanged, this, &MediaManager::_on_media_changed_for_theme);
}

MediaManager::~MediaManager()
{
    // Flush any pending debounced save
    if (m_saveStateTimer.isActive()) {
        m_saveStateTimer.stop();
        _save_playback_state_now();
    }
    _save_meta_store();
    if (m_player) {
        m_player->stop();
    }
    m_positionTimer.stop();
    m_precacheTimer.stop();
}

// ─── Settings manager wiring ───────────────────────────────────────

void MediaManager::setSettingsManager(SettingsManager *sm)
{
    if (m_settingsManager)
        return; // guard against double init

    m_settingsManager = sm;
    if (!m_settingsManager)
        return;

    // Set library root from settings and scan
    set_library_root(m_settingsManager->mediaFolder());

    // Connect to future media folder changes
    connect(m_settingsManager, &SettingsManager::mediaFolderChanged,
            this, &MediaManager::set_library_root);

    // Restore last playback state after library is scanned
    QTimer::singleShot(500, this, &MediaManager::_restore_playback_state);

    // Connect theme change
    connect(m_settingsManager, &SettingsManager::themeSettingChanged,
            this, &MediaManager::_on_theme_changed);

    // Check if Album Art Capture is already active
    m_albumArtCaptureActive = (m_settingsManager->themeSetting() == QStringLiteral("Album Art Capture"));

    // If active on startup, trigger initial extraction after restore
    if (m_albumArtCaptureActive) {
        QTimer::singleShot(1000, this, &MediaManager::_extract_colors_on_startup);
    }
}

// ─── Path safety ───────────────────────────────────────────────────

bool MediaManager::_is_safe_path(const QString &basePath, const QString &targetPath)
{
    QFileInfo baseInfo(basePath);
    QFileInfo targetInfo(targetPath);
    const QString base = baseInfo.canonicalFilePath();
    const QString target = targetInfo.canonicalFilePath().isEmpty()
                               ? QDir::cleanPath(QFileInfo(targetPath).absoluteFilePath())
                               : targetInfo.canonicalFilePath();
    return target.startsWith(base + QDir::separator()) || target == base;
}

QString MediaManager::_sanitize_metadata(const QString &text, int maxLength)
{
    if (text.isEmpty())
        return text;

    QString sanitized;
    sanitized.reserve(text.size());
    for (const QChar &ch : text) {
        if (ch.isPrint() || ch == QLatin1Char(' '))
            sanitized.append(ch);
        else
            sanitized.append(QLatin1Char(' '));
    }

    // Collapse multiple spaces and strip
    sanitized = sanitized.simplified();

    if (sanitized.size() > maxLength) {
        sanitized = sanitized.left(maxLength - 3) + QStringLiteral("...");
    }
    return sanitized;
}

// ─── Directory helpers ─────────────────────────────────────────────

void MediaManager::_ensure_directories()
{
    QDir dir;
    if (!dir.exists(m_mediaDir)) {
        dir.mkpath(m_mediaDir);
        qCInfo(lcMedia) << "Created media directory:" << m_mediaDir;
    }
    if (!dir.exists(m_tempDir)) {
        dir.mkpath(m_tempDir);
        qCInfo(lcMedia) << "Created temp directory:" << m_tempDir;
    }
}

// Keep at most m_maxCacheFiles cover files on disk, dropping the least
// recently read ones (covers from removed tracks would otherwise accumulate
// forever now that the temp dir survives restarts).
void MediaManager::_prune_cover_dir()
{
    QDir tempDir(m_tempDir);
    QFileInfoList covers = tempDir.entryInfoList({QStringLiteral("cover_*")}, QDir::Files);
    const int excess = covers.size() - m_maxCacheFiles;
    if (excess <= 0)
        return;
    std::sort(covers.begin(), covers.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastRead() < b.lastRead();
    });
    for (int i = 0; i < excess; ++i)
        QFile::remove(covers[i].absoluteFilePath());
    qCInfo(lcMedia) << "Pruned" << excess << "stale cover files from" << m_tempDir;
}

void MediaManager::clearTempFilesInternal()
{
    QDir tempDir(m_tempDir);
    if (tempDir.exists()) {
        const QFileInfoList entries = tempDir.entryInfoList(QDir::Files);
        for (const QFileInfo &fi : entries) {
            QFile::remove(fi.absoluteFilePath());
        }
    }
    // Ensure directory exists
    QDir().mkpath(m_tempDir);
}

// ─── Position timer ────────────────────────────────────────────────

void MediaManager::_update_position()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        emit positionChanged(m_player->position());
    }
}

// ─── Media status / error handling ─────────────────────────────────

void MediaManager::_handle_media_status(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::EndOfMedia) {
        qCInfo(lcMedia) << "Song ended, playing next track";
        next_track();
    } else if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
        // The file decodes: this is what "recovered" actually means
        if (m_recoveryAttempts)
            qCInfo(lcMedia) << "Playback recovered for:" << m_recoveryFile;
        m_recoveryAttempts = 0;
        m_consecutiveBadTracks = 0;
    } else if (status == QMediaPlayer::StalledMedia || status == QMediaPlayer::InvalidMedia) {
        if (!m_isPlaying)
            return;   // a stopped player keeps reporting its last failure
        qCWarning(lcMedia) << (status == QMediaPlayer::StalledMedia ? "Media stalled - attempting recovery"
                                                                     : "Invalid media - attempting recovery");
        _attempt_playback_recovery();
    }
}

void MediaManager::_handle_player_error(QMediaPlayer::Error error, const QString &errorString)
{
    if (!m_isPlaying) {
        qCDebug(lcMedia) << "Player error while stopped" << error << ":" << errorString;
        return;
    }
    qCWarning(lcMedia) << "Player error" << error << ":" << errorString;
    _attempt_playback_recovery();
}

static constexpr int kMaxRecoveryAttempts = 2;        // per file, then skip it
static constexpr int kMaxConsecutiveBadTracks = 10;   // then stop instead of looping the library

// Re-seat the current source and resume playback. Capped per file: after
// kMaxRecoveryAttempts the track is skipped (a tag-only stub or a truncated
// file never becomes playable), and after kMaxConsecutiveBadTracks skips
// playback stops.
// Status/error driven calls are ignored while nothing is playing (a stopped
// player keeps reporting the last failure); toggle_play passes force.
void MediaManager::_attempt_playback_recovery(bool force)
{
    if (!m_isPlaying && !force)
        return;
    const QString currentFile = get_current_file();
    if (currentFile.isEmpty()) {
        qCWarning(lcMedia) << "Recovery failed - no current file";
        return;
    }

    if (currentFile != m_recoveryFile) {
        m_recoveryFile = currentFile;
        m_recoveryAttempts = 0;
    }
    if (++m_recoveryAttempts > kMaxRecoveryAttempts) {
        qCCritical(lcMedia) << "Giving up on" << currentFile << "after" << kMaxRecoveryAttempts
                            << "recovery attempts; skipping it";
        _skip_bad_track();
        return;
    }

    const qint64 position = m_player->position();
    const bool wasPlaying = m_isPlaying;

    const QString filePath = _get_file_path(currentFile);
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        qCWarning(lcMedia) << "Recovery failed - file missing:" << filePath;
        _skip_bad_track();
        return;
    }

    qCInfo(lcMedia) << "Recovering playback for:" << currentFile << "at" << position << "ms"
                    << "(attempt" << m_recoveryAttempts << "/" << kMaxRecoveryAttempts << ")";

    // Clear then re-set the source: Qt does not reload (or re-report an
    // error for) a source identical to the current one.
    m_player->setSource(QUrl());
    m_player->setSource(QUrl::fromLocalFile(filePath));
    if (position > 0)
        m_player->setPosition(position);

    if (wasPlaying) {
        m_player->play();
        m_isPlaying = true;
        m_isPaused = false;
    }

    // Restore mute state after recovery
    if (m_isMuted)
        m_audioOutput->setVolume(0.0f);

    emit playStateChanged(m_isPlaying);
}

// The current file is unplayable: move on, or stop if the whole playlist is
// turning out that way.
void MediaManager::_skip_bad_track()
{
    m_recoveryAttempts = 0;
    m_recoveryFile.clear();
    ++m_consecutiveBadTracks;
    const int limit = qMin(kMaxConsecutiveBadTracks, qMax(1, int(m_currentPlaylist.size())));
    if (m_consecutiveBadTracks >= limit) {
        qCCritical(lcMedia) << m_consecutiveBadTracks << "unplayable tracks in a row; stopping playback";
        m_consecutiveBadTracks = 0;
        m_player->stop();
        m_player->setSource(QUrl());   // no more status/error events for it
        m_isPlaying = false;
        m_isPaused = true;
        emit playStateChanged(false);
        return;
    }
    if (m_isPlaying)
        next_track();
}

// ─── Metadata caching (TagLib) ─────────────────────────────────────

void MediaManager::_cache_metadata(const QString &filename)
{
    if (m_metadataCache.contains(filename))
        return;
    const QString filePath = _get_file_path(filename);
    if (filePath.isEmpty())
        return;
    MediaMetadata stored;
    if (_meta_from_store(filename, filePath, &stored)) {
        if (m_metadataCache.size() >= m_metadataCacheMax)
            m_metadataCache.erase(m_metadataCache.begin());
        m_metadataCache.insert(filename, stored);
        return;   // cover, if any, is found by get_album_art()'s hash fast path
    }
    _cache_metadata_from_file(filename);
    if (m_metadataCache.contains(filename))
        _meta_to_store(filename, filePath, m_metadataCache.value(filename));
}

// ---------------------------------------------------------------------------
// Persistent tag store
// ---------------------------------------------------------------------------
void MediaManager::_load_meta_store()
{
    QFile f(m_metaStorePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() == 1)
        m_metaStore = root.value(QStringLiteral("files")).toObject();
    qCInfo(lcMedia) << "Loaded tag store with" << m_metaStore.size() << "entries";
}

void MediaManager::_save_meta_store()
{
    if (!m_metaStoreDirty)
        return;
    m_metaStoreSaveTimer.stop();
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("files")] = m_metaStore;
    QFile f(m_metaStorePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && f.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) >= 0) {
        m_metaStoreDirty = false;   // only once the write succeeded: a failed one retries later
    } else {
        qCWarning(lcMedia) << "Could not write tag store:" << f.errorString();
    }
}

bool MediaManager::_meta_from_store(const QString &filename, const QString &filePath, MediaMetadata *out)
{
    if (!m_metaStore.contains(filename))
        return false;
    const QJsonValue v = m_metaStore.value(filename);
    const QJsonObject e = v.toObject();
    const QFileInfo fi(filePath);
    if (!v.isObject() || !fi.exists() || e.value(QStringLiteral("size")).toDouble() != double(fi.size())
        || qint64(e.value(QStringLiteral("mtime")).toDouble()) != fi.lastModified().toSecsSinceEpoch()) {
        // Deleted, renamed or rewritten track: drop the entry so the store
        // stays bounded by the current library, not by everything ever seen.
        m_metaStore.remove(filename);
        m_metaStoreDirty = true;
        m_metaStoreSaveTimer.start();
        return false;
    }
    out->title = e.value(QStringLiteral("title")).toString();
    out->artist = e.value(QStringLiteral("artist")).toString();
    out->album = e.value(QStringLiteral("album")).toString();
    out->durationSeconds = e.value(QStringLiteral("duration")).toInt();
    return true;
}

void MediaManager::_meta_to_store(const QString &filename, const QString &filePath, const MediaMetadata &meta)
{
    const QFileInfo fi(filePath);
    if (!fi.exists())
        return;
    QJsonObject e;
    e[QStringLiteral("title")] = meta.title;
    e[QStringLiteral("artist")] = meta.artist;
    e[QStringLiteral("album")] = meta.album;
    e[QStringLiteral("duration")] = meta.durationSeconds;
    e[QStringLiteral("size")] = double(fi.size());
    e[QStringLiteral("mtime")] = double(fi.lastModified().toSecsSinceEpoch());
    m_metaStore[filename] = e;
    m_metaStoreDirty = true;
    m_metaStoreSaveTimer.start();
}

QString MediaManager::_cached_art_url_for(const QString &albumId) const
{
    const QByteArray hash = QCryptographicHash::hash((albumId + QStringLiteral("_0")).toUtf8(),
                                                     QCryptographicHash::Sha256).toHex().left(16);
    for (const char *ext : {"jpg", "png", "gif", "img"}) {
        const QString path = m_tempDir + QStringLiteral("/cover_") + QString::fromLatin1(hash)
                             + QLatin1Char('.') + QLatin1String(ext);
        if (QFile::exists(path))
            return QUrl::fromLocalFile(path).toString();
    }
    return {};
}

QString MediaManager::_extract_art_for(const QString &filePath, const QString &albumId)
{
    // Pure with respect to the caches: reads the file, may write one cover
    // into m_tempDir. Safe to call from a worker thread.
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QStringLiteral("mp3"))
        return _extract_album_art_mp3(filePath, albumId);
    if (ext == QStringLiteral("m4a") || ext == QStringLiteral("mp4") || ext == QStringLiteral("aac"))
        return _extract_album_art_mp4(filePath, albumId);
    if (ext == QStringLiteral("flac"))
        return _extract_album_art_flac(filePath, albumId);
    if (ext == QStringLiteral("ogg") || ext == QStringLiteral("opus"))
        return _extract_album_art_ogg(filePath, albumId);
    return {};
}

void MediaManager::_cache_metadata_from_file(const QString &filename)
{
    if (m_metadataCache.contains(filename))
        return;

    const QString filePath = _get_file_path(filename);
    if (filePath.isEmpty())
        return;

    const QString displayName = _get_original_filename(filename);
    const QString baseName = QFileInfo(displayName).completeBaseName();

    // Manage cache size
    if (m_metadataCache.size() >= m_metadataCacheMax) {
        m_metadataCache.erase(m_metadataCache.begin());
    }

    const QString ext = QFileInfo(filePath).suffix().toLower();
    Q_UNUSED(ext);

#ifdef Q_OS_ANDROID
    // Android: use MediaMetadataRetriever via JNI (no TagLib cross-compile).
    // Returns "title\n<t>\nartist\n<a>\nalbum\n<al>\nduration\n<secs>".
    QString meta = OctaveAndroid::readMetadata(filePath);
    QString title, artist, album;
    int duration = 0;
    if (!meta.isEmpty()) {
        const QStringList lines = meta.split(QLatin1Char('\n'));
        for (int i = 0; i + 1 < lines.size(); i += 2) {
            const QString &k = lines.at(i);
            const QString &v = lines.at(i + 1);
            if      (k == QLatin1String("title"))    title    = v;
            else if (k == QLatin1String("artist"))   artist   = v;
            else if (k == QLatin1String("album"))    album    = v;
            else if (k == QLatin1String("duration")) duration = v.toInt();
        }
    }
    if (title.isEmpty())  title  = baseName;
    if (artist.isEmpty()) artist = QStringLiteral("Unknown Artist");
    if (album.isEmpty())  album  = QStringLiteral("Unknown Album");
    m_metadataCache.insert(filename, {title, artist, album, duration});

    // Album art: extract embedded picture to the temp dir.
    const QString albumId = _get_album_id(filename);
    if (!m_albumArtCache.contains(albumId)) {
        _manage_cache(albumId);
        m_accessCount[albumId] = m_accessCount.value(albumId, 0) + 1;

        const QByteArray cacheKey = (albumId + QStringLiteral("_0")).toUtf8();
        const QByteArray hash = QCryptographicHash::hash(cacheKey, QCryptographicHash::Sha256).toHex().left(16);
        const QString tempPath = m_tempDir + QStringLiteral("/cover_")
                                + QString::fromLatin1(hash) + QStringLiteral(".jpg");
        if (!QFile::exists(tempPath) && OctaveAndroid::extractAlbumArt(filePath, tempPath)) {
            m_albumArtCache.insert(albumId, QUrl::fromLocalFile(tempPath).toString());
        } else if (QFile::exists(tempPath)) {
            m_albumArtCache.insert(albumId, QUrl::fromLocalFile(tempPath).toString());
        }
    }
    return;
#elif defined(Q_OS_MOBILE)
    // Non-Android mobile (iOS, future): no TagLib yet. Filename-only fallback.
    m_metadataCache.insert(filename, {baseName, QStringLiteral("Unknown Artist"),
                                      QStringLiteral("Unknown Album"), 0});
    return;
#else
    m_metadataCache.insert(filename, _read_tags(filePath, baseName));

    // Also extract album art from the same file read
    const QString albumId = _get_album_id(filename);
    if (!m_albumArtCache.contains(albumId)) {
        _manage_cache(albumId);
        m_accessCount[albumId] = m_accessCount.value(albumId, 0) + 1;

        QString artUrl = _cached_art_url_for(albumId);
        if (artUrl.isEmpty())
            artUrl = _extract_art_for(filePath, albumId);
        if (!artUrl.isEmpty()) {
            m_albumArtCache.insert(albumId, artUrl);
        }
    }
#endif // Q_OS_MOBILE
}

#if !defined(Q_OS_MOBILE)
MediaMetadata MediaManager::_read_tags(const QString &filePath, const QString &baseName)
{
    // Pure TagLib read; safe on a worker thread.
    TagLib::FileRef fileRef(filePath.toUtf8().constData());
    if (fileRef.isNull() || !fileRef.tag())
        return {baseName, QStringLiteral("Unknown Artist"), QStringLiteral("Unknown Album"), 0};

    TagLib::Tag *tag = fileRef.tag();
    TagLib::AudioProperties *props = fileRef.audioProperties();

    QString artist = QString::fromStdString(tag->artist().to8Bit(true));
    QString album = QString::fromStdString(tag->album().to8Bit(true));
    QString title = QString::fromStdString(tag->title().to8Bit(true));
    const int duration = props ? props->lengthInSeconds() : 0;

    if (artist.isEmpty()) artist = QStringLiteral("Unknown Artist");
    if (album.isEmpty()) album = QStringLiteral("Unknown Album");
    if (title.isEmpty()) title = baseName;

    return {_sanitize_metadata(title), _sanitize_metadata(artist), _sanitize_metadata(album), duration};
}
#endif

void MediaManager::_emit_metadata(const QString &filename)
{
    if (!m_metadataCache.contains(filename))
        _cache_metadata(filename);

    const MediaMetadata &meta = m_metadataCache[filename];
    emit metadataChanged(meta.title, meta.artist, meta.album);
}

QString MediaManager::_get_album_id(const QString &filename)
{
    if (!m_metadataCache.contains(filename))
        _cache_metadata(filename);

    if (m_metadataCache.contains(filename)) {
        const MediaMetadata &meta = m_metadataCache[filename];
        // When album is unknown (common for YouTube downloads), each track
        // likely has its own unique cover art, so include the filename
        // to avoid all "Unknown Album" tracks sharing one cached image.
        if (meta.album == QStringLiteral("Unknown Album"))
            return meta.album + QStringLiteral("_") + meta.artist + QStringLiteral("_") + filename;
        return meta.album + QStringLiteral("_") + meta.artist;
    }

    // Fallback: SHA256 hash of filename
    QByteArray hash = QCryptographicHash::hash(filename.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex().left(16));
}

void MediaManager::_manage_cache(const QString &newAlbumId)
{
    if (m_albumArtCache.size() < m_maxCacheFiles)
        return;

    // Find least recently accessed item
    QString leastUsed;
    int leastCount = INT_MAX;
    for (auto it = m_accessCount.constBegin(); it != m_accessCount.constEnd(); ++it) {
        if (it.key() != newAlbumId && it.value() < leastCount) {
            leastCount = it.value();
            leastUsed = it.key();
        }
    }

    if (leastUsed.isEmpty())
        return;

    // Remove cached file from disk
    if (m_albumArtCache.contains(leastUsed)) {
        QString url = m_albumArtCache.value(leastUsed);
        QString filePath = QUrl(url).toLocalFile();
        if (!filePath.isEmpty())
            QFile::remove(filePath);
        m_albumArtCache.remove(leastUsed);
    }
    m_accessCount.remove(leastUsed);

    qCInfo(lcMedia) << "Cache managed. New size:" << m_albumArtCache.size();
}

// ─── Album art extraction via TagLib ───────────────────────────────
// On mobile (Q_OS_MOBILE), all four extractors return empty — TagLib isn't linked.

#ifdef Q_OS_MOBILE
QString MediaManager::_extract_album_art_mp3(const QString &, const QString &) { return {}; }
QString MediaManager::_extract_album_art_mp4(const QString &, const QString &) { return {}; }
QString MediaManager::_extract_album_art_flac(const QString &, const QString &) { return {}; }
QString MediaManager::_extract_album_art_ogg(const QString &, const QString &) { return {}; }
#else

QString MediaManager::_extract_album_art_mp3(const QString &filePath, const QString &albumId)
{
    TagLib::MPEG::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return {};

    TagLib::ID3v2::Tag *id3v2 = file.ID3v2Tag();
    if (!id3v2)
        return {};

    TagLib::ID3v2::FrameList frames = id3v2->frameListMap()["APIC"];
    if (frames.isEmpty())
        return {};

    auto *frame = static_cast<TagLib::ID3v2::AttachedPictureFrame *>(frames.front());
    if (!frame || frame->picture().isEmpty())
        return {};

    // Determine extension from MIME
    QString mime = QString::fromStdString(frame->mimeType().to8Bit(false)).toLower();
    QString ext = QStringLiteral("jpg");
    if (mime.contains(QStringLiteral("png")))
        ext = QStringLiteral("png");
    else if (mime.contains(QStringLiteral("gif")))
        ext = QStringLiteral("gif");

    QByteArray cacheKey = (albumId + QStringLiteral("_0")).toUtf8();
    QByteArray hash = QCryptographicHash::hash(cacheKey, QCryptographicHash::Sha256).toHex().left(16);
    QString tempPath = m_tempDir + QStringLiteral("/cover_") + QString::fromLatin1(hash) + QStringLiteral(".") + ext;

    if (!QFile::exists(tempPath)) {
        QFile out(tempPath);
        if (out.open(QIODevice::WriteOnly)) {
            const TagLib::ByteVector &data = frame->picture();
            out.write(data.data(), data.size());
        }
    }

    return QUrl::fromLocalFile(tempPath).toString();
}

QString MediaManager::_extract_album_art_mp4(const QString &filePath, const QString &albumId)
{
    TagLib::MP4::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return {};

    TagLib::MP4::Tag *tag = file.tag();
    if (!tag)
        return {};

    TagLib::MP4::ItemMap items = tag->itemMap();
    if (!items.contains("covr"))
        return {};

    TagLib::MP4::CoverArtList covers = items["covr"].toCoverArtList();
    if (covers.isEmpty())
        return {};

    const TagLib::MP4::CoverArt &cover = covers.front();
    QString ext = (cover.format() == TagLib::MP4::CoverArt::PNG)
                      ? QStringLiteral("png")
                      : QStringLiteral("jpg");

    QByteArray cacheKey = (albumId + QStringLiteral("_0")).toUtf8();
    QByteArray hash = QCryptographicHash::hash(cacheKey, QCryptographicHash::Sha256).toHex().left(16);
    QString tempPath = m_tempDir + QStringLiteral("/cover_") + QString::fromLatin1(hash) + QStringLiteral(".") + ext;

    if (!QFile::exists(tempPath)) {
        QFile out(tempPath);
        if (out.open(QIODevice::WriteOnly)) {
            const TagLib::ByteVector &data = cover.data();
            out.write(data.data(), data.size());
        }
    }

    return QUrl::fromLocalFile(tempPath).toString();
}

QString MediaManager::_extract_album_art_flac(const QString &filePath, const QString &albumId)
{
    TagLib::FLAC::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return {};

    const TagLib::List<TagLib::FLAC::Picture *> &pics = file.pictureList();
    if (pics.isEmpty())
        return {};

    TagLib::FLAC::Picture *pic = pics.front();
    if (!pic || pic->data().isEmpty())
        return {};

    QString mime = QString::fromStdString(pic->mimeType().to8Bit(false)).toLower();
    QString ext = QStringLiteral("jpg");
    if (mime.contains(QStringLiteral("png")))
        ext = QStringLiteral("png");

    QByteArray cacheKey = (albumId + QStringLiteral("_0")).toUtf8();
    QByteArray hash = QCryptographicHash::hash(cacheKey, QCryptographicHash::Sha256).toHex().left(16);
    QString tempPath = m_tempDir + QStringLiteral("/cover_") + QString::fromLatin1(hash) + QStringLiteral(".") + ext;

    if (!QFile::exists(tempPath)) {
        QFile out(tempPath);
        if (out.open(QIODevice::WriteOnly)) {
            const TagLib::ByteVector &data = pic->data();
            out.write(data.data(), data.size());
        }
    }

    return QUrl::fromLocalFile(tempPath).toString();
}

QString MediaManager::_extract_album_art_ogg(const QString &filePath, const QString &albumId)
{
    TagLib::Ogg::Vorbis::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return {};

    TagLib::Ogg::XiphComment *xiph = file.tag();
    if (!xiph)
        return {};

    const TagLib::List<TagLib::FLAC::Picture *> &pics = xiph->pictureList();
    if (pics.isEmpty())
        return {};

    TagLib::FLAC::Picture *pic = pics.front();
    if (!pic || pic->data().isEmpty())
        return {};

    QString mime = QString::fromStdString(pic->mimeType().to8Bit(false)).toLower();
    QString ext = QStringLiteral("jpg");
    if (mime.contains(QStringLiteral("png")))
        ext = QStringLiteral("png");

    QByteArray cacheKey = (albumId + QStringLiteral("_0")).toUtf8();
    QByteArray hash = QCryptographicHash::hash(cacheKey, QCryptographicHash::Sha256).toHex().left(16);
    QString tempPath = m_tempDir + QStringLiteral("/cover_") + QString::fromLatin1(hash) + QStringLiteral(".") + ext;

    if (!QFile::exists(tempPath)) {
        QFile out(tempPath);
        if (out.open(QIODevice::WriteOnly)) {
            const TagLib::ByteVector &data = pic->data();
            out.write(data.data(), data.size());
        }
    }

    return QUrl::fromLocalFile(tempPath).toString();
}

#endif // Q_OS_MOBILE

// ─── Album art public API ──────────────────────────────────────────

QString MediaManager::get_album_art(const QString &filename)
{
    if (filename.isEmpty())
        return {};

    const QString albumId = _get_album_id(filename);
    m_accessCount[albumId] = m_accessCount.value(albumId, 0) + 1;

    // Return from cache if available
    if (m_albumArtCache.contains(albumId)) {
        return m_albumArtCache.value(albumId);
    }

    // Manage cache before adding
    _manage_cache(albumId);

    // A cover extracted on an earlier run is found by its hash with no tag parse
    QString artUrl = _cached_art_url_for(albumId);
    if (artUrl.isEmpty()) {
        const QString filePath = _get_file_path(filename);
        if (filePath.isEmpty() || !QFile::exists(filePath))
            return {};
        artUrl = _extract_art_for(filePath, albumId);
    }

    if (!artUrl.isEmpty()) {
        m_albumArtCache.insert(albumId, artUrl);
    }

    return artUrl;
}

// ─── Color extraction (k-means) ────────────────────────────────────

QString MediaManager::_extract_album_colors(const QString &imagePath)
{
    QImage img(imagePath);
    if (img.isNull())
        return {};

    img = img.convertToFormat(QImage::Format_RGBA8888);
    img = img.scaled(50, 50, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const uchar *bits = img.constBits();
    const int totalBytes = img.sizeInBytes();

    QVector<QVector<int>> pixels;
    pixels.reserve(totalBytes / 4);
    for (int i = 0; i < totalBytes; i += 4) {
        pixels.append({bits[i], bits[i + 1], bits[i + 2]});
    }

    QVector<QVector<int>> colors = _kmeans_colors(pixels, 5);

    // Sort by vibrancy (saturation * value)
    auto colorVibrancy = [](const QVector<int> &rgb) -> double {
        double r = rgb[0] / 255.0, g = rgb[1] / 255.0, b = rgb[2] / 255.0;
        double maxC = std::max({r, g, b});
        double minC = std::min({r, g, b});
        double s = (maxC == 0.0) ? 0.0 : (maxC - minC) / maxC;
        return s * maxC; // saturation * value
    };
    std::sort(colors.begin(), colors.end(), [&](const QVector<int> &a, const QVector<int> &b) {
        return colorVibrancy(a) > colorVibrancy(b);
    });

    // Calculate average luminance to determine dark/light
    auto luminance = [](const QVector<int> &rgb) -> double {
        return 0.299 * (rgb[0] / 255.0) + 0.587 * (rgb[1] / 255.0) + 0.114 * (rgb[2] / 255.0);
    };
    int sampleCount = std::min(static_cast<int>(pixels.size()), 1000);
    double avgLum = 0.0;
    for (int i = 0; i < sampleCount; ++i)
        avgLum += luminance(pixels[i]);
    avgLum /= sampleCount;
    bool isDark = avgLum < 0.5;

    return _generate_theme_from_colors(colors, isDark);
}

QVector<QVector<int>> MediaManager::_kmeans_colors(const QVector<QVector<int>> &pixels, int k, int maxIterations)
{
    const int n = pixels.size();
    if (n == 0)
        return {};

    k = std::min(k, n);

    // Seed with deterministic random
    std::mt19937 rng(42);

    // Select initial centroids randomly
    QVector<int> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);

    QVector<QVector<double>> centroids(k);
    for (int j = 0; j < k; ++j) {
        const QVector<int> &px = pixels[indices[j]];
        centroids[j] = {static_cast<double>(px[0]), static_cast<double>(px[1]), static_cast<double>(px[2])};
    }

    QVector<int> labels(n, 0);

    for (int iter = 0; iter < maxIterations; ++iter) {
        // Assign each pixel to nearest centroid
        for (int i = 0; i < n; ++i) {
            double minDist = std::numeric_limits<double>::max();
            for (int j = 0; j < k; ++j) {
                double dr = pixels[i][0] - centroids[j][0];
                double dg = pixels[i][1] - centroids[j][1];
                double db = pixels[i][2] - centroids[j][2];
                double d = dr * dr + dg * dg + db * db;
                if (d < minDist) {
                    minDist = d;
                    labels[i] = j;
                }
            }
        }

        // Update centroids
        bool converged = true;
        QVector<QVector<double>> newCentroids(k);
        for (int j = 0; j < k; ++j) {
            double sr = 0, sg = 0, sb = 0;
            int cnt = 0;
            for (int i = 0; i < n; ++i) {
                if (labels[i] == j) {
                    sr += pixels[i][0];
                    sg += pixels[i][1];
                    sb += pixels[i][2];
                    ++cnt;
                }
            }
            if (cnt > 0) {
                newCentroids[j] = {sr / cnt, sg / cnt, sb / cnt};
            } else {
                newCentroids[j] = centroids[j];
            }

            if (std::abs(newCentroids[j][0] - centroids[j][0]) > 1.0
                || std::abs(newCentroids[j][1] - centroids[j][1]) > 1.0
                || std::abs(newCentroids[j][2] - centroids[j][2]) > 1.0) {
                converged = false;
            }
        }

        centroids = newCentroids;
        if (converged)
            break;
    }

    QVector<QVector<int>> result(k);
    for (int j = 0; j < k; ++j) {
        result[j] = {static_cast<int>(centroids[j][0]),
                     static_cast<int>(centroids[j][1]),
                     static_cast<int>(centroids[j][2])};
    }
    return result;
}

// ─── Theme generation from extracted colors ────────────────────────

QString MediaManager::_generate_theme_from_colors(const QVector<QVector<int>> &colors, bool isDark)
{
    if (colors.isEmpty())
        return {};

    // ── Helpers (lambdas) ──────────────────────────────────────

    auto rgbToHex = [](const QVector<int> &rgb) -> QString {
        return QStringLiteral("#%1%2%3")
            .arg(qBound(0, rgb[0], 255), 2, 16, QLatin1Char('0'))
            .arg(qBound(0, rgb[1], 255), 2, 16, QLatin1Char('0'))
            .arg(qBound(0, rgb[2], 255), 2, 16, QLatin1Char('0'));
    };

    // Convert RGB [0-255] to HSV where H [0-360], S [0-1], V [0-1]
    auto rgbToHsv = [](double r, double g, double b, double &h, double &s, double &v) {
        r /= 255.0; g /= 255.0; b /= 255.0;
        double maxC = std::max({r, g, b});
        double minC = std::min({r, g, b});
        double delta = maxC - minC;
        v = maxC;
        s = (maxC == 0.0) ? 0.0 : delta / maxC;
        if (delta == 0.0) {
            h = 0.0;
        } else if (maxC == r) {
            h = 60.0 * std::fmod((g - b) / delta, 6.0);
        } else if (maxC == g) {
            h = 60.0 * ((b - r) / delta + 2.0);
        } else {
            h = 60.0 * ((r - g) / delta + 4.0);
        }
        if (h < 0.0) h += 360.0;
    };

    auto hsvToRgb = [](double h, double s, double v) -> QVector<int> {
        double c = v * s;
        double x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
        double m = v - c;
        double r1 = 0, g1 = 0, b1 = 0;
        if (h < 60)       { r1 = c; g1 = x; }
        else if (h < 120) { r1 = x; g1 = c; }
        else if (h < 180) { g1 = c; b1 = x; }
        else if (h < 240) { g1 = x; b1 = c; }
        else if (h < 300) { r1 = x; b1 = c; }
        else              { r1 = c; b1 = x; }
        return {static_cast<int>((r1 + m) * 255),
                static_cast<int>((g1 + m) * 255),
                static_cast<int>((b1 + m) * 255)};
    };

    auto adjustBrightness = [&](const QVector<int> &rgb, double factor) -> QVector<int> {
        double h, s, v;
        rgbToHsv(rgb[0], rgb[1], rgb[2], h, s, v);
        v = qBound(0.0, v * factor, 1.0);
        return hsvToRgb(h, s, v);
    };

    auto adjustSaturation = [&](const QVector<int> &rgb, double factor) -> QVector<int> {
        double h, s, v;
        rgbToHsv(rgb[0], rgb[1], rgb[2], h, s, v);
        s = qBound(0.0, s * factor, 1.0);
        return hsvToRgb(h, s, v);
    };

    auto getLuminance = [](const QVector<int> &rgb) -> double {
        auto ch = [](int c) -> double {
            double f = c / 255.0;
            return (f <= 0.03928) ? f / 12.92 : std::pow((f + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * ch(rgb[0]) + 0.7152 * ch(rgb[1]) + 0.0722 * ch(rgb[2]);
    };

    auto getContrastRatio = [&](const QVector<int> &a, const QVector<int> &b) -> double {
        double l1 = getLuminance(a);
        double l2 = getLuminance(b);
        double lighter = std::max(l1, l2);
        double darker = std::min(l1, l2);
        return (lighter + 0.05) / (darker + 0.05);
    };

    auto getReadableTextColor = [&](const QVector<int> &bg) -> QVector<int> {
        double lum = getLuminance(bg);
        return (lum < 0.3) ? QVector<int>{245, 245, 245} : QVector<int>{25, 25, 25};
    };

    auto getSecondaryTextColor = [&](const QVector<int> &bg) -> QVector<int> {
        double lum = getLuminance(bg);
        return (lum < 0.3) ? QVector<int>{210, 210, 210} : QVector<int>{60, 60, 60};
    };

    auto capBrightness = [&](const QVector<int> &rgb, double maxBr = 0.85) -> QVector<int> {
        double h, s, v;
        rgbToHsv(rgb[0], rgb[1], rgb[2], h, s, v);
        if (v > maxBr) {
            v = maxBr;
            s = std::min(1.0, s * 1.1);
        }
        return hsvToRgb(h, s, v);
    };

    auto ensureIconContrast = [&](const QVector<int> &iconRgb, const QVector<int> &bgRgb, double minContrast = 3.0) -> QVector<int> {
        double contrast = getContrastRatio(iconRgb, bgRgb);
        if (contrast >= minContrast)
            return capBrightness(iconRgb);

        double bgLum = getLuminance(bgRgb);
        if (bgLum < 0.5) {
            // Dark background - brighten
            for (double factor : {1.3, 1.5, 1.8, 2.0, 2.5, 3.0}) {
                QVector<int> adjusted = adjustBrightness(iconRgb, factor);
                if (getContrastRatio(adjusted, bgRgb) >= minContrast)
                    return capBrightness(adjusted);
            }
            return {200, 180, 130}; // Fallback muted gold
        } else {
            // Light background - darken
            for (double factor : {0.7, 0.5, 0.4, 0.3, 0.2}) {
                QVector<int> adjusted = adjustBrightness(iconRgb, factor);
                if (getContrastRatio(adjusted, bgRgb) >= minContrast)
                    return adjusted;
            }
            return {50, 40, 30}; // Fallback dark
        }
    };

    // ── Extract palette colors ────────────────────────────────

    const QVector<int> &primaryRgb = colors[0];
    const QVector<int> &secondaryRgb = (colors.size() > 1) ? colors[1] : colors[0];
    const QVector<int> &tertiaryRgb = (colors.size() > 2) ? colors[2] : secondaryRgb;
    const QVector<int> &quaternaryRgb = (colors.size() > 3) ? colors[3] : tertiaryRgb;

    // Accent colors with saturation/brightness adjustments
    QVector<int> accent = adjustBrightness(adjustSaturation(primaryRgb, 1.4), 1.2);
    QVector<int> accent2 = adjustBrightness(adjustSaturation(secondaryRgb, 1.3), 1.15);
    QVector<int> accent3 = adjustBrightness(adjustSaturation(tertiaryRgb, 1.25), 1.1);
    QVector<int> accent4 = adjustBrightness(adjustSaturation(quaternaryRgb, 1.2), 1.05);

    QVector<int> navColor = adjustBrightness(accent, 0.7);
    QVector<int> navColor2 = adjustBrightness(accent2, 0.75);

    // ── Build theme palette ───────────────────────────────────

    QVector<int> base, baseAlt, hover, paused, playing;
    if (isDark) {
        base = adjustBrightness(primaryRgb, 0.15);
        baseAlt = adjustBrightness(primaryRgb, 0.22);
        hover = adjustBrightness(primaryRgb, 0.30);
        paused = adjustBrightness(primaryRgb, 0.25);
        playing = adjustBrightness(primaryRgb, 0.35);
    } else {
        base = adjustSaturation(adjustBrightness(primaryRgb, 2.5), 0.3);
        baseAlt = adjustBrightness(base, 0.92);
        hover = adjustBrightness(base, 0.88);
        paused = adjustBrightness(base, 0.85);
        playing = adjustBrightness(base, 0.80);
    }

    QVector<int> textPrimary = getReadableTextColor(base);
    QVector<int> textSecondary = getSecondaryTextColor(base);

    QVector<int> accentVis = ensureIconContrast(accent, base);
    QVector<int> accent2Vis = ensureIconContrast(accent2, base);
    QVector<int> accent3Vis = ensureIconContrast(accent3, base);
    QVector<int> accent4Vis = ensureIconContrast(accent4, base);
    QVector<int> navVis = ensureIconContrast(navColor, base);
    QVector<int> navVis2 = ensureIconContrast(navColor2, base);

    // ── Build JSON ────────────────────────────────────────────

    QJsonObject text;
    text[QStringLiteral("primary")] = rgbToHex(textPrimary);
    text[QStringLiteral("secondary")] = rgbToHex(textSecondary);

    QJsonObject states;
    states[QStringLiteral("hover")] = rgbToHex(hover);
    states[QStringLiteral("paused")] = rgbToHex(paused);
    states[QStringLiteral("playing")] = rgbToHex(playing);

    QJsonObject sliders;
    sliders[QStringLiteral("volume")] = rgbToHex(accentVis);
    sliders[QStringLiteral("media")] = rgbToHex(accent2Vis);
    sliders[QStringLiteral("settings")] = rgbToHex(accent3Vis);

    QJsonObject bottombar;
    bottombar[QStringLiteral("previous")] = rgbToHex(navVis);
    bottombar[QStringLiteral("play")] = rgbToHex(accentVis);
    bottombar[QStringLiteral("pause")] = rgbToHex(accentVis);
    bottombar[QStringLiteral("next")] = rgbToHex(navVis);
    bottombar[QStringLiteral("volume")] = rgbToHex(accent2Vis);
    bottombar[QStringLiteral("shuffle")] = rgbToHex(accent3Vis);
    bottombar[QStringLiteral("toggleShade")] = rgbToHex(hover);
    bottombar[QStringLiteral("homeButton")] = rgbToHex(accentVis);
    bottombar[QStringLiteral("obdButton")] = rgbToHex(accent4Vis);
    bottombar[QStringLiteral("mediaButton")] = rgbToHex(accent2Vis);
    bottombar[QStringLiteral("settingsButton")] = rgbToHex(accent3Vis);
    bottombar[QStringLiteral("sensorButton")] = rgbToHex(accentVis);
    bottombar[QStringLiteral("androidAutoButton")] = rgbToHex(navVis2);
    bottombar[QStringLiteral("phoneMirrorButton")] = rgbToHex(accent4Vis);

    QVector<int> mediaPlayVis = ensureIconContrast(adjustBrightness(accent, 1.1), base);
    QJsonObject mediaroom;
    mediaroom[QStringLiteral("previous")] = rgbToHex(navVis);
    mediaroom[QStringLiteral("play")] = rgbToHex(mediaPlayVis);
    mediaroom[QStringLiteral("pause")] = rgbToHex(mediaPlayVis);
    mediaroom[QStringLiteral("next")] = rgbToHex(navVis);
    mediaroom[QStringLiteral("left")] = rgbToHex(accent4Vis);
    mediaroom[QStringLiteral("right")] = rgbToHex(accent4Vis);
    mediaroom[QStringLiteral("shuffle")] = rgbToHex(accent3Vis);
    mediaroom[QStringLiteral("toggleShade")] = rgbToHex(paused);

    QJsonObject mainmenu;
    mainmenu[QStringLiteral("mediaContainer")] = rgbToHex(adjustBrightness(accent2Vis, 0.6));

    QJsonObject obd;
    obd[QStringLiteral("boxBackground")] = rgbToHex(baseAlt);
    obd[QStringLiteral("barColor")] = rgbToHex(ensureIconContrast(accent4, baseAlt));
    obd[QStringLiteral("labelColor")] = rgbToHex(getSecondaryTextColor(baseAlt));
    obd[QStringLiteral("valueColor")] = rgbToHex(getReadableTextColor(baseAlt));

    QJsonObject theme;
    theme[QStringLiteral("base")] = rgbToHex(base);
    theme[QStringLiteral("baseAlt")] = rgbToHex(baseAlt);
    theme[QStringLiteral("accent")] = rgbToHex(accentVis);
    theme[QStringLiteral("text")] = text;
    theme[QStringLiteral("states")] = states;
    theme[QStringLiteral("sliders")] = sliders;
    theme[QStringLiteral("bottombar")] = bottombar;
    theme[QStringLiteral("mediaroom")] = mediaroom;
    theme[QStringLiteral("mainmenu")] = mainmenu;
    theme[QStringLiteral("obd")] = obd;

    return QString::fromUtf8(QJsonDocument(theme).toJson(QJsonDocument::Compact));
}

// ─── Extract colors from album art (public slot) ───────────────────

void MediaManager::extract_colors_from_album_art(const QString &filename)
{
    qCDebug(lcMedia) << "[AlbumArtCapture] extract_colors_from_album_art called with:" << filename;

    const QString artUrl = get_album_art(filename);
    if (artUrl.isEmpty()) {
        qCDebug(lcMedia) << "[AlbumArtCapture] No album art found for:" << filename;
        return;
    }

    const QString imagePath = QUrl(artUrl).toLocalFile();
    if (imagePath.isEmpty() || !QFile::exists(imagePath)) {
        qCDebug(lcMedia) << "[AlbumArtCapture] Album art file not found:" << imagePath;
        return;
    }

    // Restoring playback and the 1 s startup timer both ask for the same file;
    // one extraction is enough.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (filename == m_lastColorFile && now - m_lastColorMs < 2000)
        return;
    m_lastColorFile = filename;
    m_lastColorMs = now;

    // k-means over the cover runs on a worker; a newer request supersedes
    // this one so a fast skip through tracks applies only the last theme.
    const int job = ++m_colorJob;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, job, filename]() {
        watcher->deleteLater();
        if (job != m_colorJob)
            return;
        _apply_album_colors(watcher->result(), filename);
    });
    watcher->setFuture(QtConcurrent::run([this, imagePath]() { return _extract_album_colors(imagePath); }));
}

void MediaManager::_apply_album_colors(const QString &themeJson, const QString &filename)
{
    if (themeJson.isEmpty())
        return;
    if (m_settingsManager) {
        m_settingsManager->set_album_art_colors(themeJson);
        qCDebug(lcMedia) << "[AlbumArtCapture] Called set_album_art_colors for:" << filename;
    } else {
        emit albumColorsExtracted(themeJson);
        qCDebug(lcMedia) << "[AlbumArtCapture] Emitted via mediaManager for:" << filename;
    }
}

// ─── Theme change handler ──────────────────────────────────────────

void MediaManager::_on_theme_changed(const QString &theme)
{
    m_albumArtCaptureActive = (theme == QStringLiteral("Album Art Capture"));
    if (m_albumArtCaptureActive) {
        const QString currentFile = get_current_file();
        if (!currentFile.isEmpty())
            extract_colors_from_album_art(currentFile);
    }
}

void MediaManager::_on_media_changed_for_theme(const QString &filename)
{
    if (m_albumArtCaptureActive && !filename.isEmpty()) {
        qCDebug(lcMedia) << "[AlbumArtCapture] Media changed, extracting colors for:" << filename;
        extract_colors_from_album_art(filename);
    }
}

void MediaManager::_extract_colors_on_startup()
{
    if (m_albumArtCaptureActive) {
        const QString currentFile = get_current_file();
        if (!currentFile.isEmpty())
            extract_colors_from_album_art(currentFile);
    }
}

// ─── Media file listing ────────────────────────────────────────────

QStringList MediaManager::get_media_files(bool emitSignal)
{
    QStringList files;
    QDir dir(m_mediaDir);
    if (!dir.exists())
        return files;

    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &fi : entries) {
        if (hasAudioExtension(fi.fileName()))
            files.append(fi.fileName());
    }

    if (emitSignal)
        emit mediaListChanged(files);

    return files;
}

// ─── Metadata getters ──────────────────────────────────────────────

QString MediaManager::get_formatted_duration(const QString &filename)
{
    if (!m_metadataCache.contains(filename))
        _cache_metadata(filename);

    if (!m_metadataCache.contains(filename))
        return QStringLiteral("0:00");

    int secs = m_metadataCache[filename].durationSeconds;
    int m = secs / 60;
    int s = secs % 60;
    QString formatted = QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
    emit durationFormatChanged(formatted);
    return formatted;
}

QString MediaManager::get_band(const QString &filename)
{
    if (!m_metadataCache.contains(filename))
        _cache_metadata(filename);
    return m_metadataCache.contains(filename) ? m_metadataCache[filename].artist
                                               : QStringLiteral("Unknown Artist");
}

QString MediaManager::get_album(const QString &filename)
{
    if (!m_metadataCache.contains(filename))
        _cache_metadata(filename);
    return m_metadataCache.contains(filename) ? m_metadataCache[filename].album
                                               : QStringLiteral("Unknown Album");
}

// ─── Current file / peek ───────────────────────────────────────────

QString MediaManager::get_current_file()
{
    // Initialize playlist if empty
    if (m_currentPlaylist.isEmpty()) {
        QStringList files = _get_current_playlist_files();
        if (!files.isEmpty()) {
            std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
                return cleanForSort(a) < cleanForSort(b);
            });
            m_currentPlaylist = files;
            m_currentIndex = 0;
            return m_currentPlaylist.first();
        }
    }

    if (m_currentIndex >= 0 && m_currentIndex < m_currentPlaylist.size())
        return m_currentPlaylist[m_currentIndex];

    // Fallback
    QStringList files = _get_current_playlist_files();
    if (!files.isEmpty()) {
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            return cleanForSort(a) < cleanForSort(b);
        });
        m_currentPlaylist = files;
        m_currentIndex = 0;
        return m_currentPlaylist.first();
    }

    return {};
}

QString MediaManager::get_next_track_name()
{
    if (m_currentPlaylist.isEmpty())
        return {};
    int next = (m_currentIndex + 1) % m_currentPlaylist.size();
    return m_currentPlaylist[next];
}

QString MediaManager::get_previous_track_name()
{
    if (m_currentPlaylist.isEmpty())
        return {};
    int prev = (m_currentIndex - 1 + m_currentPlaylist.size()) % m_currentPlaylist.size();
    return m_currentPlaylist[prev];
}

QString MediaManager::get_next_track_album_art()
{
    const QString next = get_next_track_name();
    return next.isEmpty() ? QString() : get_album_art(next);
}

QString MediaManager::get_previous_track_album_art()
{
    const QString prev = get_previous_track_name();
    return prev.isEmpty() ? QString() : get_album_art(prev);
}

QStringList MediaManager::get_neighbor_album_arts()
{
    const QString prev = get_previous_track_name();
    const QString next = get_next_track_name();
    return {
        prev.isEmpty() ? QString() : get_album_art(prev),
        next.isEmpty() ? QString() : get_album_art(next)
    };
}

// ─── Playback control ──────────────────────────────────────────────

void MediaManager::play_file(const QString &filename)
{
    qCInfo(lcMedia) << "play_file called with:" << filename;
    // Initialize current_playlist if needed
    if (m_currentPlaylist.isEmpty()) {
        QStringList files = _get_current_playlist_files();
        if (m_shuffle) {
            m_currentPlaylist = _shuffle_playlist();
        } else {
            std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
                return cleanForSort(a) < cleanForSort(b);
            });
            m_currentPlaylist = files;
        }
    }

    if (m_currentPlaylist.isEmpty()) {
        qCInfo(lcMedia) << "No media files available to play";
        return;
    }

    // Find the file in the playlist
    QString fileToPlay = filename;
    int idx = m_currentPlaylist.indexOf(filename);
    if (idx >= 0) {
        m_currentIndex = idx;
    } else if (!m_shuffle) {
        // Rebuild alphabetical playlist
        QStringList files = _get_current_playlist_files();
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            return cleanForSort(a) < cleanForSort(b);
        });
        m_currentPlaylist = files;
        idx = m_currentPlaylist.indexOf(filename);
        if (idx >= 0) {
            m_currentIndex = idx;
        } else {
            fileToPlay = m_currentPlaylist.isEmpty() ? QString() : m_currentPlaylist.first();
            m_currentIndex = 0;
        }
    } else {
        m_currentIndex = 0;
        fileToPlay = m_currentPlaylist.isEmpty() ? QString() : m_currentPlaylist.first();
    }

    if (fileToPlay.isEmpty())
        return;

    // Play the file
    const QString filePath = _get_file_path(fileToPlay);
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        qCInfo(lcMedia) << "File not found:" << filePath;
        return;
    }

    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_player->play();
    m_isPlaying = true;
    m_isPaused = false;

    // Restore mute state
    if (m_isMuted)
        m_audioOutput->setVolume(0.0f);

    emit playStateChanged(true);
    _emit_metadata(fileToPlay);
    emit currentMediaChanged(fileToPlay);
    get_formatted_duration(fileToPlay);

    // Schedule pre-cache for neighbors
    m_precacheTimer.start();

    qCInfo(lcMedia) << "Now playing:" << fileToPlay
                     << "from" << (m_shuffle ? "shuffled" : "alphabetical")
                     << "playlist at position" << m_currentIndex;
}

void MediaManager::next_track()
{
    if (m_currentPlaylist.isEmpty()) {
        QStringList files = _get_current_playlist_files();
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            return cleanForSort(a) < cleanForSort(b);
        });
        m_currentPlaylist = files;
    }

    if (m_currentPlaylist.isEmpty()) {
        qCInfo(lcMedia) << "No media files available";
        return;
    }

    m_currentIndex = (m_currentIndex + 1) % m_currentPlaylist.size();
    play_file(m_currentPlaylist[m_currentIndex]);
    m_saveStateTimer.start(); // debounced save
}

void MediaManager::previous_track()
{
    if (m_currentPlaylist.isEmpty()) {
        QStringList files = _get_current_playlist_files();
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            return cleanForSort(a) < cleanForSort(b);
        });
        m_currentPlaylist = files;
    }

    if (m_currentPlaylist.isEmpty()) {
        qCInfo(lcMedia) << "No media files available";
        return;
    }

    m_currentIndex = (m_currentIndex - 1 + m_currentPlaylist.size()) % m_currentPlaylist.size();
    play_file(m_currentPlaylist[m_currentIndex]);
    m_saveStateTimer.start(); // debounced save
}

void MediaManager::pause()
{
    m_player->pause();
    m_isPaused = true;
    m_isPlaying = false;
    emit playStateChanged(false);
    _save_playback_state_debounced();
    if (!m_isMuted && m_volume > 0.0f)
        m_previousVolume = m_volume;
}

void MediaManager::toggle_play()
{
    // Handle case when no source is set
    if (!m_player->source().isValid()) {
        const QString currentFile = get_current_file();
        if (!currentFile.isEmpty()) {
            play_file(currentFile);
            return;
        }
    }

    if (m_isPlaying) {
        m_player->pause();
        m_isPaused = true;
        m_isPlaying = false;
        _save_playback_state_debounced();
    } else {
        // Check if player is in a bad state before resuming
        QMediaPlayer::MediaStatus ms = m_player->mediaStatus();
        if (ms == QMediaPlayer::InvalidMedia
            || ms == QMediaPlayer::NoMedia
            || ms == QMediaPlayer::StalledMedia) {
            qCWarning(lcMedia) << "Player in bad state on resume:" << ms << "- recovering";
            _attempt_playback_recovery(true);
            return;
        }

        m_player->play();
        m_isPaused = false;
        m_isPlaying = true;

        // Verify player started
        if (m_player->playbackState() != QMediaPlayer::PlayingState) {
            qCWarning(lcMedia) << "Player did not start - recovering";
            _attempt_playback_recovery(true);
            return;
        }

        // Restore mute state
        if (m_isMuted)
            m_audioOutput->setVolume(0.0f);
    }
    emit playStateChanged(m_isPlaying);
}

// ─── State getters ─────────────────────────────────────────────────

bool MediaManager::is_playing() { return m_isPlaying; }
bool MediaManager::is_paused() { return m_isPaused; }
float MediaManager::get_duration() { return static_cast<float>(m_player->duration()); }
float MediaManager::get_position() { return static_cast<float>(m_player->position()); }
void MediaManager::set_position(int positionMs) { m_player->setPosition(positionMs); }

// ─── Mute ──────────────────────────────────────────────────────────

void MediaManager::toggle_mute()
{
    if (m_muteToggleLocked)
        return;
    m_muteToggleLocked = true;

    if (m_isMuted) {
        // Unmuting: m_volume tracked the slider while muted; fall back if it is 0
        if (m_volume <= 0.0f)
            m_volume = (m_previousVolume > 0.0f) ? m_previousVolume : 0.5f;
    } else if (m_volume > 0.0f) {
        m_previousVolume = m_volume;
    }

    m_isMuted = !m_isMuted;
    applyOutputVolume();
    emit muteChanged(m_isMuted);
    qCInfo(lcMedia) << "Mute toggled:" << m_isMuted;

    QTimer::singleShot(150, this, &MediaManager::_unlock_mute_toggle);
}

void MediaManager::_unlock_mute_toggle()
{
    m_muteToggleLocked = false;
}

bool MediaManager::is_muted() { return m_isMuted; }

// ─── Volume ────────────────────────────────────────────────────────

void MediaManager::setVolume(float volume)
{
    volume = qBound(0.0f, volume, 1.0f);
    m_volume = volume;
    if (m_isMuted)
        m_previousVolume = volume;
    applyOutputVolume();
    emit volumeChanged(volume);
}

void MediaManager::setDucking(float factor)
{
    factor = qBound(0.0f, factor, 1.0f);
    if (qFuzzyCompare(factor, m_duckTarget))
        return;
    const bool ducking = factor < m_duckTarget;
    m_duckTarget = factor;
    m_duckAnim.stop();
    m_duckAnim.setStartValue(m_duckCurrent);
    m_duckAnim.setEndValue(factor);
    m_duckAnim.setDuration(ducking ? 80 : 500);
    m_duckAnim.start();
}

void MediaManager::applyOutputVolume()
{
    m_audioOutput->setVolume(m_isMuted ? 0.0f : m_volume * m_duckCurrent);
}

float MediaManager::getVolume()
{
    // The user's volume, not the (possibly ducked or muted) output level
    return m_volume;
}

// ─── Shuffle ───────────────────────────────────────────────────────

QStringList MediaManager::_shuffle_playlist()
{
    QStringList files = _get_current_playlist_files();
    if (files.isEmpty())
        files = get_media_files(false);
    if (files.isEmpty())
        return {};

    // Use a random device for shuffling
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(files.begin(), files.end(), rng);
    return files;
}

void MediaManager::toggle_shuffle()
{
    m_shuffle = !m_shuffle;

    const QString currentSong = get_current_file();
    QStringList files = _get_current_playlist_files();

    if (m_shuffle) {
        if (m_originalFiles.isEmpty())
            m_originalFiles = files;

        QStringList shuffled = files;
        std::random_device rd;
        std::mt19937 rng(rd());
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        // Move current song to front
        int idx = shuffled.indexOf(currentSong);
        if (idx > 0)
            shuffled.swapItemsAt(0, idx);

        m_currentPlaylist = shuffled;
        m_currentIndex = 0;
        qCInfo(lcMedia) << "Shuffle enabled for" << m_currentPlaylistName;
    } else {
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            return cleanForSort(a) < cleanForSort(b);
        });

        if (!currentSong.isEmpty()) {
            int idx = files.indexOf(currentSong);
            if (idx >= 0) {
                m_currentIndex = idx;
            } else {
                m_currentIndex = 0;
            }
        } else {
            m_currentIndex = 0;
        }
        m_currentPlaylist = files;
        m_originalFiles.clear();
        qCInfo(lcMedia) << "Shuffle disabled for" << m_currentPlaylistName;
    }

    emit shuffleStateChanged(m_shuffle);

    if (m_settingsManager)
        m_settingsManager->save_last_shuffle_state(m_shuffle);

    emit mediaListChanged(m_currentPlaylist);
}

bool MediaManager::is_shuffled() { return m_shuffle; }

// ─── Playlist helpers ──────────────────────────────────────────────

QStringList MediaManager::_get_current_playlist_files()
{
    if (!m_currentPlaylistName.isEmpty() && m_playlists.contains(m_currentPlaylistName))
        return m_playlists[m_currentPlaylistName].files;
    return m_currentPlaylist;
}

// ─── Library scanning ──────────────────────────────────────────────

void MediaManager::scan_library(bool resetDisplayNames)
{
    if (m_scanInProgress) {
        qCInfo(lcMedia) << "scan_library: already in progress, skipping";
        return;
    }

    if (m_libraryRoot.isEmpty() || !QDir(m_libraryRoot).exists()) {
        emit scanProgress(QStringLiteral("[ERROR] Library path not set or doesn't exist"));
        return;
    }

    m_scanInProgress = true;
    _scan_library_inner(resetDisplayNames);
    m_scanInProgress = false;
}

void MediaManager::_scan_library_inner(bool resetDisplayNames)
{
    emit scanProgress(QStringLiteral("[SCAN] Starting library scan..."));
    emit scanProgress(QStringLiteral("[PATH] ") + m_libraryRoot);
    qCInfo(lcMedia) << "Scanning library at:" << m_libraryRoot;

    const QString previousPlaylist = m_currentPlaylistName;

    // Clear existing caches
    m_playlists.clear();
    m_playlistNames.clear();
    m_metadataCache.clear();
    m_albumArtCache.clear();
    m_accessCount.clear();
    m_allMusicFilePaths.clear();
    m_isAllMusicActive = false;
    if (resetDisplayNames) {
        m_displayNames.clear();
        _save_display_names();
    }
    invalidate_stats_cache();
    emit scanProgress(QStringLiteral("[CLEAR] Caches cleared"));

    QStringList allMusicFiles;

    // Check root-level audio files (goes to "Unsorted")
    emit scanProgress(QStringLiteral("[SCAN] Checking root folder for MP3s..."));
    QStringList rootMp3s;
    {
        QDir rootDir(m_libraryRoot);
        const QFileInfoList entries = rootDir.entryInfoList(QDir::Files);
        for (const QFileInfo &fi : entries) {
            if (hasAudioExtension(fi.fileName())) {
                rootMp3s.append(fi.fileName());
                m_allMusicFilePaths.insert(fi.fileName(), m_libraryRoot);
                allMusicFiles.append(fi.fileName());
            }
        }
    }

    if (!rootMp3s.isEmpty()) {
        PlaylistInfo info;
        info.name = QStringLiteral("Unsorted");
        info.path = m_libraryRoot;
        info.files = rootMp3s;
        info.songCount = rootMp3s.size();
        m_playlists.insert(QStringLiteral("Unsorted"), info);
        m_playlistNames.append(QStringLiteral("Unsorted"));
        emit scanProgress(QStringLiteral("[FOUND] 'Unsorted' - %1 songs").arg(rootMp3s.size()));
    }

    // Scan subfolders
    emit scanProgress(QStringLiteral("[SCAN] Scanning subfolders..."));
    {
        QDir rootDir(m_libraryRoot);
        const QStringList subfolders = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        emit scanProgress(QStringLiteral("[INFO] Found %1 subfolders to scan").arg(subfolders.size()));

        for (const QString &item : subfolders) {
            if (item.startsWith(QLatin1Char('.')))
                continue; // skip hidden

            const QString subfolderPath = m_libraryRoot + QDir::separator() + item;
            QStringList mp3Files;

            QDir subDir(subfolderPath);
            const QFileInfoList entries = subDir.entryInfoList(QDir::Files);
            for (const QFileInfo &fi : entries) {
                if (hasAudioExtension(fi.fileName())) {
                    mp3Files.append(fi.fileName());
                    // Handle duplicate filenames for All Music
                    QString uniqueName = fi.fileName();
                    if (m_allMusicFilePaths.contains(fi.fileName())) {
                        uniqueName = item + QStringLiteral(" - ") + fi.fileName();
                    }
                    m_allMusicFilePaths.insert(uniqueName, subfolderPath);
                    allMusicFiles.append(uniqueName);
                }
            }

            // Include every subfolder (even empty ones)
            PlaylistInfo info;
            info.name = item;
            info.path = subfolderPath;
            info.files = mp3Files;
            info.songCount = mp3Files.size();
            m_playlists.insert(item, info);
            m_playlistNames.append(item);

            if (!mp3Files.isEmpty()) {
                emit scanProgress(QStringLiteral("[FOUND] '%1' - %2 songs").arg(item).arg(mp3Files.size()));
            } else {
                emit scanProgress(QStringLiteral("[FOUND] '%1' - empty playlist").arg(item));
            }
        }
    }

#ifdef Q_OS_ANDROID
    // Android: also scan the app's external-files "Music" directory (and its
    // immediate subfolders) where the downloader writes files. Qt's library
    // root is at /sdcard/Music, but yt-dlp can only write to the scoped-storage
    // app-private dir. So we walk the app's own Music dir here and merge its
    // subfolder contents as extra playlists.
    {
        const QString appMusicRoot = OctaveAndroid::getDownloadsDir();
        qCInfo(lcMedia) << "Android secondary scan root:" << appMusicRoot;
        QDir rootDir(appMusicRoot);
        if (rootDir.exists()) {
            // Walk each subfolder as a playlist (Downloads, Unsorted, etc.)
            const QStringList subs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            qCInfo(lcMedia) << "Android secondary subfolders found:" << subs
                            << "(total entries:" << rootDir.entryList().size() << ")";
            for (const QString &sub : subs) {
                const QString subPath = rootDir.filePath(sub);
                QDir subDir(subPath);
                QStringList songs;
                for (const QFileInfo &fi : subDir.entryInfoList(QDir::Files)) {
                    if (hasAudioExtension(fi.fileName())) {
                        QString uniqueName = fi.fileName();
                        if (m_allMusicFilePaths.contains(fi.fileName())) {
                            uniqueName = sub + QStringLiteral(" - ") + fi.fileName();
                        }
                        songs.append(uniqueName);
                        m_allMusicFilePaths.insert(uniqueName, subPath);
                        allMusicFiles.append(uniqueName);
                    }
                }
                if (!songs.isEmpty()) {
                    // If the library root scan already found a playlist with
                    // this name (e.g. "Downloads"), MERGE the app-dir songs
                    // into it so the user sees one unified playlist. Each
                    // song tracks its own source path via m_allMusicFilePaths.
                    if (m_playlists.contains(sub)) {
                        PlaylistInfo &existing = m_playlists[sub];
                        existing.files.append(songs);
                        existing.songCount = existing.files.size();
                        qCInfo(lcMedia) << "Merged" << songs.size()
                                         << "app-dir songs into existing playlist:" << sub;
                        emit scanProgress(
                            QStringLiteral("[MERGED] '%1' +%2 from app dir")
                                .arg(sub).arg(songs.size()));
                    } else {
                        PlaylistInfo info;
                        info.name = sub;
                        info.path = subPath;
                        info.files = songs;
                        info.songCount = songs.size();
                        m_playlists.insert(sub, info);
                        m_playlistNames.append(sub);
                        qCInfo(lcMedia) << "Android playlist:" << sub
                                         << "at" << subPath
                                         << "songs:" << songs.size();
                        emit scanProgress(
                            QStringLiteral("[FOUND] '%1' (app dir) - %2 songs")
                                .arg(sub).arg(songs.size()));
                    }
                }
            }
        } else {
            qCInfo(lcMedia) << "Android secondary scan root does not exist:" << appMusicRoot;
        }
    }
#endif

    // Create "All Music" combined playlist
    if (!allMusicFiles.isEmpty()) {
        PlaylistInfo info;
        info.name = QStringLiteral("All Music");
        info.path = m_libraryRoot;
        info.files = allMusicFiles;
        info.songCount = allMusicFiles.size();
        info.isCombined = true;
        m_playlists.insert(QStringLiteral("All Music"), info);
        emit scanProgress(QStringLiteral("[FOUND] 'All Music' - %1 songs (combined)").arg(allMusicFiles.size()));
    }

    // Sort playlist names: "All Music" first, then "Unsorted", then alphabetical
    m_playlistNames.removeAll(QStringLiteral("Unsorted"));
    m_playlistNames.sort(Qt::CaseInsensitive);
    if (m_playlists.contains(QStringLiteral("Unsorted")))
        m_playlistNames.prepend(QStringLiteral("Unsorted"));
    if (m_playlists.contains(QStringLiteral("All Music")))
        m_playlistNames.prepend(QStringLiteral("All Music"));

    emit scanProgress(QStringLiteral("[DONE] Scan complete: %1 playlists, %2 total songs")
                          .arg(m_playlistNames.size())
                          .arg(allMusicFiles.size()));

    _auto_strip_display_names();

    // Re-select previous playlist if it still exists
    if (!previousPlaylist.isEmpty() && m_playlists.contains(previousPlaylist)) {
        QString previousFile;
        if (!m_currentPlaylist.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_currentPlaylist.size())
            previousFile = m_currentPlaylist[m_currentIndex];

        select_playlist(previousPlaylist);

        if (!previousFile.isEmpty()) {
            int idx = m_currentPlaylist.indexOf(previousFile);
            if (idx >= 0)
                m_currentIndex = idx;
        }
    }

    emit playlistsChanged();
}

void MediaManager::set_library_root(const QString &path)
{
    if (path.isEmpty())
        return;

    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QDir dir(normalized);
    if (dir.exists()) {
        m_libraryRoot = normalized;
        qCInfo(lcMedia) << "Library root set to:" << normalized;
        scan_library(false);

        if (!m_playlistNames.isEmpty())
            select_playlist(m_playlistNames.first());
    } else {
        qCInfo(lcMedia) << "Invalid library path:" << path;
    }
}

// ─── Playlist management ──────────────────────────────────────────

QStringList MediaManager::get_playlist_names()
{
    return m_playlistNames;
}

void MediaManager::select_playlist(const QString &name)
{
    if (!m_playlists.contains(name)) {
        qCInfo(lcMedia) << "Playlist not found:" << name;
        return;
    }

    qCInfo(lcMedia) << "Selecting playlist:" << name;

    m_currentPlaylistName = name;
    const PlaylistInfo &playlist = m_playlists[name];

    m_isAllMusicActive = playlist.isCombined;
    if (m_isAllMusicActive) {
        m_mediaDir = m_libraryRoot;
    } else {
        m_mediaDir = playlist.path;
    }

    QStringList sorted = playlist.files;
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        return cleanForSort(a) < cleanForSort(b);
    });
    m_currentPlaylist = sorted;
    m_currentIndex = 0;

    m_metadataCache.clear();

    invalidate_stats_cache();
    _calculate_all_stats();

    emit currentPlaylistChanged(name);
    emit mediaListChanged(m_currentPlaylist);
}

QString MediaManager::get_current_playlist_name()
{
    return m_currentPlaylistName;
}

QStringList MediaManager::get_current_song_list()
{
    return m_currentPlaylist;
}

// ─── File path helpers ─────────────────────────────────────────────

QString MediaManager::_get_file_path(const QString &filename)
{
    // Reject path traversal
    if (filename.contains(QDir::separator() + QStringLiteral(".."))
        || filename.startsWith(QStringLiteral("..") + QDir::separator())
        || filename == QStringLiteral("..")
        || filename.startsWith(QLatin1Char('/'))
        || filename.startsWith(QLatin1Char('\\'))) {
        qCInfo(lcMedia) << "Rejected potentially unsafe filename:" << filename;
        return {};
    }

    QString filePath;

    // Check _all_music_file_paths first
    if (m_allMusicFilePaths.contains(filename)) {
        const QString directory = m_allMusicFilePaths.value(filename);
        // Handle renamed files (prefixed with folder name for duplicates)
        if (filename.contains(QStringLiteral(" - "))
            && !QFile::exists(directory + QDir::separator() + filename)) {
            int dashPos = filename.indexOf(QStringLiteral(" - "));
            QString originalFilename = filename.mid(dashPos + 3);
            filePath = directory + QDir::separator() + originalFilename;
        } else {
            filePath = directory + QDir::separator() + filename;
        }
    } else {
        filePath = m_mediaDir + QDir::separator() + filename;
    }

    // Validate path is within library root. On Android we also accept the
    // app's external-files Music dir as a secondary valid root — that's where
    // the downloader writes (scoped-storage-safe path).
    if (!m_libraryRoot.isEmpty() && !_is_safe_path(m_libraryRoot, filePath)) {
#ifdef Q_OS_ANDROID
        const QString androidRoot = OctaveAndroid::getDownloadsDir();
        if (!androidRoot.isEmpty() && _is_safe_path(androidRoot, filePath)) {
            return filePath;
        }
#endif
        qCInfo(lcMedia) << "Path validation failed - file outside library:" << filePath;
        return {};
    }

    return filePath;
}

QString MediaManager::_get_original_filename(const QString &filename)
{
    if (filename.contains(QStringLiteral(" - ")) && m_allMusicFilePaths.contains(filename)) {
        int dashPos = filename.indexOf(QStringLiteral(" - "));
        if (dashPos >= 0) {
            QString potentialOriginal = filename.mid(dashPos + 3);
            if (m_allMusicFilePaths.contains(potentialOriginal))
                return potentialOriginal;
        }
    }
    return filename;
}

// ─── Song deletion ─────────────────────────────────────────────────

bool MediaManager::delete_song(const QString &filename)
{
    if (filename.isEmpty())
        return false;

    const QString filePath = _get_file_path(filename);
    if (filePath.isEmpty() || !QFile::exists(filePath))
        return false;

    // Stop if currently playing
    bool isCurrent = (!m_currentPlaylist.isEmpty()
                      && m_currentIndex >= 0 && m_currentIndex < m_currentPlaylist.size()
                      && m_currentPlaylist[m_currentIndex] == filename
                      && m_isPlaying);
    if (isCurrent) {
        m_player->stop();
        m_isPlaying = false;
        emit playStateChanged(false);
    }

    if (!QFile::remove(filePath)) {
        qCWarning(lcMedia) << "Failed to delete:" << filePath;
        return false;
    }

    qCInfo(lcMedia) << "Deleted song:" << filePath;

    if (m_displayNames.contains(filename)) {
        m_displayNames.remove(filename);
        _save_display_names();
    }

    scan_library(false);
    emit songDeleted(filename);
    return true;
}

// ─── Playlist creation / song move ─────────────────────────────────

bool MediaManager::create_playlist(const QString &name)
{
    if (name.trimmed().isEmpty())
        return false;

    const QString trimmed = name.trimmed();

    // Reject unsafe names
    if (trimmed.contains(QDir::separator()) || trimmed.contains(QLatin1Char('/'))
        || trimmed.contains(QLatin1Char('\\')) || trimmed.contains(QStringLiteral(".."))) {
        qCWarning(lcMedia) << "create_playlist: rejected unsafe name:" << trimmed;
        return false;
    }
    if (trimmed.startsWith(QLatin1Char('.')))
        return false;

    if (m_libraryRoot.isEmpty() || !QDir(m_libraryRoot).exists())
        return false;

    const QString folderPath = m_libraryRoot + QDir::separator() + trimmed;
    if (!_is_safe_path(m_libraryRoot, folderPath))
        return false;

    QDir dir(folderPath);
    if (dir.exists()) {
        // Reuse empty folder
        if (dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
            qCInfo(lcMedia) << "create_playlist: reusing empty folder:" << trimmed;
        } else {
            return false;
        }
    } else {
        if (!QDir().mkpath(folderPath)) {
            qCWarning(lcMedia) << "Failed to create playlist folder:" << trimmed;
            return false;
        }
        qCInfo(lcMedia) << "Created playlist folder:" << folderPath;
    }

    scan_library(false);
    return true;
}

bool MediaManager::move_song_to_playlist(const QString &filename, const QString &targetPlaylist)
{
    if (filename.isEmpty() || targetPlaylist.isEmpty())
        return false;

    const QString sourcePath = _get_file_path(filename);
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath))
        return false;

    if (!m_playlists.contains(targetPlaylist))
        return false;

    const QString targetDir = m_playlists[targetPlaylist].path;
    if (!QDir(targetDir).exists())
        return false;

    if (!_is_safe_path(m_libraryRoot, sourcePath))
        return false;

    const QString originalName = _get_original_filename(filename);
    QString destPath = targetDir + QDir::separator() + originalName;

    if (!_is_safe_path(m_libraryRoot, destPath))
        return false;

    // Handle collision
    if (QFile::exists(destPath)) {
        QFileInfo fi(originalName);
        const QString base = fi.completeBaseName();
        const QString ext = fi.suffix();
        int counter = 1;
        while (QFile::exists(destPath)) {
            destPath = targetDir + QDir::separator()
                       + QStringLiteral("%1 (%2).%3").arg(base).arg(counter).arg(ext);
            ++counter;
        }
    }

    // Stop if currently playing
    bool isCurrent = (!m_currentPlaylist.isEmpty()
                      && m_currentIndex >= 0 && m_currentIndex < m_currentPlaylist.size()
                      && m_currentPlaylist[m_currentIndex] == filename
                      && m_isPlaying);
    if (isCurrent) {
        m_player->stop();
        m_isPlaying = false;
        emit playStateChanged(false);
    }

    if (!QFile::rename(sourcePath, destPath)) {
        qCWarning(lcMedia) << "Failed to move:" << sourcePath;
        return false;
    }

    qCInfo(lcMedia) << "Moved" << sourcePath << "->" << destPath;
    scan_library(false);
    return true;
}

QStringList MediaManager::get_movable_playlist_names()
{
    QStringList result;
    for (const QString &n : m_playlistNames) {
        if (n != QStringLiteral("All Music"))
            result.append(n);
    }
    return result;
}

bool MediaManager::delete_playlist(const QString &name)
{
    if (name.trimmed().isEmpty())
        return false;

    const QString trimmed = name.trimmed();

    if (trimmed == QStringLiteral("All Music") || trimmed == QStringLiteral("Unsorted"))
        return false;

    if (!m_playlists.contains(trimmed))
        return false;

    const QString folderPath = m_playlists[trimmed].path;
    if (folderPath.isEmpty() || !QDir(folderPath).exists())
        return false;

    if (!_is_safe_path(m_libraryRoot, folderPath))
        return false;

    if (m_currentPlaylistName == trimmed && m_isPlaying) {
        m_player->stop();
        m_isPlaying = false;
        emit playStateChanged(false);
    }

    QDir dir(folderPath);
    if (!dir.removeRecursively()) {
        qCWarning(lcMedia) << "Failed to delete playlist:" << trimmed;
        return false;
    }

    qCInfo(lcMedia) << "Deleted playlist folder:" << folderPath;

    bool wasActive = (m_currentPlaylistName == trimmed);
    scan_library(false);

    if (wasActive && !m_playlistNames.isEmpty())
        select_playlist(m_playlistNames.first());

    return true;
}

// ─── Download integration ──────────────────────────────────────────

bool MediaManager::play_downloaded_song(const QString &artist, const QString &name, const QString &playlist)
{
    if (artist.isEmpty() || name.isEmpty() || playlist.isEmpty())
        return false;

    scan_library(false);

    if (!m_playlists.contains(playlist)) {
        qCWarning(lcMedia) << "play_downloaded_song: playlist not found:" << playlist;
        return false;
    }

    // Build expected filename stem
    QString expectedStem = (artist + QStringLiteral(" - ") + name).toLower();
    // Sanitize like the download tool
    static QRegularExpression unsafeChars(QStringLiteral("[/?\\\\*|<>]"));
    expectedStem.remove(unsafeChars);
    expectedStem.replace(QLatin1Char('"'), QLatin1Char('\''));
    expectedStem.replace(QLatin1Char(':'), QLatin1Char('-'));

    const PlaylistInfo &plData = m_playlists[playlist];
    QString matchedFile;

    // Exact match
    for (const QString &f : plData.files) {
        QString stem = QFileInfo(f).completeBaseName().toLower();
        if (stem == expectedStem) {
            matchedFile = f;
            break;
        }
    }

    // Fuzzy match
    if (matchedFile.isEmpty()) {
        for (const QString &f : plData.files) {
            QString stem = QFileInfo(f).completeBaseName().toLower();
            if (expectedStem.contains(stem) || stem.contains(expectedStem)) {
                matchedFile = f;
                break;
            }
        }
    }

    if (matchedFile.isEmpty()) {
        qCWarning(lcMedia) << "play_downloaded_song: no match for" << artist << "-" << name;
        return false;
    }

    select_playlist(playlist);
    play_file(matchedFile);
    qCInfo(lcMedia) << "play_downloaded_song: playing" << matchedFile << "from" << playlist;
    return true;
}

bool MediaManager::play_file_at_path(const QString &filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath))
        return false;

    scan_library(false);

    QFileInfo fi(filePath);
    const QString fileDir = fi.canonicalPath();
    const QString filename = fi.fileName();

    // Prefer the playlist whose on-disk directory matches the file's directory.
    for (auto it = m_playlists.constBegin(); it != m_playlists.constEnd(); ++it) {
        if (it->isCombined)
            continue;
        const QString plDir = QFileInfo(it->path).canonicalFilePath();
        if (plDir == fileDir && it->files.contains(filename)) {
            select_playlist(it.key());
            play_file(filename);
            qCInfo(lcMedia) << "play_file_at_path: playing" << filename << "from" << it.key();
            return true;
        }
    }

    // Fallback: any playlist that lists the filename, regardless of its
    // configured path. Handles the Android dual-root case where "Downloads"
    // is merged from /sdcard/Music/Downloads (the playlist's stored path)
    // AND the app-external files/Music/Downloads (where the files actually
    // live). Without this, play_file_at_path fails for every downloaded
    // song on Android.
    for (auto it = m_playlists.constBegin(); it != m_playlists.constEnd(); ++it) {
        if (it->isCombined)
            continue;
        if (it->files.contains(filename)) {
            select_playlist(it.key());
            play_file(filename);
            qCInfo(lcMedia) << "play_file_at_path: playing" << filename
                            << "from" << it.key() << "(filename-only match)";
            return true;
        }
    }

    qCWarning(lcMedia) << "play_file_at_path: no playlist contains" << filePath;
    return false;
}

QString MediaManager::get_song_file_path(const QString &filename)
{
    const QString path = _get_file_path(filename);
    return path.isEmpty() ? QString() : path;
}

QString MediaManager::get_full_file_path(const QString &filename)
{
    const QString path = _get_file_path(filename);
    return (!path.isEmpty() && QFile::exists(path)) ? path : QString();
}

// ─── Display names ─────────────────────────────────────────────────

void MediaManager::_load_display_names()
{
    QFile file(m_displayNamesPath);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcMedia) << "Failed to open display names file:" << m_displayNamesPath;
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError) {
        qCWarning(lcMedia) << "Display names JSON parse error:" << err.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        m_displayNames.insert(it.key(), it.value().toString());

    qCInfo(lcMedia) << "Loaded" << m_displayNames.size() << "display names";
}

void MediaManager::_save_display_names()
{
    QJsonObject obj;
    for (auto it = m_displayNames.constBegin(); it != m_displayNames.constEnd(); ++it)
        obj.insert(it.key(), it.value());

    QFile file(m_displayNamesPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcMedia) << "Failed to save display names:" << m_displayNamesPath;
        return;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    qCInfo(lcMedia) << "Saved" << m_displayNames.size() << "display names";
}

QString MediaManager::_strip_single_filename(const QString &filename)
{
    QString name = filename;

    // Strip known audio extensions
    for (const QString &ext : s_audioExtensions) {
        if (name.toLower().endsWith(ext)) {
            name.chop(ext.size());
            break;
        }
    }

    const QString originalName = name;

    // Get artist from metadata to remove from filename
    _cache_metadata(filename);
    if (m_metadataCache.contains(filename)) {
        const QString &artist = m_metadataCache[filename].artist;
        if (!artist.isEmpty() && artist != QStringLiteral("Unknown Artist")) {
            // Split on / and ; for multiple artists
            static QRegularExpression splitRe(QStringLiteral("[/;]"));
            const QStringList parts = artist.split(splitRe);
            for (const QString &a : parts) {
                const QString trimmed = a.trimmed();
                if (!trimmed.isEmpty()) {
                    name.replace(trimmed, QString(), Qt::CaseInsensitive);
                }
            }
        }
    }

    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    name.replace(s_trackNumRe, QString());
    name.replace(s_junkRe, QString());

    // Clean up leftover separators
    static QRegularExpression sepClean1(QStringLiteral("[,\\s]+-[,\\s]+"));
    static QRegularExpression sepClean2(QStringLiteral(",\\s*,"));
    static QRegularExpression sepClean3(QStringLiteral("\\s{2,}"));
    static QRegularExpression sepClean4(QStringLiteral("-{2,}"));
    static QRegularExpression sepClean5(QStringLiteral("\\.{2,}"));
    static QRegularExpression emptyParens(QStringLiteral("\\(\\s*\\)"));
    static QRegularExpression emptyBrackets(QStringLiteral("\\[\\s*\\]"));
    static QRegularExpression trimChars(QStringLiteral("^[ \\-.,]+|[ \\-.,]+$"));

    name.replace(sepClean1, QStringLiteral(" - "));
    name.replace(sepClean2, QStringLiteral(","));
    name.replace(sepClean3, QStringLiteral(" "));
    name.replace(sepClean4, QStringLiteral("-"));
    name.replace(sepClean5, QStringLiteral("."));
    name.replace(trimChars, QString());
    name.replace(emptyParens, QString());
    name.replace(emptyBrackets, QString());
    name = name.trimmed();

    if (!name.isEmpty() && name != originalName)
        return name;
    return {};
}

void MediaManager::_auto_strip_display_names()
{
    if (m_libraryRoot.isEmpty() || !QDir(m_libraryRoot).exists())
        return;

    QStringList allFiles;
    for (auto it = m_playlists.constBegin(); it != m_playlists.constEnd(); ++it) {
        if (it->isCombined)
            continue;
        for (const QString &f : it->files) {
            if (!allFiles.contains(f))
                allFiles.append(f);
        }
    }

    if (allFiles.isEmpty())
        return;

    int changed = 0;
    for (const QString &filename : allFiles) {
        if (m_displayNames.contains(filename))
            continue;
        const QString cleaned = _strip_single_filename(filename);
        if (!cleaned.isEmpty()) {
            m_displayNames.insert(filename, cleaned);
            ++changed;
        }
    }

    if (changed > 0) {
        _save_display_names();
        qCInfo(lcMedia) << "Auto-stripped" << changed << "of" << allFiles.size() << "filenames";
    }
}

void MediaManager::strip_filenames()
{
    if (m_libraryRoot.isEmpty() || !QDir(m_libraryRoot).exists()) {
        emit scanProgress(QStringLiteral("[ERROR] Library path not set or doesn't exist"));
        return;
    }

    emit scanProgress(QStringLiteral("[STRIP] Starting filename cleanup..."));

    int changed = 0;
    QStringList allFiles;

    for (auto it = m_playlists.constBegin(); it != m_playlists.constEnd(); ++it) {
        if (it->isCombined)
            continue;
        for (const QString &f : it->files) {
            if (!allFiles.contains(f))
                allFiles.append(f);
        }
    }

    if (allFiles.isEmpty()) {
        emit scanProgress(QStringLiteral("[STRIP] No MP3 files found in library"));
        return;
    }

    for (const QString &filename : allFiles) {
        const QString cleaned = _strip_single_filename(filename);
        if (!cleaned.isEmpty()) {
            m_displayNames.insert(filename, cleaned);
            // Construct original name for the progress message
            QString original = filename;
            for (const QString &ext : s_audioExtensions) {
                if (original.toLower().endsWith(ext)) {
                    original.chop(ext.size());
                    break;
                }
            }
            emit scanProgress(QStringLiteral("[STRIP] \"%1\" -> \"%2\"").arg(original, cleaned));
            ++changed;
        }
    }

    _save_display_names();
    emit scanProgress(QStringLiteral("[DONE] Stripped %1 of %2 filenames").arg(changed).arg(allFiles.size()));
    emit mediaListChanged(m_currentPlaylist);
}

QString MediaManager::get_display_name(const QString &filename)
{
    if (m_displayNames.contains(filename))
        return m_displayNames.value(filename);
    // Fallback: strip any recognised audio extension
    QString name = filename;
    const QString lower = name.toLower();
    for (const QString &ext : s_audioExtensions) {
        if (lower.endsWith(ext)) {
            name.chop(ext.size());
            break;
        }
    }
    return name;
}

// ─── Statistics ────────────────────────────────────────────────────

void MediaManager::invalidate_stats_cache()
{
    m_statsCache.isValid = false;
}

void MediaManager::_calculate_all_stats()
{
    if (m_statsCache.isValid)
        return;

    QStringList files = _get_current_playlist_files();
    qint64 totalMs = 0;
    QSet<QString> albums;
    QSet<QString> artists;

    for (const QString &filename : files) {
        if (!m_metadataCache.contains(filename))
            _cache_metadata(filename);

        if (m_metadataCache.contains(filename)) {
            const MediaMetadata &meta = m_metadataCache[filename];
            totalMs += static_cast<qint64>(meta.durationSeconds) * 1000;
            if (!meta.album.isEmpty() && meta.album != QStringLiteral("Unknown Album"))
                albums.insert(meta.album);
            if (!meta.artist.isEmpty() && meta.artist != QStringLiteral("Unknown Artist"))
                artists.insert(meta.artist);
        }
    }

    m_statsCache.totalDurationMs = totalMs;
    m_statsCache.totalDurationFormatted = _format_duration(totalMs);
    m_statsCache.albumCount = albums.size();
    m_statsCache.artistCount = artists.size();
    m_statsCache.isValid = true;

    emit totalDurationChanged(m_statsCache.totalDurationFormatted);
    emit albumCountChanged(m_statsCache.albumCount);
    emit artistCountChanged(m_statsCache.artistCount);
}

QString MediaManager::_format_duration(qint64 ms)
{
    int totalSeconds = static_cast<int>(ms / 1000);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString MediaManager::get_total_duration()
{
    if (!m_statsCache.isValid)
        _calculate_all_stats();
    return m_statsCache.totalDurationFormatted;
}

int MediaManager::get_album_count()
{
    if (!m_statsCache.isValid)
        _calculate_all_stats();
    return m_statsCache.albumCount;
}

int MediaManager::get_artist_count()
{
    if (!m_statsCache.isValid)
        _calculate_all_stats();
    return m_statsCache.artistCount;
}

// ─── Sorting ───────────────────────────────────────────────────────

QStringList MediaManager::sort_media_files(const QString &sortColumn, bool ascending)
{
    QStringList files = m_currentPlaylist.isEmpty() ? get_media_files(false) : m_currentPlaylist;

    if (sortColumn == QStringLiteral("title")) {
        std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) {
            QString aKey = cleanForSort(QFileInfo(a).completeBaseName());
            QString bKey = cleanForSort(QFileInfo(b).completeBaseName());
            return ascending ? aKey < bKey : aKey > bKey;
        });
    } else if (sortColumn == QStringLiteral("album")) {
        std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) {
            QString aKey = cleanForSort(get_album(a));
            QString bKey = cleanForSort(get_album(b));
            return ascending ? aKey < bKey : aKey > bKey;
        });
    } else if (sortColumn == QStringLiteral("artist")) {
        std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) {
            QString aKey = cleanForSort(get_band(a));
            QString bKey = cleanForSort(get_band(b));
            return ascending ? aKey < bKey : aKey > bKey;
        });
    }

    return files;
}

// ─── Playback state persistence ────────────────────────────────────

void MediaManager::_save_playback_state_debounced()
{
    m_saveStateTimer.start();
}

void MediaManager::_save_playback_state_now()
{
    if (!m_settingsManager)
        return;

    const QString currentSong = get_current_file();
    const int currentPosition = static_cast<int>(m_player->position());
    const QString currentPlaylist = m_currentPlaylistName;

    if (!currentSong.isEmpty()) {
        m_settingsManager->save_playback_state(currentSong, currentPosition, currentPlaylist);
    }
}

void MediaManager::_restore_playback_state()
{
    if (!m_settingsManager)
        return;

    const QString lastSong = m_settingsManager->get_last_played_song();
    const int lastPosition = m_settingsManager->get_last_played_position();
    const QString lastPlaylist = m_settingsManager->get_last_played_playlist();
    const bool autoPlay = m_settingsManager->get_auto_play_on_startup();

    // Restore shuffle state
    if (m_settingsManager->get_persist_shuffle_state()) {
        bool restoreShuffle = m_settingsManager->get_last_shuffle_state();
        if (restoreShuffle && !m_shuffle) {
            m_shuffle = true;
            emit shuffleStateChanged(true);
            qCInfo(lcMedia) << "Restored persistent shuffle state: ON";
        }
    }

    qCInfo(lcMedia) << "Restoring playback state: song=" << lastSong
                     << "position=" << lastPosition
                     << "playlist=" << lastPlaylist
                     << "autoPlay=" << autoPlay;

    if (lastSong.isEmpty())
        return;

    // Select playlist if it exists
    if (!lastPlaylist.isEmpty() && m_playlists.contains(lastPlaylist))
        select_playlist(lastPlaylist);

    if (!m_currentPlaylist.contains(lastSong)) {
        qCInfo(lcMedia) << "Last played song not found in current playlist:" << lastSong;
        return;
    }

    // If shuffle was restored, shuffle with last song at front
    if (m_shuffle) {
        m_originalFiles = m_currentPlaylist;
        QStringList shuffled = m_currentPlaylist;
        std::random_device rd;
        std::mt19937 rng(rd());
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        int idx = shuffled.indexOf(lastSong);
        if (idx > 0)
            shuffled.swapItemsAt(0, idx);
        m_currentPlaylist = shuffled;
        emit mediaListChanged(m_currentPlaylist);
    }

    // Set up player
    const QString filePath = _get_file_path(lastSong);
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        qCInfo(lcMedia) << "Last played file not found:" << filePath;
        return;
    }

    m_currentIndex = m_currentPlaylist.indexOf(lastSong);
    m_player->setSource(QUrl::fromLocalFile(filePath));

    emit currentMediaChanged(lastSong);
    _emit_metadata(lastSong);
    get_formatted_duration(lastSong);

    // Set position after delay
    if (lastPosition > 0) {
        QTimer::singleShot(100, this, [this, lastPosition]() {
            m_player->setPosition(lastPosition);
        });
    }

    // Auto-play if enabled
    if (autoPlay) {
        QTimer::singleShot(500, this, [this]() {
            m_player->play();
            _set_playing_state(true);
        });
    }

    qCInfo(lcMedia) << "Playback state restored:" << lastSong << "at" << lastPosition << "ms";
}

void MediaManager::_set_playing_state(bool isPlaying)
{
    m_isPlaying = isPlaying;
    m_isPaused = !isPlaying;
    emit playStateChanged(isPlaying);
}

// ─── Pre-caching ───────────────────────────────────────────────────

void MediaManager::_precache_neighbors_start()
{
    if (m_currentPlaylist.isEmpty())
        return;

    const int idx = m_currentIndex;
    const QStringList playlist = m_currentPlaylist; // snapshot
    const int n = playlist.size();
    // Bail on the "no current track" sentinel (-1) or a stale index that
    // outlived a playlist shrink.
    if (n == 0 || idx < 0 || idx >= n)
        return;

    QStringList neighbors;
    for (int offset = -3; offset <= 3; ++offset) {
        if (offset == 0)
            continue;
        // Euclidean modulo. C++ truncates toward zero, so a single "+ n" only
        // normalises while |offset| <= n — a 2-track playlist at idx 0 with
        // offset -3 yields -1 % 2 == -1 and trips QList::at's assert.
        const int j = ((idx + offset) % n + n) % n;
        const QString &track = playlist.at(j);
        // Playlists shorter than the +/-3 window wrap onto themselves; don't
        // re-cache the current track or queue the same file repeatedly.
        if (j == idx || neighbors.contains(track))
            continue;
        neighbors.append(track);
    }

    if (neighbors.isEmpty())
        return;

#if defined(Q_OS_MOBILE)
    // Mobile reads tags through the platform (cheap); keep it on this thread.
    _precache_tracks(neighbors);
#else
    // Phase 1 (worker): read tags for tracks not yet known. Phase 2 (GUI):
    // insert them, then hand the tracks still lacking a cover to a worker that
    // writes the cover file. Phase 3 (GUI): record the URLs. The caches are
    // only ever touched here on the GUI thread; the old version mutated them
    // from the worker while delegates read them.
    struct TagJob { QString filename, filePath, baseName; };
    QList<TagJob> tagJobs;
    for (const QString &f : neighbors) {
        if (m_metadataCache.contains(f))
            continue;
        const QString path = _get_file_path(f);
        if (path.isEmpty())
            continue;
        MediaMetadata stored;
        if (_meta_from_store(f, path, &stored)) {
            m_metadataCache.insert(f, stored);
            continue;
        }
        tagJobs.append({f, path, QFileInfo(_get_original_filename(f)).completeBaseName()});
    }
    auto *tagWatcher = new QFutureWatcher<QList<QPair<QString, MediaMetadata>>>(this);
    connect(tagWatcher, &QFutureWatcher<QList<QPair<QString, MediaMetadata>>>::finished, this,
            [this, tagWatcher, neighbors, tagJobs]() {
        tagWatcher->deleteLater();
        const auto results = tagWatcher->result();
        for (int i = 0; i < results.size(); ++i) {
            const QString &f = results[i].first;
            if (!m_metadataCache.contains(f)) {
                if (m_metadataCache.size() >= m_metadataCacheMax)
                    m_metadataCache.erase(m_metadataCache.begin());
                m_metadataCache.insert(f, results[i].second);
                _meta_to_store(f, tagJobs[i].filePath, results[i].second);
            }
        }
        // Covers
        struct ArtJob { QString albumId, filePath; };
        QList<ArtJob> artJobs;
        for (const QString &f : neighbors) {
            if (!m_metadataCache.contains(f))
                continue;
            const QString albumId = _get_album_id(f);
            if (m_albumArtCache.contains(albumId))
                continue;
            const QString cached = _cached_art_url_for(albumId);
            if (!cached.isEmpty()) {
                m_albumArtCache.insert(albumId, cached);
                continue;
            }
            const QString path = _get_file_path(f);
            if (!path.isEmpty() && QFile::exists(path))
                artJobs.append({albumId, path});
        }
        if (artJobs.isEmpty())
            return;
        auto *artWatcher = new QFutureWatcher<QList<QPair<QString, QString>>>(this);
        connect(artWatcher, &QFutureWatcher<QList<QPair<QString, QString>>>::finished, this,
                [this, artWatcher]() {
            artWatcher->deleteLater();
            for (const auto &r : artWatcher->result()) {
                if (!r.second.isEmpty() && !m_albumArtCache.contains(r.first)) {
                    _manage_cache(r.first);
                    m_albumArtCache.insert(r.first, r.second);
                }
            }
        });
        artWatcher->setFuture(QtConcurrent::run([this, artJobs]() {
            QList<QPair<QString, QString>> out;
            for (const ArtJob &j : artJobs)
                out.append({j.albumId, _extract_art_for(j.filePath, j.albumId)});
            return out;
        }));
    });
    tagWatcher->setFuture(QtConcurrent::run([tagJobs]() {
        QList<QPair<QString, MediaMetadata>> out;
        for (const TagJob &j : tagJobs)
            out.append({j.filename, _read_tags(j.filePath, j.baseName)});
        return out;
    }));
#endif
}

void MediaManager::_precache_tracks(const QStringList &filenames)
{
    // Synchronous variant (mobile): tags via the platform, covers via cache
    for (const QString &filename : filenames) {
        _cache_metadata(filename);
        get_album_art(filename);
    }
}

// ─── Update media directory ────────────────────────────────────────

void MediaManager::update_media_directory(const QString &directory)
{
    QDir dir(directory);
    if (!dir.exists())
        return;

    m_mediaDir = directory;
    m_metadataCache.clear();
    m_albumArtCache.clear();
    m_accessCount.clear();
    invalidate_stats_cache();

    get_media_files();

    const QString currentFile = get_current_file();
    if (m_isPlaying) {
        const QString path = _get_file_path(currentFile);
        if (!currentFile.isEmpty() && !path.isEmpty() && QFile::exists(path)) {
            play_file(currentFile);
        } else {
            QStringList files = get_media_files(false);
            if (!files.isEmpty()) {
                play_file(files.first());
            } else {
                m_player->stop();
                m_isPlaying = false;
                m_isPaused = true;
                emit playStateChanged(false);
            }
        }
    }
}

QString MediaManager::get_default_media_dir()
{
    return m_defaultMediaDir;
}

QString MediaManager::get_media_folder_name()
{
    return QFileInfo(m_mediaDir).fileName();
}
