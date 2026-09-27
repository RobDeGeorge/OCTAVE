#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

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

    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)
    Q_PROPERTY(int activeDownloads READ activeDownloads NOTIFY activeDownloadsChanged)
    Q_PROPERTY(QString downloadPath READ downloadPath CONSTANT)

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    // Property getters
    bool isSearching() const;
    int activeDownloads() const;
    QString downloadPath() const;

    // Android All-Files-Access (MANAGE_EXTERNAL_STORAGE). On other platforms
    // hasStorageAccess() always returns true and requestStorageAccess() is a
    // no-op, so QML can call these unconditionally.
    Q_INVOKABLE bool hasStorageAccess() const;
    Q_INVOKABLE bool requestStorageAccess();

signals:
    // Search signals
    void searchResults(const QString &jsonArray);
    void searchInProgress(bool inProgress);
    void searchError(const QString &errorMsg);

    // Download signals
    void downloadStarted(const QString &songId, const QString &displayName);
    void downloadProgress(const QString &songId, float progress);
    void downloadComplete(const QString &songId, const QString &filePath);
    void downloadError(const QString &songId, const QString &displayName, const QString &errorMsg);
    void downloadQueueChanged();
    void downloadStatusChanged(const QString &songId, const QString &field, const QString &valueJson);

    // Status
    void statusMessage(const QString &message);

    // Properties
    void isSearchingChanged(bool value);
    void activeDownloadsChanged(int count);

public slots:
    // ──────────────────────────────────────────────────────────────
    // Slots — names match the Python @Slot originals for QML
    // ──────────────────────────────────────────────────────────────

    // Search
    void search(const QString &query);
    void next_search_page();
    void search_more();

    // Download
    void download(const QString &songId, const QString &songName);
    void download_song(const QString &jsonString);
    void cancel_download(const QString &songId);
    void pause_download(const QString &songId);
    void resume_download(const QString &songId);

    // Queue management
    Q_INVOKABLE QVariantList get_download_queue();
    void clear_download_queue();

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

public:
    explicit DownloadManager(QObject *parent = nullptr) : QObject(parent) {}
    ~DownloadManager() override = default;

    bool isSearching() const { return false; }
    int activeDownloads() const { return 0; }
    QString downloadPath() const { return QString(); }

signals:
    void searchResults(const QString &jsonArray);
    void searchInProgress(bool inProgress);
    void searchError(const QString &errorMsg);
    void downloadStarted(const QString &songId, const QString &displayName);
    void downloadProgress(const QString &songId, float progress);
    void downloadComplete(const QString &songId, const QString &filePath);
    void downloadError(const QString &songId, const QString &displayName, const QString &errorMsg);
    void downloadQueueChanged();
    void downloadStatusChanged(const QString &songId, const QString &field, const QString &valueJson);
    void statusMessage(const QString &message);
    void isSearchingChanged(bool value);
    void activeDownloadsChanged(int count);

public slots:
    void search(const QString &) { emit searchError(QStringLiteral("Downloads disabled in this build")); }
    void next_search_page() {}
    void search_more() {}
    void download(const QString &songId, const QString &) { emit downloadError(songId, QString(), QStringLiteral("Downloads disabled in this build")); }
    void download_song(const QString &) { emit downloadError(QString(), QString(), QStringLiteral("Downloads disabled in this build")); }
    void cancel_download(const QString &) {}
    void pause_download(const QString &) {}
    void resume_download(const QString &) {}
    Q_INVOKABLE QVariantList get_download_queue() { return {}; }
    void clear_download_queue() {}
    Q_INVOKABLE void connect_settings_manager(QObject *) {}
    void set_download_playlist(const QString &) {}
    Q_INVOKABLE QString get_download_playlist() { return QStringLiteral("Downloads"); }
    void cleanup() {}
};

#endif // OCTAVE_ENABLE_DOWNLOADS

#endif // DOWNLOADMANAGER_H
