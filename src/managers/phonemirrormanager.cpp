#ifndef Q_OS_MOBILE

#include "phonemirrormanager.h"
#include "../phone_mirror/scrcpyclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

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
}

PhoneMirrorManager::~PhoneMirrorManager()
{
    cleanup();
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
                          "over USB and accept the authorization prompt.");
}

// ─── adb discovery ────────────────────────────────────────────────────

QString PhoneMirrorManager::findAdb() const
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
    proc.start(m_adbPath, args);
    if (!proc.waitForFinished(timeoutMs) || proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
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

QString PhoneMirrorManager::getDeviceSerial()
{
    for (const auto &d : devices())
        if (d.second == QLatin1String("device"))
            return d.first;
    return {};
}

QString PhoneMirrorManager::getDeviceName()
{
    return runAdb({QStringLiteral("shell"), QStringLiteral("getprop"), QStringLiteral("ro.product.model")});
}

QString PhoneMirrorManager::getDeviceResolution()
{
    const QStringList lines = runAdb({QStringLiteral("shell"), QStringLiteral("wm"), QStringLiteral("size")})
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
    const int sdk = runAdb({QStringLiteral("shell"), QStringLiteral("getprop"),
                            QStringLiteral("ro.build.version.sdk")}).toInt(&ok);
    return ok ? sdk : 0;
}

void PhoneMirrorManager::killStaleServer()
{
    // A crashed session can leave the device-side server running
    if (!m_adbPath.isEmpty())
        QProcess::startDetached(m_adbPath, {QStringLiteral("shell"), QStringLiteral("pkill"),
                                            QStringLiteral("-f"), QLatin1String(ScrcpyClient::kServerProcessPattern)});
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
        emit scrcpyError(describeDeviceState(state));
        return;
    }
    const QString serial = getDeviceSerial();
    const QString jar = ScrcpyClient::bundledServerJar();
    if (jar.isEmpty()) {
        emit scrcpyError(QStringLiteral("Bundled scrcpy server could not be extracted"));
        return;
    }

    m_isStarting = true;
    m_isStopping = false;
    m_ready = false;
    m_activeDisplaySize = m_displaySize;
    runAdb({QStringLiteral("shell"), QStringLiteral("pkill"), QStringLiteral("-f"),
            QLatin1String(ScrcpyClient::kServerProcessPattern)}, 5000);  // clear a stale server first

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
                          << ", audio" << (m_audioEnabled ? "on" : "off") << ")";
    m_serial = serial;
    m_vdisplayId = -1;
    m_client->start(serial, displaySize, 60, 8000000, m_audioEnabled, true);
    emit isRunningChanged();
}

void PhoneMirrorManager::onConnected(int w, int h)
{
    m_ready = true;
    m_isStarting = false;
    m_frameWidth = w; m_frameHeight = h;
    if (!m_activeDisplaySize.isEmpty())
        m_activeDisplaySize = QStringLiteral("%1x%2").arg(w).arg(h);
    emit frameSizeChanged(w, h);
    qCInfo(lcPhoneMirror) << "Phone mirror connected:" << w << "x" << h;
    emit scrcpyStarted(1);
    // The first wakefulness poll (1 s in) decides the initial panel state.
    startWakeWatch();
    QTimer::singleShot(1000, this, &PhoneMirrorManager::pollWakefulness);
}

void PhoneMirrorManager::onDisconnected(const QString &reason)
{
    if (m_isStopping)
        return;
    stopWakeWatch();
    m_ready = false;
    m_isStarting = false;
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
    m_activeDisplaySize.clear();
    stopWakeWatch();
    if (m_client) {
        ScrcpyClient *client = m_client;
        m_client = nullptr;
        client->stop();
        client->deleteLater();
        killStaleServer();
    }
    emit scrcpyStopped();
    emit isRunningChanged();
}

// ── phone screen / sleep ──────────────────────────────────────────────
// A locked (dozing) phone keeps the stream open but stops compositing the
// virtual display and drops injected touch, with no signal on the wire, so
// the only way to notice is to ask (`dumpsys power`) every couple of seconds.
static constexpr int kWakePollIntervalMs = 1000;
// After a power press the phone is woken but its panel is left lit for this
// long. If the user unlocks it in that window they want their phone, not the
// mirror's screen-off; otherwise the panel is blanked again.
static constexpr int kWakeGraceMs = 5000;

void PhoneMirrorManager::setPhoneScreenOff(bool off)
{
    if (off == m_phoneScreenOff)
        return;
    m_phoneScreenOff = off;
    if (m_client && m_client->isRunning() && m_ready)
        setPanel(!off);
}

// Send SET_DISPLAY_POWER once per state change; the server logs each one.
void PhoneMirrorManager::setPanel(bool on)
{
    if (!m_client || !m_client->isRunning())
        return;
    if (m_panelOff == !on)
        return;
    m_panelOff = !on;
    m_client->setDisplayPower(on);
}

void PhoneMirrorManager::applyScreenOff()
{
    if (m_ready && m_phoneScreenOff)
        setPanel(false);
}

void PhoneMirrorManager::wakePhone()
{
    if (m_serial.isEmpty() || m_adbPath.isEmpty())
        return;
    qCInfo(lcPhoneMirror) << "Waking phone";
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    proc->start(m_adbPath, {QStringLiteral("-s"), m_serial, QStringLiteral("shell"), QStringLiteral("input"),
                            QStringLiteral("keyevent"), QString::number(ScrcpyClient::KeycodeWakeup)});
}

void PhoneMirrorManager::startWakeWatch()
{
    if (!m_wakePoll.isActive()) {
        m_grace.setSingleShot(true);
        m_grace.setInterval(kWakeGraceMs);
        connect(&m_grace, &QTimer::timeout, this, &PhoneMirrorManager::onGraceExpired, Qt::UniqueConnection);
        m_wakePoll.setInterval(kWakePollIntervalMs);
        connect(&m_wakePoll, &QTimer::timeout, this, &PhoneMirrorManager::pollWakefulness, Qt::UniqueConnection);
        m_wakePoll.start();
    }
}

void PhoneMirrorManager::stopWakeWatch()
{
    m_wakePoll.stop();
    m_grace.stop();
    setInUse(false);
    m_prevLocked = -2;
    m_blankOnWake = false;
    m_panelOff = false;
    if (m_phoneAsleep) {
        m_phoneAsleep = false;
        emit phoneAsleepChanged(false);
    }
}

void PhoneMirrorManager::pollWakefulness()
{
    if (m_wakeProbeBusy || m_serial.isEmpty() || m_adbPath.isEmpty())
        return;
    m_wakeProbeBusy = true;
    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this, [this, proc](int exitCode, QProcess::ExitStatus) {
        m_wakeProbeBusy = false;
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        if (exitCode != 0)
            return;
        static const QRegularExpression re(QStringLiteral("mWakefulness=(\\w+)"));
        // The current user's line; a work profile / Secure Folder prints its
        // own deviceLocked, which is not the keyguard we care about.
        static const QRegularExpression reLockCurrent(QStringLiteral("\\(current\\)[^\\n]*deviceLocked=(\\d)"));
        static const QRegularExpression reLock(QStringLiteral("deviceLocked=(\\d)"));
        const auto m = re.match(out);
        auto lk = reLockCurrent.match(out);
        if (!lk.hasMatch())
            lk = reLock.match(out);
        if (m.hasMatch())
            onWakefulness(m.captured(1), lk.hasMatch() ? lk.captured(1).toInt() : -1);
    });
    QTimer::singleShot(5000, proc, [proc] { if (proc->state() != QProcess::NotRunning) proc->kill(); });
    // One adb round trip per poll: wakefulness plus the keyguard state
    // (`dumpsys trust` prints deviceLocked=0 once the user has authenticated).
    // No `grep -m1`: an early pipe close makes dumpsys log an error on the phone.
    proc->start(m_adbPath, {QStringLiteral("-s"), m_serial, QStringLiteral("shell"),
                            QStringLiteral("dumpsys power | grep mWakefulness=; dumpsys trust | grep deviceLocked")});
}

void PhoneMirrorManager::onWakefulness(const QString &state, int locked)
{
    if (!m_client || !m_client->isRunning() || !m_wakePoll.isActive())
        return;
    const bool asleep = state != QLatin1String("Awake");
    if (asleep != m_phoneAsleep) {
        m_phoneAsleep = asleep;
        emit phoneAsleepChanged(asleep);
        if (asleep) {
            // stay_awake means the phone never idles off: this is a power
            // press. Wake it, then let the grace window tell us whether the
            // user wanted the screen off or wanted their phone -- unless
            // they were using it, in which case they are putting it down.
            m_grace.stop();
            m_blankOnWake = m_phoneInUse && m_phoneScreenOff;
            setInUse(false);
            if (m_client)
                m_client->setHoldBlack(true);   // keep the last good frame on the dash
            m_panelOff = false;   // the wake powers the panel on; our mode is reverted
            qCInfo(lcPhoneMirror) << "Phone went to sleep (" << state << "); waking it";
            if (m_blankOnWake)
                applyScreenOff();   // may survive the wake; re-applied below if not
            wakePhone();
        } else {
            qCInfo(lcPhoneMirror) << "Phone is awake again";
            if (m_client)
                m_client->setHoldBlack(false);
            if (m_blankOnWake) {
                m_blankOnWake = false;
                applyScreenOff();
            } else if (m_phoneScreenOff && !m_phoneInUse) {
                m_grace.start();
            }
        }
        return;
    }
    if (asleep)
        return;
    const int prev = m_prevLocked;
    if (locked >= 0)
        m_prevLocked = locked;
    if (prev == -2) {
        // First poll of the session: an unlocked phone is in the user's
        // hand, a locked one is in the cradle and gets the screen-off.
        if (locked == 0)
            setInUse(true);
        else
            applyScreenOff();
        return;
    }
    // Edge-triggered on the unlock itself (1 -> 0). Blanking the panel does
    // not lock the phone, so a level check would undo resumeMirroring() on
    // the next poll; and phones with a lock-after delay stay deviceLocked=0
    // for a while after a power press, which is not an unlock either.
    if (locked == 0 && prev == 1 && !m_phoneInUse) {
        m_grace.stop();
        setInUse(true);
    } else if (locked == 1 && prev == 0 && m_phoneInUse) {
        // Locked without sleeping (lock shortcut / lock-after delay elapsed)
        setInUse(false);
        if (m_phoneScreenOff)
            m_grace.start();
    }
}

void PhoneMirrorManager::setInUse(bool inUse)
{
    if (inUse == m_phoneInUse)
        return;
    m_phoneInUse = inUse;
    if (inUse && m_ready)
        setPanel(true);   // they are holding it: never leave it dark
    qCInfo(lcPhoneMirror) << (inUse ? "Phone unlocked by the user; leaving its screen alone"
                                    : "Phone handed back to the mirror");
    emit phoneInUseChanged(inUse);
}

void PhoneMirrorManager::onGraceExpired()
{
    if (!m_phoneInUse && !m_phoneAsleep)
        applyScreenOff();
}

void PhoneMirrorManager::resumeMirroring()
{
    setInUse(false);
    m_grace.stop();
    if (m_phoneAsleep)
        wakePhone();
    else
        applyScreenOff();
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

#endif // Q_OS_MOBILE
