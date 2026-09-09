#ifndef Q_OS_MOBILE

#include "phonemirrormanager.h"
#include "../phone_mirror/scrcpyclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <signal.h>
#endif

Q_LOGGING_CATEGORY(lcPhoneMirror, "octave.phonemirror")

// Default v4l2loopback node on Linux
// (modprobe v4l2loopback exclusive_caps=0 card_label=OCTAVE video_nr=10)
static const QString kDefaultVideoDevice = QStringLiteral("/dev/video10");
// Landscape virtual display for the dash. scrcpy rounds each dimension down
// to a multiple of 8 (1080x2316 becomes 1080x2312), so the setting is snapped
// the same way; the reader still probes the node for the real size.
static const QString kDefaultDisplaySize = QStringLiteral("1280x800");

static QString defaultCaptureMode()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("window");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("v4l2");
#else
    return QStringLiteral("unsupported");
#endif
}

// ─── Helpers ──────────────────────────────────────────────────────────

static QStringList commonScrcpyPaths()
{
#ifdef Q_OS_WIN
    const QString home = QDir::homePath();
    return {
        QStringLiteral("C:/scrcpy/scrcpy.exe"),
        QStringLiteral("C:/Program Files/scrcpy/scrcpy.exe"),
        QStringLiteral("C:/Program Files (x86)/scrcpy/scrcpy.exe"),
        home + QStringLiteral("/scrcpy/scrcpy.exe"),
        home + QStringLiteral("/Downloads/scrcpy-win64-v3.1/scrcpy.exe"),
        home + QStringLiteral("/Downloads/scrcpy/scrcpy.exe"),
    };
#else
    const QString home = QDir::homePath();
    return {
        QStringLiteral("/usr/bin/scrcpy"),
        QStringLiteral("/usr/local/bin/scrcpy"),
        QStringLiteral("/snap/bin/scrcpy"),
        home + QStringLiteral("/.local/bin/scrcpy"),
    };
#endif
}

static QStringList commonAdbPaths()
{
#ifdef Q_OS_WIN
    const QString home = QDir::homePath();
    return {
        QStringLiteral("C:/scrcpy/adb.exe"),
        QStringLiteral("C:/Program Files/Android/platform-tools/adb.exe"),
        QStringLiteral("C:/Program Files (x86)/Android/platform-tools/adb.exe"),
        home + QStringLiteral("/scrcpy/adb.exe"),
        home + QStringLiteral("/AppData/Local/Android/Sdk/platform-tools/adb.exe"),
    };
#else
    const QString home = QDir::homePath();
    return {
        QStringLiteral("/usr/bin/adb"),
        QStringLiteral("/usr/local/bin/adb"),
        home + QStringLiteral("/Android/Sdk/platform-tools/adb"),
    };
#endif
}

// ─── Constructor / Destructor ─────────────────────────────────────────

PhoneMirrorManager::PhoneMirrorManager(QObject *parent)
    : QObject(parent)
    , m_captureMode(defaultCaptureMode())
    , m_videoDevice(kDefaultVideoDevice)
    , m_displaySize(kDefaultDisplaySize)
{
    m_scrcpyPath = findScrcpy();
    m_adbPath    = findAdb();

    // Window poll timer — used after starting scrcpy to find its window
    m_windowPollTimer.setInterval(100);
    connect(&m_windowPollTimer, &QTimer::timeout, this, &PhoneMirrorManager::findScrcpyWindow);

    // v4l2 mode: if scrcpy never prints "v4l2 sink started" but is still
    // alive after 8 s, assume it is streaming.
    m_readyFallbackTimer.setSingleShot(true);
    m_readyFallbackTimer.setInterval(8000);
    connect(&m_readyFallbackTimer, &QTimer::timeout, this, &PhoneMirrorManager::markReady);
}

PhoneMirrorManager::~PhoneMirrorManager()
{
    cleanup();
}

// ─── Properties ───────────────────────────────────────────────────────

bool PhoneMirrorManager::isScrcpyInstalled() const
{
    return !getEffectiveScrcpyPath().isEmpty();
}

QString PhoneMirrorManager::scrcpyPath() const
{
    return getEffectiveScrcpyPath();
}

QString PhoneMirrorManager::adbPath() const
{
    return m_adbPath;
}

bool PhoneMirrorManager::isRunning() const
{
    if (m_client && m_client->isRunning())
        return true;
    return m_process != nullptr
        && m_process->state() != QProcess::NotRunning;
}

int PhoneMirrorManager::scrcpyWindowHandle() const
{
    return m_scrcpyHwnd;
}

QString PhoneMirrorManager::scrcpyVersion() const
{
    return m_scrcpyVersion;
}

QString PhoneMirrorManager::captureMode() const
{
    return m_captureMode;
}

QString PhoneMirrorManager::videoDevice() const
{
    return m_videoDevice;
}

QString PhoneMirrorManager::displaySize() const
{
    return m_displaySize;
}

int PhoneMirrorManager::displayId() const
{
    return m_displayId;
}

QString PhoneMirrorManager::activeDisplaySize() const
{
    return m_activeDisplaySize;
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

bool PhoneMirrorManager::environmentOk()
{
    // scrcpy installed, new enough and (on Linux) the video node exists: a
    // failure is about the phone, not the setup, so no install instructions.
    if (m_nativeMode)
        return nativeAvailable();
    if (getEffectiveScrcpyPath().isEmpty() || versionTooOld())
        return false;
    if (m_captureMode == QLatin1String("v4l2") && !videoDeviceExists())
        return false;
    return true;
}

bool PhoneMirrorManager::videoDeviceExists()
{
    return QFileInfo::exists(m_videoDevice);
}

void PhoneMirrorManager::killStaleServer()
{
    // scrcpy's device-side server can outlive the client (seen after a
    // crashed 1.21 run); a stale one conflicts with the next session.
    if (m_adbPath.isEmpty())
        return;
    QProcess::startDetached(m_adbPath, {QStringLiteral("shell"), QStringLiteral("pkill"),
                                        QStringLiteral("-f"), QStringLiteral("com.genymobile.scrcpy")});
}

// ─── Built-in client (native mode) ────────────────────────────────────

bool PhoneMirrorManager::nativeAvailable() const
{
    return ScrcpyClient::available() && !m_adbPath.isEmpty();
}

QString PhoneMirrorManager::serverVersion() const
{
    return QLatin1String(ScrcpyClient::kServerVersion);
}

void PhoneMirrorManager::setVideoSink(QObject *sink)
{
    m_videoSink = sink;
    if (m_client)
        m_client->setVideoSink(sink);
}

void PhoneMirrorManager::setNativeMode(bool enabled)
{
    if (enabled == m_nativeMode)
        return;
    if (enabled && !nativeAvailable())
        qCWarning(lcPhoneMirror) << "Built-in phone mirror requested but unavailable "
                                    "(decoder linked:" << ScrcpyClient::available() << ", adb:" << m_adbPath << ")";
    const bool wasRunning = isRunning();
    if (wasRunning)
        stopScrcpy();
    m_nativeMode = enabled;
    emit nativeModeChanged();
    if (wasRunning)
        QTimer::singleShot(300, this, &PhoneMirrorManager::startScrcpy);
}

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

void PhoneMirrorManager::pressHome()      { injectKey(ScrcpyClient::KeycodeHome); }
void PhoneMirrorManager::pressBack()      { injectKey(ScrcpyClient::KeycodeBack); }
void PhoneMirrorManager::pressAppSwitch() { injectKey(ScrcpyClient::KeycodeAppSwitch); }

void PhoneMirrorManager::startNative(const QString &serial)
{
    if (!ScrcpyClient::available()) {
        emit scrcpyError(QStringLiteral("This build has no built-in mirror client (libavcodec not linked)"));
        return;
    }
    const QString jar = ScrcpyClient::bundledServerJar();
    if (jar.isEmpty()) {
        emit scrcpyError(QStringLiteral("Bundled scrcpy server could not be extracted"));
        return;
    }
    m_isStarting = true;
    m_isStopping = false;
    m_ready = false;
    m_displayId = -1;
    m_activeDisplaySize = m_displaySize;

    if (m_client) {
        m_client->stop();
        m_client->deleteLater();
    }
    m_client = new ScrcpyClient(m_adbPath, jar, this);
    m_client->setVideoSink(m_videoSink);
    connect(m_client, &ScrcpyClient::connected, this, &PhoneMirrorManager::onNativeConnected, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::disconnected, this, &PhoneMirrorManager::onNativeDisconnected, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::frameSizeChanged, this, [this](int w, int h) {
        if (w != m_frameWidth || h != m_frameHeight) {
            m_frameWidth = w; m_frameHeight = h;
            emit frameSizeChanged(w, h);
        }
    }, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::frameReady, this, &PhoneMirrorManager::frameReady, Qt::QueuedConnection);

    QString displaySize = m_displaySize;
    if (!displaySize.isEmpty()) {
        const int sdk = getDeviceSdk();
        if (sdk > 0 && sdk < kNewDisplayMinSdk) {
            qCWarning(lcPhoneMirror) << "Device SDK" << sdk << "<" << kNewDisplayMinSdk << ": virtual display unavailable";
            displaySize.clear();
            m_activeDisplaySize.clear();
        }
    }
    // Audio forwarding is not implemented in the built-in client yet
    if (m_audioEnabled)
        qCInfo(lcPhoneMirror) << "Built-in client: audio forwarding not implemented yet, mirroring video only";
    qCInfo(lcPhoneMirror) << "Starting built-in scrcpy client" << serverVersion() << "for" << serial
                          << "(display" << (displaySize.isEmpty() ? QStringLiteral("phone screen") : displaySize) << ")";
    m_client->start(serial, displaySize, 60, 8000000, false, true);
    emit isRunningChanged();
}

void PhoneMirrorManager::onNativeConnected(int w, int h)
{
    m_ready = true;
    m_isStarting = false;
    m_scrcpyHwnd = 1;  // non-zero "handle" keeps the QML contract
    m_frameWidth = w; m_frameHeight = h;
    emit frameSizeChanged(w, h);
    if (!m_activeDisplaySize.isEmpty())
        m_activeDisplaySize = QStringLiteral("%1x%2").arg(w).arg(h);
    qCInfo(lcPhoneMirror) << "Built-in client connected:" << w << "x" << h;
    emit scrcpyStarted(m_scrcpyHwnd);
}

void PhoneMirrorManager::onNativeDisconnected(const QString &reason)
{
    if (m_isStopping)
        return;
    m_ready = false;
    m_isStarting = false;
    m_scrcpyHwnd = 0;
    emit isRunningChanged();
    if (reason.isEmpty())
        emit scrcpyStopped();
    else
        emit scrcpyError(reason);
}

int PhoneMirrorManager::getDeviceSdk()
{
    bool ok = false;
    const int sdk = runAdb({QStringLiteral("shell"), QStringLiteral("getprop"),
                            QStringLiteral("ro.build.version.sdk")}).trimmed().toInt(&ok);
    return ok ? sdk : 0;
}

int PhoneMirrorManager::findScrcpyDisplayId() const
{
    // Fallback when scrcpy's stderr didn't reveal the new display id: find
    // the logical display named "scrcpy" in dumpsys.
    const QString out = runAdb({QStringLiteral("shell"), QStringLiteral("dumpsys"),
                                QStringLiteral("display")}, 15000);
    if (out.isEmpty())
        return -1;
    static const QRegularExpression blockStart(QStringLiteral("\\n(?=\\s*Display \\d+:)"));
    static const QRegularExpression header(QStringLiteral("^\\s*Display (\\d+):"));
    const QStringList blocks = out.split(blockStart);
    for (const QString &block : blocks) {
        const auto m = header.match(block);
        if (m.hasMatch() && block.contains(QLatin1String("scrcpy")))
            return m.captured(1).toInt();
    }
    static const QRegularExpression older(QStringLiteral("\"scrcpy\"[\\s\\S]{0,2000}?mDisplayId=(\\d+)"));
    const auto m = older.match(out);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

// ─── Slots ────────────────────────────────────────────────────────────

void PhoneMirrorManager::setScrcpyPath(const QString &path)
{
    const QString oldEffective = getEffectiveScrcpyPath();
    m_customScrcpyPath = path.trimmed();

    if (m_customScrcpyPath.isEmpty())
        m_scrcpyPath = findScrcpy();

    const QString newEffective = getEffectiveScrcpyPath();
    if (oldEffective != newEffective) {
        emit scrcpyPathChanged();
        qCDebug(lcPhoneMirror) << "Effective scrcpy path:" << newEffective;
    }
}

void PhoneMirrorManager::setAudioEnabled(bool enabled)
{
    if (m_audioEnabled == enabled)
        return;

    qCDebug(lcPhoneMirror) << "Audio forwarding:" << enabled;
    m_audioEnabled = enabled;

    if (isRunning()) {
        qCDebug(lcPhoneMirror) << "Restarting scrcpy to apply audio setting change";
        stopScrcpy();
        QTimer::singleShot(300, this, &PhoneMirrorManager::startScrcpy);
    }
}

void PhoneMirrorManager::setVideoDevice(const QString &device)
{
    QString path = device.trimmed();
    if (path.isEmpty())
        path = kDefaultVideoDevice;
    if (path == m_videoDevice)
        return;

    qCDebug(lcPhoneMirror) << "Video device:" << path;
    m_videoDevice = path;
    emit videoDeviceChanged();

    if (isRunning() && m_captureMode == QLatin1String("v4l2")) {
        stopScrcpy();
        QTimer::singleShot(300, this, &PhoneMirrorManager::startScrcpy);
    }
}

void PhoneMirrorManager::setVolume(float /*volume*/)
{
    // Volume control placeholder — not implemented
}

QString PhoneMirrorManager::getDeviceState()
{
    if (m_adbPath.isEmpty())
        return QStringLiteral("no-adb");

    const QString output = runAdb({QStringLiteral("devices")});
    if (output.isEmpty())
        return QStringLiteral("none");

    QStringList states;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList parts = lines[i].split(QRegularExpression(QStringLiteral("\\s+")),
                                                 Qt::SkipEmptyParts);
        if (parts.size() >= 2)
            states << parts[1];
    }
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
        return QStringLiteral("adb not found. Install android-tools (adb) or scrcpy.");
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
    const QString output = runAdb({QStringLiteral("devices")});
    if (output.isEmpty())
        return false;

    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    // First line is "List of devices attached"
    for (int i = 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.trimmed().isEmpty())
            continue;
        if (line.contains(QLatin1String("device"))
            && !line.contains(QLatin1String("offline"))) {
            return true;
        }
    }
    return false;
}

QString PhoneMirrorManager::getDeviceSerial()
{
    const QString output = runAdb({QStringLiteral("devices")});
    if (output.isEmpty())
        return {};

    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.trimmed().isEmpty())
            continue;
        if (line.contains(QLatin1String("device"))
            && !line.contains(QLatin1String("offline"))) {
            const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
            if (!parts.isEmpty())
                return parts.first();
        }
    }
    return {};
}

QString PhoneMirrorManager::getDeviceName()
{
    const QString output = runAdb({
        QStringLiteral("shell"), QStringLiteral("getprop"),
        QStringLiteral("ro.product.model")
    });
    return output.trimmed();
}

QString PhoneMirrorManager::getDeviceResolution()
{
    const QString output = runAdb({
        QStringLiteral("shell"), QStringLiteral("wm"), QStringLiteral("size")
    });
    if (output.isEmpty())
        return {};

    // Output format: "Physical size: 1080x2400"
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.contains(QLatin1String("Physical size:"))) {
            const int colonIdx = line.indexOf(QLatin1Char(':'));
            if (colonIdx >= 0)
                return line.mid(colonIdx + 1).trimmed();
        }
    }
    return {};
}

QString PhoneMirrorManager::getInstallInstructions()
{
#ifdef Q_OS_WIN
    return QStringLiteral(
        "To install scrcpy:\n\n"
        "1. Download from: https://github.com/Genymobile/scrcpy/releases\n"
        "2. Extract to C:\\scrcpy or your preferred location\n"
        "3. Set the path in Settings > Phone Mirror\n"
        "4. Restart OCTAVE\n\n"
        "Note: scrcpy includes ADB. Enable USB debugging on your phone."
    );
#else
    return QStringLiteral(
        "To install scrcpy (version %1.%2 or newer):\n\n"
        "Distro packages are often too old (Ubuntu 22.04 ships 1.21).\n"
        "Build the current release: https://github.com/Genymobile/scrcpy/blob/master/doc/linux.md\n"
        "Arch Linux: sudo pacman -S scrcpy\n"
        "macOS: brew install scrcpy\n\n"
        "Linux also needs the v4l2loopback kernel module:\n"
        "  sudo modprobe v4l2loopback exclusive_caps=0 card_label=OCTAVE video_nr=10\n\n"
        "Make sure USB debugging is enabled and authorized on your phone."
    ).arg(kMinScrcpyMajor).arg(kMinScrcpyMinor);
#endif
}

void PhoneMirrorManager::startScrcpy()
{
    // Already running?
    if (isRunning()) {
        qCDebug(lcPhoneMirror) << "scrcpy already running, emitting existing handle";
        if (m_ready && m_scrcpyHwnd)
            emit scrcpyStarted(m_scrcpyHwnd);
        return;
    }

    // Already starting?
    if (m_isStarting) {
        qCDebug(lcPhoneMirror) << "scrcpy already starting, ignoring duplicate request";
        return;
    }

    if (m_nativeMode) {
        const QString state = getDeviceState();
        if (state != QLatin1String("device")) {
            emit scrcpyError(describeDeviceState(state));
            return;
        }
        startNative(getDeviceSerial());
        return;
    }

    if (m_captureMode == QLatin1String("unsupported")) {
        emit scrcpyError(QStringLiteral("Phone mirroring is not supported on this platform yet"));
        return;
    }

    const QString effectivePath = getEffectiveScrcpyPath();
    if (effectivePath.isEmpty()) {
        emit scrcpyError(QStringLiteral("scrcpy not installed"));
        return;
    }

    if (versionTooOld()) {
        emit scrcpyError(QStringLiteral("scrcpy %1 at %2 is too old (need >= %3.%4). "
                                        "Install a current release from https://github.com/Genymobile/scrcpy/releases")
                             .arg(m_scrcpyVersion, effectivePath)
                             .arg(kMinScrcpyMajor).arg(kMinScrcpyMinor));
        return;
    }

    const QString state = getDeviceState();
    if (state != QLatin1String("device")) {
        emit scrcpyError(describeDeviceState(state));
        return;
    }

    const QString serial = getDeviceSerial();
    runAdb({QStringLiteral("shell"), QStringLiteral("pkill"), QStringLiteral("-f"),
            QStringLiteral("com.genymobile.scrcpy")}, 5000);  // clear stale server first
    m_isStarting = true;
    m_isStopping = false;
    m_ready = false;
    m_stderrTail.clear();
    m_displayId = -1;
    m_activeDisplaySize.clear();

    // Build command arguments
    QStringList args;
    args << QStringLiteral("--video-codec=h264")
         << QStringLiteral("--video-bit-rate=8M")
         << QStringLiteral("--max-fps=60")
         << QStringLiteral("--stay-awake");

    // Virtual display: landscape, 64-aligned width, leaves the phone's own
    // screen alone. Needs Android 11+; older phones mirror their screen.
    if (!m_displaySize.isEmpty()) {
        const int sdk = getDeviceSdk();
        if (sdk >= kNewDisplayMinSdk || sdk == 0) {
            args << QStringLiteral("--new-display=") + m_displaySize;
            m_activeDisplaySize = m_displaySize;
        } else {
            qCWarning(lcPhoneMirror) << "Device SDK" << sdk << "<" << kNewDisplayMinSdk
                                     << ": --new-display unavailable, mirroring phone screen";
        }
    }

    if (m_captureMode == QLatin1String("v4l2")) {
        // Headless: frames go to the loopback node and QML shows them via
        // QtMultimedia; touch is injected through adb by ScrcpyCapture.
        args << QStringLiteral("--no-window")
             << QStringLiteral("--v4l2-sink=") + m_videoDevice;
    } else {
        args << QStringLiteral("--window-borderless");
    }

    if (!m_audioEnabled)
        args << QStringLiteral("--no-audio");

    if (!serial.isEmpty())
        args << QStringLiteral("-s") << serial;

    qCInfo(lcPhoneMirror) << "Starting scrcpy" << m_scrcpyVersion << "[" << m_captureMode << "]:"
                          << effectivePath << args;

    // Create the process
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(QFileInfo(effectivePath).absolutePath());
    m_process->setStandardOutputFile(QProcess::nullDevice());
#ifdef Q_OS_LINUX
    // If OCTAVE dies without cleanup (SIGKILL, crash) the kernel SIGTERMs
    // scrcpy, so it never outlives the app holding a virtual display open.
    m_process->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PhoneMirrorManager::onProcessFinished);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &PhoneMirrorManager::onProcessStderr);

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return;  // Crashed/other errors are reported by finished()
        m_isStarting = false;
        m_windowPollTimer.stop();
        m_readyFallbackTimer.stop();
        const QString msg = m_process ? m_process->errorString()
                                      : QStringLiteral("Unknown process error");
        qCWarning(lcPhoneMirror) << "Process error:" << msg;
        emit scrcpyError(msg);
        emit isRunningChanged();
    });

    m_process->start(effectivePath, args);
    emit isRunningChanged();

    if (m_captureMode == QLatin1String("v4l2")) {
        m_readyFallbackTimer.start();
    } else {
        // Start polling for the window handle
        m_windowPollCount = 0;
        m_windowPollTimer.start();
    }
}

void PhoneMirrorManager::onProcessStderr()
{
    if (!m_process)
        return;
    while (m_process->canReadLine()) {
        const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (line.isEmpty())
            continue;
        m_stderrTail << line;
        while (m_stderrTail.size() > 20)
            m_stderrTail.removeFirst();
        if (line.contains(QLatin1String("ERROR")) || line.contains(QLatin1String("WARN")))
            qCWarning(lcPhoneMirror) << "scrcpy:" << line;
        else
            qCDebug(lcPhoneMirror) << "scrcpy:" << line;
        // scrcpy 3.x: "[server] INFO: New display id: 5" (wording varies)
        static const QRegularExpression newDisplay(QStringLiteral("[Nn]ew display.*?\\bid\\D{0,3}(\\d+)"));
        const auto m = newDisplay.match(line);
        if (m.hasMatch()) {
            m_displayId = m.captured(1).toInt();
            qCInfo(lcPhoneMirror) << "scrcpy virtual display id" << m_displayId;
            emit displayIdChanged();
        }
        // scrcpy >= 2.0 prints this once the loopback sink is live
        if (!m_ready && line.contains(QLatin1String("v4l2 sink started")))
            markReady();
    }
}

void PhoneMirrorManager::markReady()
{
    if (m_ready || !m_process || m_process->state() == QProcess::NotRunning)
        return;
    m_readyFallbackTimer.stop();
    m_ready = true;
    m_isStarting = false;
    m_scrcpyHwnd = static_cast<int>(m_process->processId());
    if (!m_activeDisplaySize.isEmpty() && m_displayId < 0) {
        m_displayId = findScrcpyDisplayId();
        qCInfo(lcPhoneMirror) << "scrcpy virtual display id (dumpsys):" << m_displayId;
        emit displayIdChanged();
    }
    qCInfo(lcPhoneMirror) << "scrcpy ready (" << m_captureMode << ", pid" << m_scrcpyHwnd
                          << ", display" << m_displayId << ")";
    emit scrcpyStarted(m_scrcpyHwnd);
}

void PhoneMirrorManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_windowPollTimer.stop();
    m_readyFallbackTimer.stop();
    const bool wasReady = m_ready;
    m_scrcpyHwnd = 0;
    m_isStarting = false;
    m_ready = false;
    m_displayId = -1;
    m_activeDisplaySize.clear();

    if (m_isStopping) {
        // stopScrcpy() emits scrcpyStopped/isRunningChanged itself
        return;
    }

    if (m_process)
        onProcessStderr();  // flush whatever is left

    const QString tail = m_stderrTail.mid(qMax(0, m_stderrTail.size() - 5)).join(QLatin1String(" | "));
    qCWarning(lcPhoneMirror) << "scrcpy exited with code" << exitCode
                             << (wasReady ? "" : "before becoming ready") << ":" << tail;

    emit isRunningChanged();
    if (wasReady && exitCode == 0 && status == QProcess::NormalExit) {
        emit scrcpyStopped();
    } else {
        // A yanked cable is the common vehicle case: say so plainly so the
        // view can offer reconnection instead of setup instructions.
        for (const QString &l : m_stderrTail) {
            if (l.contains(QLatin1String("Device disconnected"))) {
                emit scrcpyError(QStringLiteral("Phone disconnected. Reconnect the USB cable."));
                return;
            }
        }
        // scrcpy's INFO lines are normal headless-mode chatter, not causes.
        QStringList errs;
        for (const QString &l : m_stderrTail)
            if (l.contains(QLatin1String("ERROR")))
                errs << l;
        if (errs.isEmpty())
            for (const QString &l : m_stderrTail)
                if (l.contains(QLatin1String("WARN")))
                    errs << l;
        if (errs.isEmpty()) {
            for (const QString &l : m_stderrTail)
                if (!l.contains(QLatin1String("INFO")))
                    errs << l;
            errs = errs.mid(qMax(0, errs.size() - 2));
        }
        QString msg = errs.join(QLatin1String(" | ")).left(300);
        if (msg.isEmpty())
            msg = QStringLiteral("exit code %1").arg(exitCode);
        emit scrcpyError(QStringLiteral("scrcpy failed: ") + msg);
    }
}

void PhoneMirrorManager::stopScrcpy()
{
    qCDebug(lcPhoneMirror) << "Stopping scrcpy";

    m_windowPollTimer.stop();
    m_readyFallbackTimer.stop();
    m_scrcpyHwnd = 0;
    m_isStarting = false;
    m_isStopping = true;
    m_ready = false;
    m_displayId = -1;
    m_activeDisplaySize.clear();

    if (m_client) {
        ScrcpyClient *client = m_client;
        m_client = nullptr;
        client->stop();
        client->deleteLater();
        killStaleServer();
    }

    if (m_process) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000)) {
            qCWarning(lcPhoneMirror) << "scrcpy did not terminate within 2s, killing";
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
        killStaleServer();
    }

    emit scrcpyStopped();
    emit isRunningChanged();
}

void PhoneMirrorManager::cleanup()
{
    stopScrcpy();
}

// ─── Private helpers ──────────────────────────────────────────────────

QString PhoneMirrorManager::getBundledToolsDir() const
{
    // In development the binary sits in build/, tools/scrcpy is at project root
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);

    // Try sibling (development layout: build/../tools/scrcpy)
    if (dir.cdUp() && dir.cd(QStringLiteral("tools")) && dir.cd(QStringLiteral("scrcpy")))
        return dir.absolutePath();

    // Try from app dir itself (PyInstaller / deployed layout)
    dir = QDir(appDir);
    if (dir.cd(QStringLiteral("tools")) && dir.cd(QStringLiteral("scrcpy")))
        return dir.absolutePath();

    return {};
}

QString PhoneMirrorManager::getEffectiveScrcpyPath() const
{
    if (!m_customScrcpyPath.isEmpty() && checkScrcpy(m_customScrcpyPath))
        return m_customScrcpyPath;
    return m_scrcpyPath;
}

QString PhoneMirrorManager::probeScrcpyVersion(const QString &path, bool *ok) const
{
    if (ok) *ok = false;
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(path, {QStringLiteral("--version")});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return {};

    if (ok) *ok = true;
    // First line: "scrcpy 3.3.1 <https://github.com/Genymobile/scrcpy>"
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression re(QStringLiteral("scrcpy\\s+v?(\\d+(?:\\.\\d+)*)"));
    const auto m = re.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

bool PhoneMirrorManager::checkScrcpy(const QString &path) const
{
    bool ok = false;
    const QString version = probeScrcpyVersion(path, &ok);
    if (!ok)
        return false;
    m_scrcpyVersion = version;
    return true;
}

bool PhoneMirrorManager::versionTooOld() const
{
    // Only reject when we positively know the version
    const QStringList parts = m_scrcpyVersion.split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.first().isEmpty())
        return false;
    bool okMaj = false, okMin = true;
    const int major = parts[0].toInt(&okMaj);
    const int minor = parts.size() > 1 ? parts[1].toInt(&okMin) : 0;
    if (!okMaj || !okMin)
        return false;
    return major < kMinScrcpyMajor || (major == kMinScrcpyMajor && minor < kMinScrcpyMinor);
}

QString PhoneMirrorManager::findScrcpy() const
{
    // Check bundled location first
    const QString bundledDir = getBundledToolsDir();
    if (!bundledDir.isEmpty()) {
#ifdef Q_OS_WIN
        const QString bundled = bundledDir + QStringLiteral("/scrcpy.exe");
#else
        const QString bundled = bundledDir + QStringLiteral("/scrcpy");
#endif
        if (QFileInfo::exists(bundled) && checkScrcpy(bundled)) {
            qCDebug(lcPhoneMirror) << "Using bundled scrcpy:" << bundled;
            return bundled;
        }
    }

    // Check PATH — validated like every other candidate so the version is
    // known (and a broken/too-old binary on PATH doesn't shadow a good one)
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("scrcpy"));
    if (!inPath.isEmpty() && checkScrcpy(inPath))
        return inPath;

    // Check common install locations
    for (const QString &path : commonScrcpyPaths()) {
        if (checkScrcpy(path))
            return path;
    }

    return {};
}

QString PhoneMirrorManager::findAdb() const
{
    // Bundled platform-tools first (scripts/fetch_platform_tools.py; CI ships
    // it next to the binary). Dev layout: <repo>/tools/platform-tools/<os>/,
    // deployed: <app dir>/platform-tools/ (or the app dir itself).
    {
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
        const QStringList candidates{
            appDir + QStringLiteral("/platform-tools/") + exe,
            appDir + QStringLiteral("/../tools/platform-tools/") + osName + QLatin1Char('/') + exe,
            appDir + QStringLiteral("/../Resources/platform-tools/") + exe,   // macOS bundle
        };
        for (const QString &c : candidates) {
            if (QFileInfo::exists(c)) {
                qCDebug(lcPhoneMirror) << "Using bundled platform-tools adb:" << c;
                return QFileInfo(c).absoluteFilePath();
            }
        }
    }

    // Then a bundled scrcpy package (includes adb)
    const QString bundledDir = getBundledToolsDir();
    if (!bundledDir.isEmpty()) {
#ifdef Q_OS_WIN
        const QString bundled = bundledDir + QStringLiteral("/adb.exe");
#else
        const QString bundled = bundledDir + QStringLiteral("/adb");
#endif
        if (QFileInfo::exists(bundled)) {
            qCDebug(lcPhoneMirror) << "Using bundled adb:" << bundled;
            return bundled;
        }
    }

    // Check PATH
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("adb"));
    if (!inPath.isEmpty())
        return inPath;

    // Check common install locations
    for (const QString &path : commonAdbPaths()) {
        if (QFileInfo::exists(path))
            return path;
    }

    return {};
}

QString PhoneMirrorManager::runAdb(const QStringList &args, int timeoutMs) const
{
    if (m_adbPath.isEmpty())
        return {};

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(m_adbPath, args);
    if (!proc.waitForFinished(timeoutMs))
        return {};
    if (proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

// ─── Window finding ───────────────────────────────────────────────────

void PhoneMirrorManager::findScrcpyWindow()
{
    ++m_windowPollCount;

    // Give up after 10 seconds (100 polls * 100 ms)
    if (m_windowPollCount > 100) {
        m_windowPollTimer.stop();
        m_isStarting = false;
        qCDebug(lcPhoneMirror) << "Could not find scrcpy window after 10s";
        emit scrcpyError(QStringLiteral("Could not find scrcpy window"));
        return;
    }

    // Process died before we found the window
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        m_windowPollTimer.stop();
        m_isStarting = false;
        // finished() -> onProcessFinished() reports the failure with scrcpy's
        // own stderr; nothing more to do here.
        qCDebug(lcPhoneMirror) << "scrcpy exited before window found";
        return;
    }

#ifdef Q_OS_WIN
    int hwnd = findWindowByPid(m_process->processId());
    if (hwnd) {
        m_windowPollTimer.stop();
        m_scrcpyHwnd = hwnd;
        m_isStarting = false;
        m_ready = true;
        qCDebug(lcPhoneMirror) << "Found scrcpy window:" << hwnd;
        emit scrcpyStarted(hwnd);
    }
#else
    // Window mode is Windows-only; other platforms use v4l2 mode and never
    // start this timer. Treat reaching here as "ready" so nothing hangs.
    m_windowPollTimer.stop();
    markReady();
#endif
}

#ifdef Q_OS_WIN
int PhoneMirrorManager::findWindowByPid(qint64 targetPid) const
{
    struct EnumData {
        DWORD pid;
        HWND  result;
    } data;
    data.pid = static_cast<DWORD>(targetPid);
    data.result = nullptr;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);
        if (windowPid == d->pid && IsWindowVisible(hwnd)) {
            d->result = hwnd;
            return FALSE; // stop enumeration
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return static_cast<int>(reinterpret_cast<intptr_t>(data.result));
}
#endif

#endif // Q_OS_MOBILE
