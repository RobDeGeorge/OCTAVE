#ifndef Q_OS_MOBILE

#include "phonemirrormanager.h"
#include "../phone_mirror/scrcpyclient.h"
#include "../phone_mirror/qrcodegen.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>

Q_LOGGING_CATEGORY(lcPhoneMirror, "octave.phonemirror")

// Default size of the virtual display scrcpy creates on the phone
// (--new-display, Android 11+). Landscape suits a dash screen. scrcpy rounds
// each dimension down to a multiple of 8, so the setting is snapped the same
// way to keep the UI honest.
static const QString kDefaultDisplaySize = QStringLiteral("1280x800");

// ─── Construction ─────────────────────────────────────────────────────

PhoneMirrorManager::PhoneMirrorManager(QObject *parent)
    : QObject(parent), m_displaySize(kDefaultDisplaySize)
{
    m_adbPath = findAdb();
    // Wireless jobs run one at a time, so a reconnect never races a pairing
    m_wirelessPool.setMaxThreadCount(1);
}

PhoneMirrorManager::~PhoneMirrorManager()
{
    cleanup();
    ++m_qrGeneration;   // a QR pairing wait gives up within a second
    m_wirelessPool.clear();
    m_wirelessPool.waitForDone();
}

// ─── Availability / environment ───────────────────────────────────────

bool PhoneMirrorManager::nativeAvailable() const
{
    return ScrcpyClient::available() && !m_adbPath.isEmpty();
}

QString PhoneMirrorManager::serverVersion() const
{
    return QLatin1String(ScrcpyClient::kServerVersion);
}

bool PhoneMirrorManager::environmentOk()
{
    // Everything OCTAVE needs is present, i.e. a failure is about the phone,
    // not the setup (the view then hides the setup text).
    return nativeAvailable();
}

QString PhoneMirrorManager::getInstallInstructions()
{
    QStringList missing;
    if (!ScrcpyClient::available())
        missing << QStringLiteral("a build with libavcodec (the H.264 decoder) and the bundled phone server");
    if (m_adbPath.isEmpty())
        missing << QStringLiteral("adb (run scripts/fetch_platform_tools.py or install android-tools)");
    if (!missing.isEmpty())
        return QStringLiteral("Phone mirroring needs ") + missing.join(QStringLiteral(" and ")) + QLatin1Char('.');
    return QStringLiteral("Enable USB debugging on the phone (Settings > Developer options), connect it "
                          "over USB and accept the authorization prompt. To go wireless, open Phone Mirror and "
                          "scan its QR code from the phone's Wireless debugging screen.");
}

// ─── adb discovery ────────────────────────────────────────────────────

QString PhoneMirrorManager::findAdb()
{
    // Bundled platform-tools first (scripts/fetch_platform_tools.py; CI ships
    // it next to the binary). Dev layout: <repo>/tools/platform-tools/<os>/,
    // deployed: <app dir>/platform-tools/ (or Contents/Resources on macOS).
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("adb.exe");
    const QString osName = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    const QString exe = QStringLiteral("adb");
    const QString osName = QStringLiteral("darwin");
#else
    const QString exe = QStringLiteral("adb");
    const QString osName = QStringLiteral("linux");
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList bundled{
        appDir + QStringLiteral("/platform-tools/") + exe,
        appDir + QStringLiteral("/../tools/platform-tools/") + osName + QLatin1Char('/') + exe,
        appDir + QStringLiteral("/../Resources/platform-tools/") + exe,
    };
    for (const QString &c : bundled) {
        if (QFileInfo::exists(c)) {
            qCDebug(lcPhoneMirror) << "Using bundled platform-tools adb:" << c;
            return QFileInfo(c).absoluteFilePath();
        }
    }
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("adb"));
    if (!inPath.isEmpty())
        return inPath;
#ifdef Q_OS_WIN
    const QString home = QDir::homePath();
    const QStringList common{
        QStringLiteral("C:/Program Files/Android/platform-tools/adb.exe"),
        QStringLiteral("C:/Program Files (x86)/Android/platform-tools/adb.exe"),
        home + QStringLiteral("/AppData/Local/Android/Sdk/platform-tools/adb.exe"),
    };
#else
    const QString home = QDir::homePath();
    const QStringList common{
        QStringLiteral("/usr/bin/adb"),
        QStringLiteral("/usr/local/bin/adb"),
        home + QStringLiteral("/Android/Sdk/platform-tools/adb"),
    };
#endif
    for (const QString &c : common)
        if (QFileInfo::exists(c))
            return c;
    return {};
}

QString PhoneMirrorManager::runAdb(const QStringList &args, int timeoutMs) const
{
    if (m_adbPath.isEmpty())
        return {};
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    ScrcpyClient::hideConsoleWindow(&proc);
    proc.start(m_adbPath, args);
    if (!proc.waitForFinished(timeoutMs) || proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QString PhoneMirrorManager::runAdbFull(const QStringList &args, int timeoutMs, int *exitCode) const
{
    if (exitCode)
        *exitCode = -1;
    if (m_adbPath.isEmpty())
        return {};
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    ScrcpyClient::hideConsoleWindow(&proc);
    proc.start(m_adbPath, args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }
    if (exitCode)
        *exitCode = proc.exitCode();
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QStringList PhoneMirrorManager::serialArgs()
{
    // With a phone on both USB and Wi-Fi (or two phones) a bare `adb shell`
    // fails with "more than one device", so every probe names its phone.
    const QString serial = getDeviceSerial();
    if (serial.isEmpty())
        return {};
    return {QStringLiteral("-s"), serial};
}

QList<QPair<QString, QString>> PhoneMirrorManager::devices() const
{
    QList<QPair<QString, QString>> rows;
    const QStringList lines = runAdb({QStringLiteral("devices")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList parts = lines[i].split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 2)
            rows.append({parts[0], parts[1]});
    }
    return rows;
}

// ─── Device probes ────────────────────────────────────────────────────

QString PhoneMirrorManager::getDeviceState()
{
    if (m_adbPath.isEmpty())
        return QStringLiteral("no-adb");
    QStringList states;
    for (const auto &d : devices())
        states << d.second;
    if (states.contains(QLatin1String("device")))
        return QStringLiteral("device");
    // Nothing usable: if a Wi-Fi phone is remembered, try to reach it in the
    // background (throttled). The view polls this, so it is retried for free.
    maybeReconnectWireless();
    if (states.contains(QLatin1String("unauthorized")))
        return QStringLiteral("unauthorized");
    if (states.contains(QLatin1String("offline")))
        return QStringLiteral("offline");
    return QStringLiteral("none");
}

QString PhoneMirrorManager::describeDeviceState(const QString &state)
{
    if (state == QLatin1String("no-adb"))
        return QStringLiteral("adb not found. Run scripts/fetch_platform_tools.py or install android-tools.");
    if (state == QLatin1String("unauthorized"))
        return QStringLiteral("Phone is connected but USB debugging is not authorized. "
                              "Unlock the phone and tap 'Allow' on the USB debugging prompt.");
    if (state == QLatin1String("offline"))
        return QStringLiteral("Phone is connected but adb reports it offline. Unplug and replug the cable.");
    if (state == QLatin1String("none"))
        return QStringLiteral("No Android device connected. Connect via USB and enable USB debugging.");
    return {};
}

bool PhoneMirrorManager::hasConnectedDevice()
{
    return getDeviceState() == QLatin1String("device");
}

QString PhoneMirrorManager::describeProblem(const QString &state)
{
    // describeDeviceState() with the Wi-Fi case spelled out. The phrases the
    // view keys its auto-retry on (No Android device / not authorized /
    // offline) are kept.
    bool wireless = false;
    if (state == QLatin1String("offline") || state == QLatin1String("unauthorized")) {
        wireless = true;
        for (const auto &d : devices())
            if (d.second == state && !ScrcpyClient::isNetworkSerial(d.first))
                wireless = false;
    }
    if (wireless && state == QLatin1String("offline"))
        return QStringLiteral("The phone's Wi-Fi link is offline. Check that it is on the same Wi-Fi network "
                              "as OCTAVE and that Wireless debugging is on.");
    if (wireless && state == QLatin1String("unauthorized"))
        return QStringLiteral("Phone is connected over Wi-Fi but debugging is not authorized. "
                              "Unlock the phone and tap 'Allow' on the debugging prompt.");
    if (state == QLatin1String("none") && !m_wirelessAddress.isEmpty())
        return QStringLiteral("No Android device connected. Waiting for %1 over Wi-Fi: turn on Wireless "
                              "debugging on the phone (same Wi-Fi network as OCTAVE), or plug it in over USB.")
            .arg(m_wirelessAddress);
    return describeDeviceState(state);
}

QString PhoneMirrorManager::getDeviceSerial()
{
    // USB first: it is the faster, steadier link when the phone is on both
    QString wireless;
    for (const auto &d : devices()) {
        if (d.second != QLatin1String("device"))
            continue;
        if (!ScrcpyClient::isNetworkSerial(d.first))
            return d.first;
        if (wireless.isEmpty())
            wireless = d.first;
    }
    return wireless;
}

QString PhoneMirrorManager::getDeviceName()
{
    return runAdb(serialArgs() + QStringList{QStringLiteral("shell"), QStringLiteral("getprop"),
                                             QStringLiteral("ro.product.model")});
}

QString PhoneMirrorManager::getDeviceResolution()
{
    const QStringList lines = runAdb(serialArgs() + QStringList{QStringLiteral("shell"), QStringLiteral("wm"),
                                                                QStringLiteral("size")})
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.contains(QLatin1String("Physical size:")))
            return line.mid(line.indexOf(QLatin1Char(':')) + 1).trimmed();
    }
    return {};
}

int PhoneMirrorManager::getDeviceSdk()
{
    bool ok = false;
    const int sdk = runAdb(serialArgs() + QStringList{QStringLiteral("shell"), QStringLiteral("getprop"),
                                                      QStringLiteral("ro.build.version.sdk")}).toInt(&ok);
    return ok ? sdk : 0;
}

void PhoneMirrorManager::killStaleServer()
{
    // A crashed session can leave the device-side server running
    if (!m_adbPath.isEmpty()) {
        QProcess proc;
        proc.setProgram(m_adbPath);
        QStringList args;
        if (!m_serial.isEmpty())
            args << QStringLiteral("-s") << m_serial;
        args << QStringLiteral("shell") << QStringLiteral("pkill")
             << QStringLiteral("-f") << QLatin1String(ScrcpyClient::kServerProcessPattern);
        proc.setArguments(args);
        ScrcpyClient::hideConsoleWindow(&proc);
        proc.startDetached();
    }
}

// ─── Settings ─────────────────────────────────────────────────────────

void PhoneMirrorManager::setVolume(float volume)
{
    m_volume = volume;
    if (m_client)
        m_client->setVolume(volume);
}

void PhoneMirrorManager::setAudioGain(float gain)
{
    m_audioGain = qBound(0.25f, gain, 8.0f);
    if (m_client)
        m_client->setAudioGain(m_audioGain);
}

bool PhoneMirrorManager::audioActive() const
{
    return m_client && m_client->audioActive();
}

bool PhoneMirrorManager::audioPlaying() const
{
    return m_audioPlaying;
}

float PhoneMirrorManager::duckingFactor() const
{
    return m_duckFactor;
}

void PhoneMirrorManager::setAudioDuckEnabled(bool enabled)
{
    if (enabled == m_duckEnabled)
        return;
    m_duckEnabled = enabled;
    updateDucking();
}

void PhoneMirrorManager::setAudioDuckLevel(float level)
{
    m_duckLevel = qBound(0.0f, level, 1.0f);
    updateDucking();
}

void PhoneMirrorManager::updateDucking()
{
    const float factor = (m_duckEnabled && m_audioPlaying) ? m_duckLevel : 1.0f;
    if (qFuzzyCompare(factor, m_duckFactor))
        return;
    m_duckFactor = factor;
    qCDebug(lcPhoneMirror) << "ducking factor" << factor;
    emit duckingChanged(factor);
}

void PhoneMirrorManager::setAudioEnabled(bool enabled)
{
    // Play the phone's audio through OCTAVE (setting scrcpyAudioEnabled).
    // Restarts the session if one is running, like the display size.
    if (enabled == m_audioEnabled)
        return;
    m_audioEnabled = enabled;
    if (isRunning()) {
        stopScrcpy();
        QTimer::singleShot(300, this, &PhoneMirrorManager::startScrcpy);
    }
}

QString PhoneMirrorManager::normalizeDisplaySize(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+)\\s*[xX]\\s*(\\d+)\\s*$"));
    const auto m = re.match(value);
    if (!m.hasMatch())
        return {};
    int w = m.captured(1).toInt();
    int h = m.captured(2).toInt();
    w = qMax(8, (w / 8) * 8);
    h = qMax(8, (h / 8) * 8);
    return QStringLiteral("%1x%2").arg(w).arg(h);
}

void PhoneMirrorManager::setDisplaySize(const QString &size)
{
    const QString norm = normalizeDisplaySize(size);
    if (norm != size.trimmed())
        qCInfo(lcPhoneMirror) << "Display size" << size << "normalized to" << norm
                              << "(scrcpy rounds to multiples of 8)";
    if (norm == m_displaySize)
        return;
    m_displaySize = norm;
    emit displaySizeChanged();
    if (isRunning()) {
        stopScrcpy();
        QTimer::singleShot(300, this, &PhoneMirrorManager::startScrcpy);
    }
}

void PhoneMirrorManager::setVideoSink(QObject *sink)
{
    m_videoSink = sink;
    if (m_client)
        m_client->setVideoSink(sink);
    emit videoSinkChanged();
}

// ─── Input ────────────────────────────────────────────────────────────

void PhoneMirrorManager::injectTouch(int pointerId, int action, float relX, float relY)
{
    if (!m_client || !m_client->isRunning())
        return;
    m_client->injectTouch(pointerId, action,
                          int(relX * m_client->frameWidth()), int(relY * m_client->frameHeight()));
}

void PhoneMirrorManager::injectKey(int keycode)
{
    if (m_client && m_client->isRunning())
        m_client->pressKey(keycode);
}

void PhoneMirrorManager::pressHome()
{
    // HOME is a system key that Android routes to the default display, so on
    // a virtual display it does nothing; launch the launcher there instead.
    if (m_vdisplayId >= 0 && !m_serial.isEmpty() && !m_adbPath.isEmpty()) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
        ScrcpyClient::hideConsoleWindow(proc);
        proc->start(m_adbPath, {QStringLiteral("-s"), m_serial, QStringLiteral("shell"), QStringLiteral("am"),
                                QStringLiteral("start"), QStringLiteral("--display"), QString::number(m_vdisplayId),
                                QStringLiteral("-a"), QStringLiteral("android.intent.action.MAIN"),
                                QStringLiteral("-c"), QStringLiteral("android.intent.category.HOME")});
        return;
    }
    injectKey(ScrcpyClient::KeycodeHome);
}
void PhoneMirrorManager::pressBack()      { injectKey(ScrcpyClient::KeycodeBack); }
void PhoneMirrorManager::pressAppSwitch() { injectKey(ScrcpyClient::KeycodeAppSwitch); }

// ─── Session lifecycle ────────────────────────────────────────────────

bool PhoneMirrorManager::isRunning() const
{
    return m_client && m_client->isRunning();
}

void PhoneMirrorManager::startScrcpy()
{
    if (isRunning()) {
        if (m_ready)
            emit scrcpyStarted(1);
        return;
    }
    if (m_isStarting)
        return;
    if (!nativeAvailable()) {
        emit scrcpyError(getInstallInstructions());
        return;
    }
    const QString state = getDeviceState();
    if (state != QLatin1String("device")) {
        emit scrcpyError(describeProblem(state));
        return;
    }
    const QString serial = getDeviceSerial();
    const bool wireless = ScrcpyClient::isNetworkSerial(serial);
    const QString jar = ScrcpyClient::bundledServerJar();
    if (jar.isEmpty()) {
        emit scrcpyError(QStringLiteral("Bundled scrcpy server could not be extracted"));
        return;
    }

    m_isStarting = true;
    m_isStopping = false;
    m_ready = false;
    m_activeDisplaySize = m_displaySize;
    QString attachScid;
    if (!m_persistScid.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_persistUntilMs)
        attachScid = m_persistScid;
    m_attaching = !attachScid.isEmpty();
    if (!m_attaching) {
        m_persistScid.clear();
        runAdb({QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("pkill"), QStringLiteral("-f"),
                QLatin1String(ScrcpyClient::kServerProcessPattern)}, 5000);  // clear a stale server first
    }

    if (m_client) {
        m_client->stop();
        m_client->deleteLater();
    }
    m_client = new ScrcpyClient(m_adbPath, jar, this);
    m_client->setVideoSink(m_videoSink);
    connect(m_client, &ScrcpyClient::connected, this, &PhoneMirrorManager::onConnected, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::disconnected, this, &PhoneMirrorManager::onDisconnected, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::frameSizeChanged, this, [this](int w, int h) {
        if (w != m_frameWidth || h != m_frameHeight) {
            m_frameWidth = w; m_frameHeight = h;
            emit frameSizeChanged(w, h);
        }
    }, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::frameReady, this, &PhoneMirrorManager::frameReady, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::serverLog, this, &PhoneMirrorManager::onServerLog, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::phoneStateChanged, this, &PhoneMirrorManager::onPhoneState, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::audioStateChanged, this, &PhoneMirrorManager::audioActiveChanged, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::audioPlayingChanged, this, [this](bool playing) {
        if (playing == m_audioPlaying)
            return;
        m_audioPlaying = playing;
        emit audioPlayingChanged(playing);
        updateDucking();
    });
    m_client->setAudioGain(m_audioGain);
    m_client->setVolume(m_volume);

    QString displaySize = m_displaySize;
    if (!displaySize.isEmpty()) {
        const int sdk = getDeviceSdk();
        if (sdk > 0 && sdk < kNewDisplayMinSdk) {
            qCWarning(lcPhoneMirror) << "Device SDK" << sdk << "<" << kNewDisplayMinSdk
                                     << ": virtual display unavailable, mirroring phone screen";
            displaySize.clear();
            m_activeDisplaySize.clear();
        }
    }
    qCInfo(lcPhoneMirror) << "Starting phone mirror (server" << serverVersion() << ") for" << serial
                          << "(display" << (displaySize.isEmpty() ? QStringLiteral("phone screen") : displaySize)
                          << ", audio" << (m_audioEnabled ? "on" : "off")
                          << "," << (wireless ? "Wi-Fi" : "USB") << ")";
    m_serial = serial;
    setConnectionType(wireless ? QStringLiteral("wifi") : QStringLiteral("usb"));
    if (!m_attaching)
        m_vdisplayId = -1;
    else
        qCInfo(lcPhoneMirror) << "Reattaching to the phone's running mirror session (scid" << attachScid << ")";
    m_client->start(serial, displaySize, 60, wireless ? kWirelessBitRate : 8000000, m_audioEnabled, true, attachScid);
    emit isRunningChanged();
}

void PhoneMirrorManager::onConnected(int w, int h)
{
    m_ready = true;
    m_isStarting = false;
    m_attaching = false;
    m_persistScid.clear();
    m_frameWidth = w; m_frameHeight = h;
    if (!m_activeDisplaySize.isEmpty())
        m_activeDisplaySize = QStringLiteral("%1x%2").arg(w).arg(h);
    emit frameSizeChanged(w, h);
    qCInfo(lcPhoneMirror) << "Phone mirror connected:" << w << "x" << h;
    emit scrcpyStarted(1);
    // Hand the sleep/panel policy to the phone-side keeper. The server
    // powers the device on asynchronously at start; give it a moment.
    QTimer::singleShot(500, this, &PhoneMirrorManager::sendKeeperPolicy);
}

void PhoneMirrorManager::onDisconnected(const QString &reason)
{
    if (m_isStopping)
        return;
    resetPhoneState();
    const bool wasReady = m_ready;
    m_ready = false;
    m_isStarting = false;
    if (m_attaching) {
        // The persisted server was gone (or the link is still down): start fresh
        m_attaching = false;
        m_persistScid.clear();
        qCInfo(lcPhoneMirror) << "Reattach failed (" << reason << "); starting a new session";
        emit isRunningChanged();
        QTimer::singleShot(0, this, &PhoneMirrorManager::startScrcpy);
        return;
    }
    if (wasReady && !m_activeDisplaySize.isEmpty() && m_client && !m_client->scid().isEmpty()) {
        // Link drop mid-stream: the server keeps the virtual display for kPersistMs
        m_persistScid = m_client->scid();
        m_persistUntilMs = QDateTime::currentMSecsSinceEpoch() + ScrcpyClient::kPersistMs - 5000;
    }
    setConnectionType(QString());
    emit isRunningChanged();
    if (reason.isEmpty())
        emit scrcpyStopped();
    else
        emit scrcpyError(reason);
}

void PhoneMirrorManager::stopScrcpy()
{
    qCDebug(lcPhoneMirror) << "Stopping phone mirror";
    m_isStopping = true;
    m_isStarting = false;
    m_ready = false;
    m_attaching = false;
    m_persistScid.clear();
    m_activeDisplaySize.clear();
    resetPhoneState();
    if (m_client) {
        ScrcpyClient *client = m_client;
        m_client = nullptr;
        client->stop();
        client->deleteLater();
        killStaleServer();
    }
    setConnectionType(QString());
    emit scrcpyStopped();
    emit isRunningChanged();
}

// ── phone screen / sleep ──────────────────────────────────────────────
// The phone-side MirrorKeeper (phone_server/.../control/MirrorKeeper.java)
// does the work: it watches the phone's power state and keyguard from inside
// the device, wakes it on a power press (the virtual display has no power
// group of its own and dozes with the phone), blanks the panel per the
// setting, and decides "in use" from the keyguard being dismissed. OCTAVE
// only sets the policy, takes the phone back, and shows the state.

// After a power press from the locked state the keeper wakes the phone but
// leaves its panel lit for this long; a keyguard dismissal in that window
// means the user wants their phone, otherwise the panel is blanked again.
static constexpr int kWakeGraceMs = 8000;

void PhoneMirrorManager::setPhoneScreenOff(bool off)
{
    if (off == m_phoneScreenOff)
        return;
    m_phoneScreenOff = off;
    sendKeeperPolicy();
}

void PhoneMirrorManager::sendKeeperPolicy()
{
    if (m_client && m_client->isRunning() && m_ready)
        m_client->setKeeper(true, m_phoneScreenOff, kWakeGraceMs);
}

void PhoneMirrorManager::wakePhone()
{
    if (m_serial.isEmpty() || m_adbPath.isEmpty())
        return;
    qCInfo(lcPhoneMirror) << "Waking phone";
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    ScrcpyClient::hideConsoleWindow(proc);
    proc->start(m_adbPath, {QStringLiteral("-s"), m_serial, QStringLiteral("shell"), QStringLiteral("input"),
                            QStringLiteral("keyevent"), QString::number(ScrcpyClient::KeycodeWakeup)});
}

void PhoneMirrorManager::resumeMirroring()
{
    if (m_client && m_client->isRunning())
        m_client->takeBack();
}

void PhoneMirrorManager::onPhoneState(bool asleep, bool inUse, bool panelDark)
{
    if (asleep != m_phoneAsleep) {
        m_phoneAsleep = asleep;
        if (m_client)
            m_client->setHoldBlack(asleep);   // keep the last good frame on the dash
        qCInfo(lcPhoneMirror) << (asleep ? "Phone went to sleep; the keeper is waking it" : "Phone is awake again");
        emit phoneAsleepChanged(asleep);
    }
    if (inUse != m_phoneInUse) {
        m_phoneInUse = inUse;
        qCInfo(lcPhoneMirror) << (inUse ? "Phone unlocked by the user; leaving its screen alone"
                                        : "Phone handed back to the mirror");
        emit phoneInUseChanged(inUse);
    }
    m_panelDark = panelDark;
}

void PhoneMirrorManager::resetPhoneState()
{
    if (m_phoneInUse) {
        m_phoneInUse = false;
        emit phoneInUseChanged(false);
    }
    if (m_phoneAsleep) {
        m_phoneAsleep = false;
        emit phoneAsleepChanged(false);
    }
    m_panelDark = false;
}

void PhoneMirrorManager::onServerLog(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("New display: .*\\(id=(\\d+)\\)"));
    const auto m = re.match(line);
    if (m.hasMatch())
        m_vdisplayId = m.captured(1).toInt();
}

void PhoneMirrorManager::cleanup()
{
    stopScrcpy();
}

// ── Wi-Fi ─────────────────────────────────────────────────────────────
// adb itself carries the session over TCP: once the phone is listed by
// `adb devices` as "ip:port" (adb connect) or as an mDNS name (adb
// auto-connects phones it has paired with), the client's `adb -s <serial>`
// push / forward / shell work exactly as over USB. This code only gets the
// phone into that list and keeps it there.

void PhoneMirrorManager::setConnectionType(const QString &type)
{
    if (type == m_connectionType)
        return;
    m_connectionType = type;
    emit connectionTypeChanged();
}

// "192.168.1.5:5555" -> "192.168.1.5", "[fe80::1]:5555" -> "fe80::1"
static QString hostOf(const QString &address)
{
    QString host = address.trimmed();
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && host.indexOf(QLatin1Char(':')) == colon)
        host = host.left(colon);
    else if (host.startsWith(QLatin1Char('[')) && host.contains(QLatin1String("]:")))
        host = host.mid(1, host.indexOf(QLatin1String("]:")) - 1);
    return host;
}

QList<PhoneMirrorManager::MdnsService> PhoneMirrorManager::mdnsServices() const
{
    // "adb-R5CT…-AbCdEf  _adb-tls-connect._tcp  192.168.1.5:37199" per line
    QList<MdnsService> out;
    const QStringList lines = runAdbFull({QStringLiteral("mdns"), QStringLiteral("services")}, 5000)
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString &line : lines) {
        const QStringList parts = line.trimmed().split(ws, Qt::SkipEmptyParts);
        if (parts.size() < 3 || !parts[1].startsWith(QLatin1String("_adb")))
            continue;
        out.append({parts[0], parts[1], parts[2]});
    }
    return out;
}

QString PhoneMirrorManager::mdnsConnectAddressFor(const QString &host) const
{
    for (const MdnsService &s : mdnsServices())
        if (s.type.contains(QLatin1String("_adb-tls-connect")) && hostOf(s.address) == host)
            return s.address;
    return {};
}

bool PhoneMirrorManager::adbConnect(const QString &address, QString *message) const
{
    const QString out = runAdbFull({QStringLiteral("connect"), address}, 10000);
    // adb connect exits 0 on failure too; "already connected to" counts as success
    const bool ok = out.contains(QLatin1String("connected to")) && !out.contains(QLatin1String("failed"))
                    && !out.contains(QLatin1String("cannot"));
    if (message)
        *message = out.isEmpty() ? QStringLiteral("No answer from %1").arg(address) : out.section(QLatin1Char('\n'), -1);
    return ok;
}

void PhoneMirrorManager::runWirelessJob(std::function<void()> job, bool ui)
{
    if (ui) {
        if (m_wirelessJobs++ == 0)
            emit wirelessBusyChanged();
    }
    m_wirelessPool.start([this, job = std::move(job), ui]() {
        job();
        if (ui) {
            QMetaObject::invokeMethod(this, [this]() {
                if (--m_wirelessJobs == 0)
                    emit wirelessBusyChanged();
            }, Qt::QueuedConnection);
        }
    });
}

void PhoneMirrorManager::setWirelessAddress(const QString &address)
{
    const QString value = address.trimmed();
    if (value == m_wirelessAddress)
        return;
    m_wirelessAddress = value;
    emit wirelessAddressChanged(value);
    if (!value.isEmpty()) {
        m_lastAutoConnectMs = 0;
        maybeReconnectWireless();
    }
}

void PhoneMirrorManager::maybeReconnectWireless()
{
    static constexpr qint64 kAutoConnectIntervalMs = 5000;
    if (m_wirelessAddress.isEmpty() || m_adbPath.isEmpty() || m_wirelessJobs > 0 || m_autoConnectRunning)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastAutoConnectMs < kAutoConnectIntervalMs)
        return;
    m_lastAutoConnectMs = now;
    m_autoConnectRunning = true;
    const QString saved = m_wirelessAddress;
    runWirelessJob([this, saved]() {
        QString msg;
        bool ok = adbConnect(saved, &msg);
        QString reached = ok ? saved : QString();
        if (!ok) {
            // Wireless debugging picks a new port every time it is switched
            // on; the phone advertises the current one over mDNS.
            const QString fresh = mdnsConnectAddressFor(hostOf(saved));
            if (!fresh.isEmpty() && fresh != saved && adbConnect(fresh, &msg))
                reached = fresh;
        }
        if (!reached.isEmpty())
            qCInfo(lcPhoneMirror) << "Reconnected to the phone over Wi-Fi at" << reached;
        else
            qCDebug(lcPhoneMirror) << "Wi-Fi phone" << saved << "not reachable:" << msg;
        QMetaObject::invokeMethod(this, [this, saved, reached]() {
            m_autoConnectRunning = false;
            // The phone moved to a new port: remember that one
            if (!reached.isEmpty() && reached != saved && m_wirelessAddress == saved) {
                m_wirelessAddress = reached;
                emit wirelessAddressChanged(reached);
            }
        }, Qt::QueuedConnection);
    }, false);
}

QString PhoneMirrorManager::connectPairedHost(const QString &host) const
{
    // The connect port differs from the pairing port; the phone advertises
    // it over mDNS. adb may already have auto-connected (then "already
    // connected" is the answer).
    QString msg;
    for (int i = 0; i < 16; ++i) {
        const QString connectAddr = mdnsConnectAddressFor(host);
        if (!connectAddr.isEmpty() && adbConnect(connectAddr, &msg))
            return connectAddr;
        QThread::msleep(500);
    }
    return {};
}

void PhoneMirrorManager::forgetWireless()
{
    const QString addr = m_wirelessAddress;
    if (m_connectionType == QLatin1String("wifi"))
        stopScrcpy();
    m_wirelessAddress.clear();
    emit wirelessAddressChanged(QString());
    if (addr.isEmpty())
        return;
    qCInfo(lcPhoneMirror) << "Forgetting the Wi-Fi phone at" << addr;
    runWirelessJob([this, addr]() {
        runAdbFull({QStringLiteral("disconnect"), addr}, 5000);
    }, false);
}

// ── QR pairing ──
// Android Studio's flow: the host shows WIFI:T:ADB;S:<name>;P:<password>;;
// the phone's "Pair device with QR code" scanner reads it and advertises an
// _adb-tls-pairing service under <name>; the host then runs
// `adb pair <ip:port> <password>` against it and connects as usual.

QString PhoneMirrorManager::qrPairingPayload(const QString &service, const QString &password)
{
    return QStringLiteral("WIFI:T:ADB;S:%1;P:%2;;").arg(service, password);
}

static QString randomToken(int length)
{
    static const char kChars[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString out;
    for (int i = 0; i < length; ++i)
        out += QLatin1Char(kChars[QRandomGenerator::system()->bounded(int(sizeof(kChars) - 1))]);
    return out;
}

void PhoneMirrorManager::startQrPairing()
{
    if (m_adbPath.isEmpty()) {
        emit wirelessResult(false, describeDeviceState(QStringLiteral("no-adb")));
        return;
    }
    const QString service = QStringLiteral("octave-") + randomToken(8);
    const QString password = randomToken(12);
    const QByteArray payload = qrPairingPayload(service, password).toUtf8();
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(payload.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    QStringList rows;
    for (int y = 0; y < qr.getSize(); ++y) {
        QString row;
        for (int x = 0; x < qr.getSize(); ++x)
            row += qr.getModule(x, y) ? QLatin1Char('1') : QLatin1Char('0');
        rows << row;
    }
    m_qrRows = rows;
    emit qrPairingChanged();
    const int generation = ++m_qrGeneration;
    qCInfo(lcPhoneMirror) << "QR pairing: waiting for the phone to scan (service" << service << ")";

    runWirelessJob([this, service, password, generation]() {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + kQrPairingTimeoutMs;
        QString pairAddr;
        while (pairAddr.isEmpty() && m_qrGeneration == generation
               && QDateTime::currentMSecsSinceEpoch() < deadline) {
            for (const MdnsService &s : mdnsServices())
                if (s.name == service && s.type.contains(QLatin1String("_adb-tls-pairing")))
                    pairAddr = s.address;
            if (pairAddr.isEmpty())
                QThread::msleep(1000);
        }
        if (m_qrGeneration != generation)
            return;   // cancelled or restarted: the newer request reports
        bool ok = false;
        QString reached, detail;
        if (pairAddr.isEmpty()) {
            detail = QStringLiteral("The phone did not scan the code within %1 s.").arg(kQrPairingTimeoutMs / 1000);
        } else {
            const QString out = runAdbFull({QStringLiteral("pair"), pairAddr, password}, 20000);
            if (out.contains(QLatin1String("Successfully paired"))) {
                ok = true;
                reached = connectPairedHost(hostOf(pairAddr));
            } else {
                detail = out.isEmpty() ? QStringLiteral("no answer") : out.section(QLatin1Char('\n'), -1);
            }
        }
        QMetaObject::invokeMethod(this, [this, generation, ok, reached, detail, pairAddr]() {
            if (m_qrGeneration != generation)
                return;
            finishQrPairing();
            if (!ok) {
                qCWarning(lcPhoneMirror) << "QR pairing failed:" << detail;
                emit wirelessResult(false, pairAddr.isEmpty()
                    ? detail + QStringLiteral(" Check that the phone is on the same Wi-Fi network and try again.")
                    : QStringLiteral("Pairing failed. Try again. (%1)").arg(detail));
            } else if (reached.isEmpty()) {
                emit wirelessResult(false, QStringLiteral("Paired, but the phone did not accept a connection. Keep its "
                                                          "Wireless debugging screen open and try again."));
            } else {
                qCInfo(lcPhoneMirror) << "QR pairing: paired and connected over Wi-Fi at" << reached;
                setWirelessAddress(reached);
                emit wirelessResult(true, QStringLiteral("Paired and connected to %1 over Wi-Fi.").arg(reached));
            }
        }, Qt::QueuedConnection);
    }, true);
}

void PhoneMirrorManager::cancelQrPairing()
{
    if (m_qrRows.isEmpty())
        return;
    ++m_qrGeneration;   // the waiting job notices within a second and returns
    finishQrPairing();
}

void PhoneMirrorManager::finishQrPairing()
{
    if (m_qrRows.isEmpty())
        return;
    m_qrRows.clear();
    emit qrPairingChanged();
}

#endif // Q_OS_MOBILE
