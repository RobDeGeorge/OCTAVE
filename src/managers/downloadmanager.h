#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "../util/variantlistmodel.h"

// The two lists the download page shows, as models (one role per key) so a
// status change updates one row in place:
//   searchResults - one row per search hit: song_id, name, artist, album_name,
//     cover_url, url, duration, explicit, is_downloaded, downloaded_playlist,
//     is_failed, error_message, file_path, plus is_downloading / progress
//     while a download of it runs
//   downloadQueue - one row per download this session, oldest first: songId,
//     songName ("artist - name"), artist, name, status ("downloading" while
//     queued, running or paused, "complete", "failed" incl. cancelled),
//     progress (0-1), errorMsg, filePath
namespace DownloadModels {
inline QStringList searchKeys()
{
    return {QStringLiteral("song_id"), QStringLiteral("name"), QStringLiteral("artist"),
            QStringLiteral("album_name"), QStringLiteral("cover_url"), QStringLiteral("url"),
            QStringLiteral("duration"), QStringLiteral("explicit"), QStringLiteral("is_downloaded"),
            QStringLiteral("downloaded_playlist"), QStringLiteral("is_failed"),
            QStringLiteral("error_message"), QStringLiteral("file_path"),
            QStringLiteral("is_downloading"), QStringLiteral("progress")};
}
inline QStringList queueKeys()
{
    return {QStringLiteral("songId"), QStringLiteral("songName"), QStringLiteral("artist"),
            QStringLiteral("name"), QStringLiteral("status"), QStringLiteral("progress"),
            QStringLiteral("errorMsg"), QStringLiteral("filePath")};
}
} // namespace DownloadModels

#ifdef OCTAVE_ENABLE_DOWNLOADS

#include <QProcess>
#ifdef Q_OS_MOBILE
#include "../platform/androidprocessadapter.h"
using YtDlpProcess = AndroidProcessAdapter;
#else
using YtDlpProcess = QProcess;
#endif
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QQueue>

class SettingsManager;

// ─── Download queue entry ─────────────────────────────────────────
struct DownloadTask {
    QString songId;
    QString songName;
    QString artist;
    QString albumName;
    QString coverUrl;
    QString url;           // YouTube Music URL
    int duration = 0;
    float progress = 0.0f;
    QString status;        // "queued", "downloading", "complete", "error", "paused"
    QString errorMessage;
    QString filePath;      // set on completion
};

class DownloadManager : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "files")
    Q_CLASSINFO("octave.scope.read", "get_download_playlist hasStorageAccess next_search_page search search_more")
    Q_CLASSINFO("octave.scope.system", "requestStorageAccess")
    Q_CLASSINFO("octave.scope.full", "cleanup connect_settings_manager")
    friend class TstDownloadManager;

    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)
    Q_PROPERTY(int activeDownloads READ activeDownloads NOTIFY activeDownloadsChanged)
    Q_PROPERTY(QString downloadPath READ downloadPath CONSTANT)
    // False only in the stub below (OCTAVE_ENABLE_DOWNLOADS=OFF, app-store
    // builds); QML hides every download entry point on it.
    Q_PROPERTY(bool downloadsAvailable READ downloadsAvailable CONSTANT)
    // See DownloadModels above
    Q_PROPERTY(QAbstractListModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(QAbstractListModel *downloadQueue READ downloadQueue CONSTANT)

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    // Property getters
    bool isSearching() const;
    int activeDownloads() const;
    QString downloadPath() const;
    bool downloadsAvailable() const { return true; }
    QAbstractListModel *searchResults() const { return m_searchModel; }
    QAbstractListModel *downloadQueue() const { return m_queueModel; }

    // Android All-Files-Access (MANAGE_EXTERNAL_STORAGE). On other platforms
    // hasStorageAccess() always returns true and requestStorageAccess() is a
    // no-op, so QML can call these unconditionally.
    Q_INVOKABLE bool hasStorageAccess() const;
    Q_INVOKABLE bool requestStorageAccess();

signals:
    // Search signals (the results themselves are the searchResults model)
    void searchInProgress(bool inProgress);
    void searchError(const QString &errorMsg);

    // Download signals
    void downloadStarted(const QString &songId, const QString &displayName);
    void downloadProgress(const QString &songId, float progress);
    void downloadComplete(const QString &songId, const QString &filePath);
    void downloadError(const QString &songId, const QString &displayName, const QString &errorMsg);
    void downloadQueueChanged();

    // Status
    void statusMessage(const QString &message);

    // Properties
    void isSearchingChanged(bool value);
    void activeDownloadsChanged(int count);

public slots:
    // ──────────────────────────────────────────────────────────────
    // Slots — names are called from QML; don't rename them
    // ──────────────────────────────────────────────────────────────

    // Search
    void search(const QString &query);
    void next_search_page();
    void search_more();

    // Download a search hit (its row in searchResults supplies the details;
    // an id that isn't there downloads by id with songName as the title)
    void download(const QString &songId, const QString &songName);
    void cancel_download(const QString &songId);
    void pause_download(const QString &songId);
    void resume_download(const QString &songId);

    // Queue management
    void clear_download_queue();
    // Drop finished (complete / failed / cancelled) rows from downloadQueue
    void clear_finished_downloads();

    // Settings
    Q_INVOKABLE void connect_settings_manager(QObject *sm);
    void set_download_playlist(const QString &name);
    Q_INVOKABLE QString get_download_playlist();

    // Cleanup
    void cleanup();

private slots:
    void _onSearchProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void _onSearchProcessError(QProcess::ProcessError error);
    void _onDownloadProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void _onDownloadProcessError(QProcess::ProcessError error);
    void _onDownloadReadyReadStdout();
    void _onDownloadReadyReadStderr();
    void _processDownloadQueue();
    void _onCoverDownloadFinished(QNetworkReply *reply);

private:
    // ──────────────────────────────────────────────────────────────
    // Internal helpers
    // ──────────────────────────────────────────────────────────────
    QString _getDownloadPath() const;
    QString _getDownloadFormat() const;
    QString _getDownloadBitrate() const;
    QString _findYtDlp() const;
    // Returns the absolute path to a Netscape-format cookies file if one is
    // present at <DownloadLocation>/youtube_cookies.txt; empty otherwise.
    // Used to bypass YouTube's "Sign in to confirm you're not a bot" wall by
    // passing --cookies to yt-dlp. The user exports the file via a browser
    // extension (e.g. "Get cookies.txt LOCALLY") and drops it in Downloads.
    QString _getYoutubeCookiesPath() const;
    QJsonArray _parseSearchOutput(const QByteArray &rawOutput);
    void _markDownloaded(QJsonArray &results);
    QMap<QString, QString> _getDownloadedFiles() const;
    static QString _sanitizeForMatch(const QString &text);
    void _startNextDownload();
    void _embedMetadata(const QString &filePath, const DownloadTask &task);
    void _downloadCoverArt(const QString &songId, const QString &coverUrl, const QString &filePath);
#ifndef Q_OS_MOBILE
    void _embedCoverArt(const QString &filePath, const QByteArray &imageData);
#endif
    void _finishDownload(const QString &songId, const QString &filePath);
    void _failDownload(const QString &songId, const QString &errorMsg);
    void _emitStatusPatch(const QString &songId, const QString &field, const QVariant &value);
    // Queue a download from a search hit's fields (song_id, name, artist, ...)
    void _queueDownload(const QJsonObject &songData);
    // Mirror a task into downloadQueue and its search row (is_downloading, progress)
    void _syncTask(const QString &songId);
    // m_allResults into searchResults, with any running downloads' state
    void _publishResults();
    float _parseProgressLine(const QString &line);
#ifdef Q_OS_MOBILE
    // Android/iOS: yt-dlp runs via JNI (OctaveMediaBridge) which is a blocking
    // call returning the full combined output only when complete. stdout-line
    // progress parsing never fires mid-download, so the UI jumps 0% → 100%.
    // This QTimer polls OctaveAndroid::pollProgress() and broadcasts the
    // shared progress value to all active download tasks.
    void _pollMobileProgress();
#endif

    // ──────────────────────────────────────────────────────────────
    // Member variables
    // ──────────────────────────────────────────────────────────────
    SettingsManager *m_settingsManager = nullptr;

    VariantListModel *m_searchModel = nullptr;
    VariantListModel *m_queueModel = nullptr;

    // Search state
    bool m_isSearching = false;
    QString m_lastQuery;
    int m_searchOffset = 0;
    QJsonArray m_allResults;
    YtDlpProcess *m_searchProcess = nullptr;
    QByteArray m_searchBuffer;

    // Download state
    int m_activeDownloads = 0;
    int m_maxConcurrentDownloads = 2;
    QMap<QString, DownloadTask> m_downloadTasks;   // songId -> task
    QQueue<QString> m_downloadQueue;               // songIds in queue order
    QMap<QString, YtDlpProcess *> m_downloadProcesses; // songId -> active task
    QMap<QString, QByteArray> m_downloadStdout;    // songId -> accumulated stdout

    // Target playlist override (empty = use downloadSubfolder setting)
    QString m_targetSubfolder;

    // yt-dlp binary path (cached)
    mutable QString m_ytDlpPath;

    // Cover art downloads
    QNetworkAccessManager *m_networkManager = nullptr;
    QMap<QString, QString> m_pendingCoverDownloads; // reply URL -> songId
    // Desktop: songs whose file sits tagged in the staging dir while their
    // cover art downloads; they're published when the cover request ends.
    QSet<QString> m_awaitingCover;

#ifdef Q_OS_MOBILE
    QTimer *m_mobileProgressPoller = nullptr;
#endif

    // Audio file extensions for library scanning
    static const QStringList s_audioExtensions;
};

#else // OCTAVE_ENABLE_DOWNLOADS not defined — stub class

class DownloadManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)
    Q_PROPERTY(int activeDownloads READ activeDownloads NOTIFY activeDownloadsChanged)
    Q_PROPERTY(QString downloadPath READ downloadPath CONSTANT)
    Q_PROPERTY(bool downloadsAvailable READ downloadsAvailable CONSTANT)
    Q_PROPERTY(QAbstractListModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(QAbstractListModel *downloadQueue READ downloadQueue CONSTANT)

public:
    explicit DownloadManager(QObject *parent = nullptr)
        : QObject(parent)
        , m_searchModel(new VariantListModel(DownloadModels::searchKeys(), this))
        , m_queueModel(new VariantListModel(DownloadModels::queueKeys(), this)) {}
    ~DownloadManager() override = default;

    bool isSearching() const { return false; }
    int activeDownloads() const { return 0; }
    QString downloadPath() const { return QString(); }
    bool downloadsAvailable() const { return false; }
    QAbstractListModel *searchResults() const { return m_searchModel; }   // always empty
    QAbstractListModel *downloadQueue() const { return m_queueModel; }

signals:
    void searchInProgress(bool inProgress);
    void searchError(const QString &errorMsg);
    void downloadStarted(const QString &songId, const QString &displayName);
    void downloadProgress(const QString &songId, float progress);
    void downloadComplete(const QString &songId, const QString &filePath);
    void downloadError(const QString &songId, const QString &displayName, const QString &errorMsg);
    void downloadQueueChanged();
    void statusMessage(const QString &message);
    void isSearchingChanged(bool value);
    void activeDownloadsChanged(int count);

public slots:
    void search(const QString &) { emit searchError(QStringLiteral("Downloads disabled in this build")); }
    void next_search_page() {}
    void search_more() {}
    void download(const QString &songId, const QString &) { emit downloadError(songId, QString(), QStringLiteral("Downloads disabled in this build")); }
    void cancel_download(const QString &) {}
    void pause_download(const QString &) {}
    void resume_download(const QString &) {}
    void clear_download_queue() {}
    void clear_finished_downloads() {}
    Q_INVOKABLE void connect_settings_manager(QObject *) {}
    void set_download_playlist(const QString &) {}
    Q_INVOKABLE QString get_download_playlist() { return QStringLiteral("Downloads"); }
    void cleanup() {}

private:
    VariantListModel *m_searchModel;
    VariantListModel *m_queueModel;
};

#endif // OCTAVE_ENABLE_DOWNLOADS

#endif // DOWNLOADMANAGER_H
