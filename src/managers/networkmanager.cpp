// networkmanager.cpp — C++ port of backend/network_manager.py
// OCTAVE Network Manager

#include "networkmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSysInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNetwork, "octave.network")

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

namespace {
constexpr int kPollOnlineMs = 30000;
constexpr int kPollOfflineMs = 120000;
constexpr int kOfflineStreakForBackoff = 3;
}

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_selfUpdateStatus(QStringLiteral("idle"))
{
    // checkCanSelfUpdate() spawns `git --version`; do it after the UI is up
    // (see the singleShot below) rather than on the startup path.
    m_nam = new QNetworkAccessManager(this);

    // Poll for intermediate progress messages from self-update steps
    m_resultPollTimer = new QTimer(this);
    m_resultPollTimer->setInterval(250);
    connect(m_resultPollTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker locker(&m_progressLock);
        if (!m_progressMessage.isEmpty()) {
            QString msg = m_progressMessage;
            m_progressMessage.clear();
            locker.unlock();
            if (msg != m_selfUpdateMessage) {
                m_selfUpdateMessage = msg;
                emit selfUpdateMessageChanged(msg);
            }
        }
    });

    // Poll network status periodically: every 30 s while online, every
    // 2 min after a few consecutive misses (a car with no internet should
    // not pay for a TLS timeout twice a minute forever).
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &NetworkManager::refreshNetworkStatus);
    m_pollTimer->start(kPollOnlineMs);

    // Check once on startup (slight delay to let the UI load)
    QTimer::singleShot(2000, this, [this]() {
        const bool can = checkCanSelfUpdate();
        if (can != m_canSelfUpdate) {
            m_canSelfUpdate = can;
            emit canSelfUpdateChanged(can);
        }
        refreshNetworkStatus();
    });
}

NetworkManager::~NetworkManager()
{
    cleanup();
}

// ---------------------------------------------------------------------------
// Property getters
// ---------------------------------------------------------------------------

QString NetworkManager::networkName() const
{
    return m_networkName;
}

bool NetworkManager::isConnected() const
{
    return m_isConnected;
}

QString NetworkManager::updateStatus() const
{
    return m_updateStatus;
}

QString NetworkManager::updateMessage() const
{
    return m_updateMessage;
}

QString NetworkManager::remoteCommit() const
{
    return m_remoteCommit;
}

QString NetworkManager::remoteCommitMsg() const
{
    return m_remoteCommitMsg;
}

bool NetworkManager::workingTreeDirty() const
{
    return m_workingTreeDirty;
}

QString NetworkManager::selfUpdateStatus() const
{
    return m_selfUpdateStatus;
}

QString NetworkManager::selfUpdateMessage() const
{
    return m_selfUpdateMessage;
}

bool NetworkManager::canSelfUpdate() const
{
    return m_canSelfUpdate;
}

// ---------------------------------------------------------------------------
// Slots — public API (callable from QML)
// ---------------------------------------------------------------------------

void NetworkManager::refreshNetwork()
{
    refreshNetworkStatus();
}

void NetworkManager::checkForUpdates()
{
    if (m_updateCheckBusy)
        return;
    m_updateCheckBusy = true;

    m_updateStatus = QStringLiteral("checking");
    emit updateStatusChanged(m_updateStatus);
    m_updateMessage = QStringLiteral("Checking for updates...");
    emit updateMessageChanged(m_updateMessage);

    // First, get local commit hash via git
    auto *gitProc = new QProcess(this);
    gitProc->setWorkingDirectory(repoDir());
    gitProc->setProcessChannelMode(QProcess::MergedChannels);

    connect(gitProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, gitProc](int exitCode, QProcess::ExitStatus /*exitStatus*/) {
        gitProc->deleteLater();

        QString localHash;
        if (exitCode == 0) {
            localHash = QString::fromUtf8(gitProc->readAllStandardOutput()).trimmed();
        }

        if (localHash.isEmpty()) {
            m_updateStatus = QStringLiteral("error");
            emit updateStatusChanged(m_updateStatus);
            m_updateMessage = QStringLiteral("Could not determine local version");
            emit updateMessageChanged(m_updateMessage);
            m_updateCheckBusy = false;
            return;
        }

        // Now query GitHub API for remote HEAD
        QString url = QStringLiteral("https://api.github.com/repos/%1/commits/main").arg(QLatin1String(GITHUB_REPO));
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OCTAVE-Updater"));
        request.setTransferTimeout(10000);

        QNetworkReply *reply = m_nam->get(request);

        connect(reply, &QNetworkReply::finished, this, [this, reply, localHash]() {
            reply->deleteLater();
            m_updateCheckBusy = false;

            if (reply->error() != QNetworkReply::NoError) {
                qCWarning(lcNetwork) << "Update check failed:" << reply->errorString();
                m_updateStatus = QStringLiteral("error");
                emit updateStatusChanged(m_updateStatus);
                m_updateMessage = QStringLiteral("Could not reach GitHub");
                emit updateMessageChanged(m_updateMessage);
                return;
            }

            QByteArray data = reply->readAll();
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                m_updateStatus = QStringLiteral("error");
                emit updateStatusChanged(m_updateStatus);
                m_updateMessage = QStringLiteral("Could not parse GitHub response");
                emit updateMessageChanged(m_updateMessage);
                return;
            }

            QJsonObject obj = doc.object();
            QString remoteSha = obj.value(QStringLiteral("sha")).toString().left(7);
            QString remoteMsg = obj.value(QStringLiteral("commit")).toObject()
                                    .value(QStringLiteral("message")).toString()
                                    .section(QLatin1Char('\n'), 0, 0);

            // A plain `remoteSha != localHash` test cannot tell "behind" from
            // "ahead": running from a source checkout with local commits reported
            // an update to a commit *older* than HEAD, and the self-update path
            // would then hard-reset onto it. Classify the relationship instead.
            const QString relation = localVsRemote(remoteSha, localHash);
            const bool dirty = refreshWorkingTreeDirty();

            QString status;
            QString message;
            if (relation == QLatin1String("same")) {
                status = QStringLiteral("up-to-date");
                message = QStringLiteral("You're running the latest version");
            } else if (relation == QLatin1String("ahead")) {
                status = QStringLiteral("ahead");
                message = QStringLiteral("Dev build — ahead of origin/main");
            } else if (relation == QLatin1String("diverged")) {
                status = QStringLiteral("diverged");
                message = QStringLiteral("Local branch has diverged from origin/main");
            } else {
                status = QStringLiteral("update-available");
                message = QStringLiteral("Update available (latest: %1)").arg(remoteSha);
            }

            qCInfo(lcNetwork) << "Update check: local=" << localHash
                              << "remote=" << remoteSha << "relation=" << relation
                              << "dirty=" << dirty << "status=" << status;

            m_updateStatus = status;
            emit updateStatusChanged(status);
            m_updateMessage = message;
            emit updateMessageChanged(message);

            if (!remoteSha.isEmpty()) {
                m_remoteCommit = remoteSha;
                emit remoteCommitChanged(remoteSha);
            }
            if (!remoteMsg.isEmpty()) {
                m_remoteCommitMsg = remoteMsg;
                emit remoteCommitMsgChanged(remoteMsg);
            }
        });
    });

    gitProc->start(QStringLiteral("git"),
                   {QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")});

    if (!gitProc->waitForStarted(5000)) {
        gitProc->deleteLater();
        m_updateStatus = QStringLiteral("error");
        emit updateStatusChanged(m_updateStatus);
        m_updateMessage = QStringLiteral("Could not determine local version");
        emit updateMessageChanged(m_updateMessage);
        m_updateCheckBusy = false;
    }
}

void NetworkManager::applySelfUpdate()
{
    if (!m_canSelfUpdate)
        return;
    // Hard lock — prevents double-tap even if UI is slow to update
    if (m_updating)
        return;

    // Step 2 of this process is `git reset --hard origin/main`. Re-verify the
    // preconditions right here rather than trusting the cached update status:
    // the tree can be dirtied, or commits made, long after the last check ran.
    if (refreshWorkingTreeDirty()) {
        qCWarning(lcNetwork) << "Self-update refused: working tree has uncommitted changes";
        m_selfUpdateStatus = QStringLiteral("error");
        emit selfUpdateStatusChanged(m_selfUpdateStatus);
        m_selfUpdateMessage = QStringLiteral("Uncommitted changes — commit or stash them first");
        emit selfUpdateMessageChanged(m_selfUpdateMessage);
        return;
    }
    if (m_updateStatus == QLatin1String("ahead") || m_updateStatus == QLatin1String("diverged")) {
        qCWarning(lcNetwork) << "Self-update refused: local branch is" << m_updateStatus
                             << "relative to origin/main";
        m_selfUpdateStatus = QStringLiteral("error");
        emit selfUpdateStatusChanged(m_selfUpdateStatus);
        m_selfUpdateMessage = QStringLiteral("Local commits would be lost — refusing to reset");
        emit selfUpdateMessageChanged(m_selfUpdateMessage);
        return;
    }

    m_updating = true;

    // Relay setProgress() messages from the step machine to selfUpdateMessage
    // (mirrors the timer Python's _submit_work() starts).
    m_resultPollTimer->start();

    m_selfUpdateStatus = QStringLiteral("fetching");
    emit selfUpdateStatusChanged(m_selfUpdateStatus);
    m_selfUpdateMessage = QStringLiteral("Fetching latest changes...");
    emit selfUpdateMessageChanged(m_selfUpdateMessage);

    // Step 0: Save current commit hash for rollback
    m_selfUpdateStep = 0;
    m_oldCommit.clear();

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(repoDir());
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus /*exitStatus*/) {
        proc->deleteLater();
        if (exitCode == 0) {
            m_oldCommit = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
            qCInfo(lcNetwork) << "Rollback point saved:" << m_oldCommit.left(7);
        }
        // Proceed to step 1 regardless
        m_selfUpdateStep = 1;
        runSelfUpdateStep();
    });

    proc->start(QStringLiteral("git"),
                {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    if (!proc->waitForStarted(5000)) {
        proc->deleteLater();
        m_selfUpdateStep = 1;
        runSelfUpdateStep();
    }
}

void NetworkManager::restartApp()
{
    qCInfo(lcNetwork) << "App restart requested";

    QString appPath = QCoreApplication::applicationFilePath();
    QStringList args = QCoreApplication::arguments();
    // Remove the first argument (the application path itself) and re-add remaining
    if (!args.isEmpty())
        args.removeFirst();

    // Pre-flight: For Python-based launcher, verify main.py can parse.
    // In C++ build this is a no-op, but we keep the structure for hybrid setups.
    QString mainPy = QDir(repoDir()).filePath(QStringLiteral("main.py"));
    if (QFileInfo::exists(mainPy)) {
        QProcess check;
        check.setWorkingDirectory(repoDir());
        check.start(QStringLiteral("python3"),
                     {QStringLiteral("-m"), QStringLiteral("py_compile"), mainPy});
        if (check.waitForFinished(15000) && check.exitCode() != 0) {
            qCWarning(lcNetwork) << "Restart aborted — main.py has errors:"
                                 << QString::fromUtf8(check.readAllStandardError()).trimmed();
            m_selfUpdateStatus = QStringLiteral("error");
            emit selfUpdateStatusChanged(m_selfUpdateStatus);
            m_selfUpdateMessage = QStringLiteral("Restart aborted — code has errors");
            emit selfUpdateMessageChanged(m_selfUpdateMessage);
            return;
        }
    }

    qCInfo(lcNetwork) << "Restarting:" << appPath << args;

#ifdef Q_OS_WIN
    QProcess::startDetached(appPath, args);
    QCoreApplication::quit();
#else
    // On Unix, connect to aboutToQuit and then replace the process via execv-style restart.
    // QProcess::startDetached + quit is the portable Qt approach.
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [appPath, args]() {
        QProcess::startDetached(appPath, args);
    });
    QCoreApplication::quit();
#endif
}

void NetworkManager::shutdown_device()
{
    QString system = QSysInfo::kernelType();
    qCInfo(lcNetwork) << "Device shutdown requested on" << system;

    // Schedule OS shutdown with a brief delay so app cleanup can finish
#ifdef Q_OS_LINUX
    QProcess::startDetached(QStringLiteral("systemctl"),
                            {QStringLiteral("--no-block"), QStringLiteral("poweroff")});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("sudo"),
                            {QStringLiteral("shutdown"), QStringLiteral("-h"), QStringLiteral("+0")});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("shutdown"),
                            {QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("5")});
#endif

    // Quit the app (triggers cleanup_on_quit via aboutToQuit signal)
    QCoreApplication::quit();
}

void NetworkManager::cleanup()
{
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    if (m_resultPollTimer) {
        m_resultPollTimer->stop();
    }

    // Kill any running git processes
    if (m_gitProcess) {
        m_gitProcess->kill();
        m_gitProcess->waitForFinished(3000);
    }
    if (m_connectivityProcess) {
        m_connectivityProcess->kill();
        m_connectivityProcess->waitForFinished(3000);
    }
    if (m_networkNameProcess) {
        m_networkNameProcess->kill();
        m_networkNameProcess->waitForFinished(3000);
    }
}

// ---------------------------------------------------------------------------
// Private — Network Status
// ---------------------------------------------------------------------------

void NetworkManager::refreshNetworkStatus()
{
    if (m_networkCheckBusy)
        return;
    m_networkCheckBusy = true;

    // Start connectivity check and network name check in parallel
    startConnectivityCheck();
    startNetworkNameCheck();
}

void NetworkManager::startConnectivityCheck()
{
    // Use QNetworkAccessManager to check internet connectivity by hitting
    // Google's generate_204 endpoint (same as the Python version).
    QNetworkRequest request{QUrl(QStringLiteral("https://clients3.google.com/generate_204"))};
    request.setTransferTimeout(5000);

    QNetworkReply *reply = m_nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        bool connected = (reply->error() == QNetworkReply::NoError);

        if (connected != m_isConnected) {
            m_isConnected = connected;
            emit isConnectedChanged(connected);
        }
        m_offlineStreak = connected ? 0 : m_offlineStreak + 1;
        const int wanted = (m_offlineStreak >= kOfflineStreakForBackoff) ? kPollOfflineMs : kPollOnlineMs;
        if (m_pollTimer->interval() != wanted)
            m_pollTimer->setInterval(wanted);

        // Auto-check for updates once on first successful connectivity
        if (connected && !m_autoUpdateChecked) {
            m_autoUpdateChecked = true;
            QTimer::singleShot(500, this, &NetworkManager::checkForUpdates);
        }

        // Mark network check as complete (only when both checks are done)
        // The name check also clears this flag, so we use a simple approach:
        // the last one to finish clears the busy flag.
        // Since both run in parallel, we just let each slot contribute its part.
        m_networkCheckBusy = false;
    });
}

void NetworkManager::startNetworkNameCheck()
{
    // Platform-specific network name detection using external commands.
    // Same logic as Python's _get_network_name_sync(), but non-blocking via QProcess.

#ifdef Q_OS_ANDROID
    // Android: simplified — just report "WiFi"
    QString newName = QStringLiteral("WiFi");
    if (newName != m_networkName) {
        m_networkName = newName;
        emit networkNameChanged(newName);
    }
    return;
#endif

    if (m_networkNameProcess) {
        // Previous check still running; skip
        return;
    }

    m_networkNameProcess = new QProcess(this);

    connect(m_networkNameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NetworkManager::onNetworkNameCheckFinished);

#ifdef Q_OS_LINUX
    // Linux: nmcli -t -f ACTIVE,SSID dev wifi
    m_networkNameProcess->start(QStringLiteral("nmcli"),
                                {QStringLiteral("-t"), QStringLiteral("-f"),
                                 QStringLiteral("ACTIVE,SSID"), QStringLiteral("dev"),
                                 QStringLiteral("wifi")});
#elif defined(Q_OS_MACOS)
    // macOS: networksetup -getairportnetwork en0
    m_networkNameProcess->start(QStringLiteral("networksetup"),
                                {QStringLiteral("-getairportnetwork"), QStringLiteral("en0")});
#elif defined(Q_OS_WIN)
    // Windows: netsh wlan show interfaces
    m_networkNameProcess->start(QStringLiteral("netsh"),
                                {QStringLiteral("wlan"), QStringLiteral("show"),
                                 QStringLiteral("interfaces")});
#else
    // Unknown platform — no name detection
    m_networkNameProcess->deleteLater();
    m_networkNameProcess = nullptr;
    return;
#endif

    if (!m_networkNameProcess->waitForStarted(5000)) {
        m_networkNameProcess->deleteLater();
        m_networkNameProcess = nullptr;
    }
}

void NetworkManager::onConnectivityCheckFinished()
{
    // Handled inline in the lambda in startConnectivityCheck()
}

void NetworkManager::onNetworkNameCheckFinished()
{
    if (!m_networkNameProcess)
        return;

    QString output = QString::fromUtf8(m_networkNameProcess->readAllStandardOutput()).trimmed();
    int exitCode = m_networkNameProcess->exitCode();

    m_networkNameProcess->deleteLater();
    m_networkNameProcess = nullptr;

    QString newName;

#ifdef Q_OS_LINUX
    if (exitCode == 0) {
        // Parse nmcli WiFi output: lines like "yes:MyNetwork"
        const QStringList lines = output.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("yes:"))) {
                QString ssid = line.mid(4);
                if (!ssid.isEmpty()) {
                    newName = ssid;
                    break;
                }
            }
        }

        // If no WiFi found, check for Ethernet via a second process
        if (newName.isEmpty()) {
            auto *ethProc = new QProcess(this);
            connect(ethProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, ethProc](int ethExitCode, QProcess::ExitStatus /*exitStatus*/) {
                ethProc->deleteLater();
                if (ethExitCode == 0) {
                    QString ethOutput = QString::fromUtf8(ethProc->readAllStandardOutput()).trimmed();
                    const QStringList ethLines = ethOutput.split(QLatin1Char('\n'));
                    for (const QString &line : ethLines) {
                        if (line.contains(QLatin1String("ethernet")) &&
                            line.contains(QLatin1String("connected"))) {
                            if (m_networkName != QLatin1String("Ethernet")) {
                                m_networkName = QStringLiteral("Ethernet");
                                emit networkNameChanged(m_networkName);
                            }
                            return;
                        }
                    }
                }
                // Neither WiFi nor Ethernet — report "" like Python does
                clearNetworkName();
            });
            ethProc->start(QStringLiteral("nmcli"),
                           {QStringLiteral("-t"), QStringLiteral("-f"),
                            QStringLiteral("TYPE,STATE"), QStringLiteral("dev")});
            if (!ethProc->waitForStarted(5000)) {
                ethProc->deleteLater();
                clearNetworkName();
            }
            return; // Ethernet check is async — it will emit the signal itself
        }
    }
#elif defined(Q_OS_MACOS)
    if (exitCode == 0 && output.contains(QLatin1String("Current Wi-Fi Network"))) {
        int idx = output.indexOf(QLatin1String(": "));
        if (idx >= 0) {
            newName = output.mid(idx + 2).trimmed();
        }
    }
    if (newName.isEmpty()) {
        // Check for Ethernet via ifconfig en0
        auto *ethProc = new QProcess(this);
        connect(ethProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, ethProc](int ethExitCode, QProcess::ExitStatus /*exitStatus*/) {
            ethProc->deleteLater();
            if (ethExitCode == 0) {
                QString ethOutput = QString::fromUtf8(ethProc->readAllStandardOutput());
                if (ethOutput.contains(QLatin1String("status: active"))) {
                    if (m_networkName != QLatin1String("Ethernet")) {
                        m_networkName = QStringLiteral("Ethernet");
                        emit networkNameChanged(m_networkName);
                    }
                    return;
                }
            }
            // Neither WiFi nor Ethernet — report "" like Python does
            clearNetworkName();
        });
        ethProc->start(QStringLiteral("ifconfig"), {QStringLiteral("en0")});
        if (!ethProc->waitForStarted(5000)) {
            ethProc->deleteLater();
            clearNetworkName();
        }
        return;
    }
#elif defined(Q_OS_WIN)
    if (exitCode == 0) {
        const QStringList lines = output.split(QLatin1Char('\n'));
        for (const QString &rawLine : lines) {
            QString line = rawLine.trimmed();
            if (line.startsWith(QLatin1String("SSID")) &&
                !line.contains(QLatin1String("BSSID"))) {
                int idx = line.indexOf(QLatin1Char(':'));
                if (idx >= 0) {
                    QString ssid = line.mid(idx + 1).trimmed();
                    if (!ssid.isEmpty()) {
                        newName = ssid;
                        break;
                    }
                }
            }
        }
    }
    if (newName.isEmpty()) {
        // Check for Ethernet via netsh interface show interface
        auto *ethProc = new QProcess(this);
        connect(ethProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, ethProc](int ethExitCode, QProcess::ExitStatus /*exitStatus*/) {
            ethProc->deleteLater();
            if (ethExitCode == 0) {
                QString ethOutput = QString::fromUtf8(ethProc->readAllStandardOutput());
                const QStringList ethLines = ethOutput.split(QLatin1Char('\n'));
                for (const QString &line : ethLines) {
                    if (line.contains(QLatin1String("Connected")) &&
                        line.contains(QLatin1String("Ethernet"))) {
                        if (m_networkName != QLatin1String("Ethernet")) {
                            m_networkName = QStringLiteral("Ethernet");
                            emit networkNameChanged(m_networkName);
                        }
                        return;
                    }
                }
            }
            // Neither WiFi nor Ethernet — report "" like Python does
            clearNetworkName();
        });
        ethProc->start(QStringLiteral("netsh"),
                       {QStringLiteral("interface"), QStringLiteral("show"),
                        QStringLiteral("interface")});
        if (!ethProc->waitForStarted(5000)) {
            ethProc->deleteLater();
            clearNetworkName();
        }
        return;
    }
#endif

    if (newName != m_networkName) {
        m_networkName = newName;
        emit networkNameChanged(newName);
    }
}

void NetworkManager::clearNetworkName()
{
    if (!m_networkName.isEmpty()) {
        m_networkName.clear();
        emit networkNameChanged(m_networkName);
    }
}

void NetworkManager::onUpdateCheckReplyFinished()
{
    // Handled inline in the lambda in checkForUpdates()
}

// ---------------------------------------------------------------------------
// Private — Self-Update step machine
// ---------------------------------------------------------------------------

void NetworkManager::runSelfUpdateStep()
{
    // Self-update is implemented as a step machine using QProcess.
    // Each step starts a git command; when it finishes, the next step runs.
    //
    // Steps:
    //   1 — git fetch origin main
    //   2 — git reset --hard origin/main
    //   3 — (Python pre-flight check — py_compile main.py)
    //   4 — pip install -r requirements.txt
    //   5 — git log -1 --format=%h %s (get new commit info)

    if (m_gitProcess) {
        m_gitProcess->deleteLater();
        m_gitProcess = nullptr;
    }

    m_gitProcess = new QProcess(this);
    m_gitProcess->setWorkingDirectory(repoDir());

    connect(m_gitProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NetworkManager::onSelfUpdateStepFinished);

    switch (m_selfUpdateStep) {
    case 1: {
        // git fetch origin main
        setProgress(QStringLiteral("Fetching latest changes..."));
        m_gitProcess->start(QStringLiteral("git"),
                            {QStringLiteral("fetch"), QStringLiteral("origin"),
                             QStringLiteral("main")});
        break;
    }
    case 2: {
        // git reset --hard origin/main
        setProgress(QStringLiteral("Applying update..."));
        m_gitProcess->start(QStringLiteral("git"),
                            {QStringLiteral("reset"), QStringLiteral("--hard"),
                             QStringLiteral("origin/main")});
        break;
    }
    case 3: {
        // Pre-flight check: python3 -m py_compile main.py
        setProgress(QStringLiteral("Verifying new code..."));
        QString mainPy = QDir(repoDir()).filePath(QStringLiteral("main.py"));
        if (!QFileInfo::exists(mainPy)) {
            // No main.py to check — skip to next step
            m_selfUpdateStep = 4;
            runSelfUpdateStep();
            return;
        }
        m_gitProcess->start(QStringLiteral("python3"),
                            {QStringLiteral("-m"), QStringLiteral("py_compile"), mainPy});
        break;
    }
    case 4: {
        // pip install -r requirements.txt
        setProgress(QStringLiteral("Installing dependencies..."));
        QString requirementsPath = QDir(repoDir()).filePath(QStringLiteral("requirements.txt"));
        QString pipPath = findVenvPip();

        if (pipPath.isEmpty() || !QFileInfo::exists(requirementsPath)) {
            qCWarning(lcNetwork) << "Could not locate venv pip or requirements.txt — skipping dependency install";
            m_selfUpdateStep = 5;
            runSelfUpdateStep();
            return;
        }
        m_gitProcess->start(pipPath, {QStringLiteral("install"), QStringLiteral("-r"), requirementsPath});
        break;
    }
    case 5: {
        // git log -1 --format=%h %s
        m_gitProcess->start(QStringLiteral("git"),
                            {QStringLiteral("log"), QStringLiteral("-1"),
                             QStringLiteral("--format=%h %s")});
        break;
    }
    default: {
        // Should not reach here
        finishSelfUpdate(QStringLiteral("error"), QStringLiteral("Internal error: invalid update step"));
        return;
    }
    }

    if (!m_gitProcess->waitForStarted(5000)) {
        qCWarning(lcNetwork) << "Failed to start process for self-update step" << m_selfUpdateStep;
        if (m_selfUpdateStep <= 2) {
            rollback();
            finishSelfUpdate(QStringLiteral("error"), QStringLiteral("Failed to start git process"));
        } else {
            // Non-critical step — try to proceed
            m_selfUpdateStep++;
            if (m_selfUpdateStep <= 5)
                runSelfUpdateStep();
            else
                finishSelfUpdate(QStringLiteral("restart-required"),
                                 QStringLiteral("Update applied (some steps skipped)"));
        }
    }
}

void NetworkManager::onSelfUpdateStepFinished(int exitCode, QProcess::ExitStatus /*exitStatus*/)
{
    if (!m_gitProcess)
        return;

    QString output = QString::fromUtf8(m_gitProcess->readAllStandardOutput()).trimmed();
    QString errorOutput = QString::fromUtf8(m_gitProcess->readAllStandardError()).trimmed();

    m_gitProcess->deleteLater();
    m_gitProcess = nullptr;

    switch (m_selfUpdateStep) {
    case 1: {
        // git fetch finished
        if (exitCode != 0) {
            QString errMsg = errorOutput.isEmpty() ? QStringLiteral("unknown error") : errorOutput;
            rollback();
            finishSelfUpdate(QStringLiteral("error"),
                             QStringLiteral("Fetch failed: %1").arg(errMsg));
            return;
        }
        m_selfUpdateStep = 2;
        runSelfUpdateStep();
        break;
    }
    case 2: {
        // git reset --hard finished
        if (exitCode != 0) {
            QString errMsg = errorOutput.isEmpty() ? QStringLiteral("unknown error") : errorOutput;
            rollback();
            finishSelfUpdate(QStringLiteral("error"),
                             QStringLiteral("Apply failed: %1").arg(errMsg));
            return;
        }
        m_selfUpdateStep = 3;
        runSelfUpdateStep();
        break;
    }
    case 3: {
        // py_compile finished
        if (exitCode != 0) {
            qCWarning(lcNetwork) << "Pre-flight check failed:" << errorOutput;
            // Roll back — new code has syntax errors
            setProgress(QStringLiteral("New code has errors, rolling back..."));
            rollback();
            finishSelfUpdate(QStringLiteral("error"),
                             QStringLiteral("Update rolled back — new code failed syntax check"));
            return;
        }
        m_selfUpdateStep = 4;
        runSelfUpdateStep();
        break;
    }
    case 4: {
        // pip install finished
        QString depWarning;
        if (exitCode != 0) {
            // Log but don't block — the code update already succeeded
            qCWarning(lcNetwork) << "Dependency install had issues:" << errorOutput;
            depWarning = errorOutput.left(120);
        } else {
            qCInfo(lcNetwork) << "Dependencies installed successfully";
        }
        // Store dep warning for final message
        m_selfUpdateStep = 5;
        // Stash warning in progress message temporarily
        if (!depWarning.isEmpty()) {
            QMutexLocker locker(&m_progressLock);
            // We'll check for this in step 5 completion
            m_progressMessage = QStringLiteral("dep_warning:") + depWarning;
        }
        runSelfUpdateStep();
        break;
    }
    case 5: {
        // git log finished — finalize
        QString newInfo = (exitCode == 0) ? output : QString();

        qCInfo(lcNetwork) << "Self-update applied successfully:" << newInfo;
        QString msg = QStringLiteral("Update applied. Now on: %1").arg(newInfo);

        // Check for dep warning stashed in progress
        {
            QMutexLocker locker(&m_progressLock);
            if (m_progressMessage.startsWith(QLatin1String("dep_warning:"))) {
                msg += QStringLiteral("\n(Some dependencies may need manual install)");
                m_progressMessage.clear();
            }
        }

        finishSelfUpdate(QStringLiteral("restart-required"), msg);
        break;
    }
    default:
        finishSelfUpdate(QStringLiteral("error"), QStringLiteral("Internal error: invalid step"));
        break;
    }
}

void NetworkManager::rollback()
{
    if (m_oldCommit.isEmpty())
        return;

    qCInfo(lcNetwork) << "Rolling back to" << m_oldCommit.left(7);

    QProcess rollbackProc;
    rollbackProc.setWorkingDirectory(repoDir());
    rollbackProc.start(QStringLiteral("git"),
                       {QStringLiteral("reset"), QStringLiteral("--hard"), m_oldCommit});
    if (rollbackProc.waitForFinished(30000)) {
        if (rollbackProc.exitCode() == 0) {
            qCInfo(lcNetwork) << "Rolled back to" << m_oldCommit.left(7);
        } else {
            qCWarning(lcNetwork) << "Rollback failed:" <<
                QString::fromUtf8(rollbackProc.readAllStandardError()).trimmed();
        }
    } else {
        qCWarning(lcNetwork) << "Rollback timed out";
    }
}

void NetworkManager::finishSelfUpdate(const QString &status, const QString &message)
{
    m_selfUpdateStatus = status;
    emit selfUpdateStatusChanged(status);
    m_selfUpdateMessage = message;
    emit selfUpdateMessageChanged(message);

    // Clear the update lock
    m_updating = false;

    // Stop the progress poll timer
    m_resultPollTimer->stop();

    // If successful, also update the main update status
    if (status == QLatin1String("restart-required")) {
        m_updateStatus = QStringLiteral("up-to-date");
        emit updateStatusChanged(m_updateStatus);
        m_updateMessage = QStringLiteral("Update applied — restart to complete");
        emit updateMessageChanged(m_updateMessage);
    }
}

void NetworkManager::setProgress(const QString &msg)
{
    QMutexLocker locker(&m_progressLock);
    m_progressMessage = msg;
}

// ---------------------------------------------------------------------------
// Private — Helpers
// ---------------------------------------------------------------------------

QString NetworkManager::repoDir() const
{
    // In C++ build, the repo dir is the application directory's parent
    // (assuming the binary lives in a build/ or bin/ subdirectory).
    // This mirrors the Python approach of going up from backend/ to the repo root.
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(".."));
}

QString NetworkManager::runGitSync(const QStringList &args, bool *ok, int timeoutMs) const
{
    QProcess proc;
    proc.setWorkingDirectory(repoDir());
    proc.start(QStringLiteral("git"), args);
    const bool finished = proc.waitForStarted(timeoutMs) && proc.waitForFinished(timeoutMs);
    const bool success = finished && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (ok)
        *ok = success;
    if (!finished)
        proc.kill();
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

bool NetworkManager::refreshWorkingTreeDirty()
{
    bool ok = false;
    const QString out = runGitSync({QStringLiteral("status"), QStringLiteral("--porcelain")}, &ok);
    // On failure assume dirty: this gates a `git reset --hard`, so the safe
    // default when we cannot tell is the one that refuses to destroy anything.
    const bool dirty = ok ? !out.isEmpty() : true;
    if (dirty != m_workingTreeDirty) {
        m_workingTreeDirty = dirty;
        emit workingTreeDirtyChanged(dirty);
    }
    return dirty;
}

QString NetworkManager::localVsRemote(const QString &remoteShort, const QString &localShort) const
{
    if (remoteShort.isEmpty())
        return QStringLiteral("behind");
    if (remoteShort == localShort)
        return QStringLiteral("same");

    // Do we even have the remote commit locally? If the object is missing we
    // cannot be a descendant of it, so this is a normal "remote moved ahead".
    bool haveRemote = false;
    runGitSync({QStringLiteral("cat-file"), QStringLiteral("-e"),
                remoteShort + QStringLiteral("^{commit}")}, &haveRemote, 5000);

    if (haveRemote) {
        bool isAncestor = false;
        runGitSync({QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"),
                    remoteShort, QStringLiteral("HEAD")}, &isAncestor, 5000);
        if (isAncestor)
            return QStringLiteral("ahead");  // remote commit is already in our history
    }

    // Remote commit is unknown to us. If we also carry commits the last-fetched
    // origin/main lacks, both sides moved — that is a divergence, not an update.
    bool ok = false;
    const QString aheadCount = runGitSync({QStringLiteral("rev-list"), QStringLiteral("--count"),
                                           QStringLiteral("origin/main..HEAD")}, &ok, 5000);
    if (ok && aheadCount.toInt() > 0)
        return QStringLiteral("diverged");

    return QStringLiteral("behind");
}

bool NetworkManager::checkCanSelfUpdate()
{
    // Self-update requires git and must not be a frozen/packaged build.
    // In C++ this checks for the presence of a .git directory and the git command.

#ifdef Q_OS_ANDROID
    return false;
#endif

    // Check if this is a packaged AppImage/Inno/DMG build
    // (QCoreApplication::applicationDirPath() won't have a .git in parent)
    QString gitDir = QDir(repoDir()).filePath(QStringLiteral(".git"));
    if (!QFileInfo::exists(gitDir))
        return false;

    // Check git is available
    QProcess gitCheck;
    gitCheck.start(QStringLiteral("git"), {QStringLiteral("--version")});
    if (!gitCheck.waitForFinished(5000))
        return false;

    return gitCheck.exitCode() == 0;
}

QString NetworkManager::findVenvPip()
{
    // Locate the pip executable inside the project's venv
    QString venvDir = QDir(repoDir()).filePath(QStringLiteral("venv"));

#ifdef Q_OS_WIN
    QString pipPath = QDir(venvDir).filePath(QStringLiteral("Scripts/pip.exe"));
#else
    QString pipPath = QDir(venvDir).filePath(QStringLiteral("bin/pip"));
#endif

    if (QFileInfo::exists(pipPath))
        return pipPath;
    return {};
}
