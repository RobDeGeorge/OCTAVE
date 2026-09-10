#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QMutex>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * NetworkManager — C++ port of backend/network_manager.py
 *
 * Handles WiFi/internet connectivity status and checks for updates via GitHub.
 * All blocking I/O (subprocess, HTTP) runs asynchronously to avoid freezing
 * the Qt event loop.
 *
 * QML accesses this as context property "networkManager".
 */
class NetworkManager : public QObject
{
    Q_OBJECT

    // ======================================================================
    // Q_PROPERTY declarations
    // ======================================================================

    // Network status
    Q_PROPERTY(QString networkName READ networkName NOTIFY networkNameChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)

    // Update check
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateStatusChanged)
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateMessageChanged)
    Q_PROPERTY(QString remoteCommit READ remoteCommit NOTIFY remoteCommitChanged)
    Q_PROPERTY(QString remoteCommitMsg READ remoteCommitMsg NOTIFY remoteCommitMsgChanged)
    // True when the checkout has uncommitted changes. Self-update hard-resets the
    // tree, so the UI must be able to hide that button before it eats real work.
    Q_PROPERTY(bool workingTreeDirty READ workingTreeDirty NOTIFY workingTreeDirtyChanged)

    // Self-update
    Q_PROPERTY(QString selfUpdateStatus READ selfUpdateStatus NOTIFY selfUpdateStatusChanged)
    Q_PROPERTY(QString selfUpdateMessage READ selfUpdateMessage NOTIFY selfUpdateMessageChanged)
    Q_PROPERTY(bool canSelfUpdate READ canSelfUpdate NOTIFY canSelfUpdateChanged)

public:
    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager() override;

    // Property getters
    QString networkName() const;
    bool isConnected() const;
    QString updateStatus() const;
    QString updateMessage() const;
    QString remoteCommit() const;
    QString remoteCommitMsg() const;
    bool workingTreeDirty() const;
    QString selfUpdateStatus() const;
    QString selfUpdateMessage() const;
    bool canSelfUpdate() const;

public slots:
    void refreshNetwork();
    void checkForUpdates();
    void applySelfUpdate();
    void restartApp();
    void shutdown_device();
    void cleanup();

signals:
    // Network status signals
    void networkNameChanged(const QString &name);
    void isConnectedChanged(bool connected);

    // Update check signals
    void updateStatusChanged(const QString &status);
    void updateMessageChanged(const QString &message);
    void remoteCommitChanged(const QString &commit);
    void remoteCommitMsgChanged(const QString &msg);
    void workingTreeDirtyChanged(bool dirty);

    // Self-update signals
    void selfUpdateStatusChanged(const QString &status);
    void selfUpdateMessageChanged(const QString &message);
    void canSelfUpdateChanged(bool canUpdate);

private slots:
    void onConnectivityCheckFinished();
    void onNetworkNameCheckFinished();
    void onUpdateCheckReplyFinished();
    void onSelfUpdateStepFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    // Internal helpers
    void refreshNetworkStatus();
    void startConnectivityCheck();
    void startNetworkNameCheck();
    QString repoDir() const;
    bool checkCanSelfUpdate();
    // Blocking git invocation for the short, cheap queries below. These run on the
    // GUI thread deliberately: they gate a destructive action, so their answer must
    // be current at the moment of the click, not whatever an async check last cached.
    QString runGitSync(const QStringList &args, bool *ok = nullptr, int timeoutMs = 10000) const;
    bool refreshWorkingTreeDirty();
    // "same" | "behind" | "ahead" | "diverged" — how local HEAD relates to the
    // remote commit reported by the GitHub API.
    QString localVsRemote(const QString &remoteShort, const QString &localShort) const;
    void runSelfUpdateStep();
    void rollback();
    void finishSelfUpdate(const QString &status, const QString &message);
    void setProgress(const QString &msg);
    QString findVenvPip();

    // ==================== State ====================

    // Network status
    QString m_networkName;
    bool m_isConnected = false;

    // Update check
    QString m_updateStatus;
    QString m_updateMessage;
    QString m_remoteCommit;
    QString m_remoteCommitMsg;
    bool m_workingTreeDirty = false;

    // Self-update
    QString m_selfUpdateStatus;
    QString m_selfUpdateMessage;
    bool m_canSelfUpdate = false;
    bool m_updating = false;
    bool m_autoUpdateChecked = false;
    QString m_oldCommit;
    int m_selfUpdateStep = 0;

    // Progress relay
    QMutex m_progressLock;
    QString m_progressMessage;

    // Timers
    QTimer *m_pollTimer = nullptr;
    int m_offlineStreak = 0;          // consecutive failed connectivity probes
    QTimer *m_resultPollTimer = nullptr;

    // Async network
    QNetworkAccessManager *m_nam = nullptr;

    // Async processes
    QProcess *m_connectivityProcess = nullptr;
    QProcess *m_networkNameProcess = nullptr;
    QProcess *m_gitProcess = nullptr;

    // Busy flags
    bool m_networkCheckBusy = false;
    bool m_updateCheckBusy = false;

    // GitHub repo for update checks
    static constexpr const char *GITHUB_REPO = "WayBetterSolutions/OCTAVE";
};

#endif // NETWORKMANAGER_H
