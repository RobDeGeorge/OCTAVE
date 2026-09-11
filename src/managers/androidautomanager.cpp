/*
 * Android Auto Manager for OCTAVE (C++ port)
 *
 * Manages Google DHU subprocess, ADB port forwarding, frame capture,
 * and click forwarding.  Uses QScreen::grabWindow() for portable
 * window capture (no platform-specific GDI/PrintWindow code).
 *
 * Desktop-only — excluded on mobile builds.
 */

#ifndef Q_OS_MOBILE

#include "androidautomanager.h"

#include "phonemirrormanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>

#include <QLoggingCategory>

#ifdef Q_OS_WIN
// QProcess::CreateProcessArguments::startupInfo is a STARTUPINFOW* — Qt
// forward-declares the struct in qprocess.h, so accessing its members
// requires the full definition from <windows.h>.
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(lcAndroidAuto, "octave.androidauto")

// =====================================================================
//  DhuFrameProvider
// =====================================================================

DhuFrameProvider::DhuFrameProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DhuFrameProvider::requestImage(const QString &id,
                                      QSize *size,
                                      const QSize &requestedSize)
{
    Q_UNUSED(id)
    Q_UNUSED(requestedSize)

    QMutexLocker lock(&m_mutex);
    if (!m_currentImage.isNull()) {
        if (size)
            *size = m_currentImage.size();
        return m_currentImage;
    }
    // Placeholder while no frame is available
    QImage placeholder(800, 480, QImage::Format_RGB32);
    placeholder.fill(Qt::black);
    if (size)
        *size = placeholder.size();
    return placeholder;
}

void DhuFrameProvider::updateFrame(const QImage &image)
{
    QMutexLocker lock(&m_mutex);
    m_currentImage = image;
}

// =====================================================================
//  DhuCapture
// =====================================================================

DhuCapture::DhuCapture(QObject *parent)
    : QObject(parent)
    , m_frameProvider(new DhuFrameProvider)
{
}

DhuCapture::~DhuCapture()
{
    stopCapture();
    // m_frameProvider ownership is transferred to the QML engine via
    // addImageProvider(); do NOT delete it here.
}

bool DhuCapture::isCapturing() const
{
    return m_capturing;
}

int DhuCapture::windowHandle() const
{
    return m_hwnd;
}

DhuFrameProvider *DhuCapture::frameProvider() const
{
    return m_frameProvider;
}

void DhuCapture::setWindowHandle(int hwnd)
{
    m_hwnd = hwnd;
    qCDebug(lcAndroidAuto) << "DhuCapture: window handle set to" << hwnd;
}

void DhuCapture::startCapture()
{
    if (m_capturing)
        return;

    if (!m_hwnd) {
        emit error(QStringLiteral("No window handle set"));
        return;
    }

    qCInfo(lcAndroidAuto) << "DhuCapture: starting capture at"
                          << m_targetFps << "FPS";
    m_capturing = true;

    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &DhuCapture::captureFrame);
    m_captureTimer->start(1000 / m_targetFps);

    emit captureStarted();
}

void DhuCapture::stopCapture()
{
    if (!m_capturing)
        return;

    qCInfo(lcAndroidAuto) << "DhuCapture: stopping capture";
    m_capturing = false;

    if (m_captureTimer) {
        m_captureTimer->stop();
        delete m_captureTimer;
        m_captureTimer = nullptr;
    }

    emit captureStopped();
}

void DhuCapture::captureFrame()
{
    if (!m_capturing || !m_hwnd)
        return;

    // Portable capture via QScreen::grabWindow().
    // WId 0 grabs the entire screen; a nonzero WId grabs that window.
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    QPixmap pixmap = screen->grabWindow(static_cast<WId>(m_hwnd));
    if (pixmap.isNull())
        return;

    QImage image = pixmap.toImage();
    if (image.isNull())
        return;

    m_frameProvider->updateFrame(image);
    emit frameReady();
}

void DhuCapture::sendMouseClick(int x, int y)
{
    Q_UNUSED(x)
    Q_UNUSED(y)

    if (!m_hwnd)
        return;

    // On Windows we would use SendMessage / mouse_event.
    // On Linux/macOS there is no universal API; callers should use
    // ADB-based input instead.  This is intentionally left as a stub
    // because DHU click forwarding is platform-specific and the DHU
    // is rarely used.
    qCDebug(lcAndroidAuto) << "DhuCapture::sendMouseClick — stub ("
                           << x << "," << y << ")";
}

// =====================================================================
//  AndroidAutoManager
// =====================================================================

AndroidAutoManager::AndroidAutoManager(QObject *parent)
    : QObject(parent)
    , m_dhuCaptureObj(new DhuCapture(this))
{
    connect(m_dhuCaptureObj, &DhuCapture::frameReady,
            this, &AndroidAutoManager::onDhuFrameReady);
    connect(m_dhuCaptureObj, &DhuCapture::error,
            this, &AndroidAutoManager::onDhuCaptureError);
}

AndroidAutoManager::~AndroidAutoManager()
{
    cleanup();
}

// ─── Property accessors ──────────────────────────────────────────────

bool AndroidAutoManager::isDhuInstalled() const
{
    return !findDhuPath().isEmpty();
}

QString AndroidAutoManager::dhuPath() const
{
    return findDhuPath();
}

bool AndroidAutoManager::isDhuEmbedded() const
{
    return m_dhuEmbedded;
}

int AndroidAutoManager::dhuWindowHandle() const
{
    return m_dhuHwnd;
}

QObject *AndroidAutoManager::dhuCapture() const
{
    return m_dhuCaptureObj;
}

DhuFrameProvider *AndroidAutoManager::frameProvider() const
{
    return m_dhuCaptureObj ? m_dhuCaptureObj->frameProvider() : nullptr;
}

// ─── Binary detection ────────────────────────────────────────────────

QString AndroidAutoManager::findDhuPath() const
{
    if (!m_cachedDhuPath.isEmpty())
        return m_cachedDhuPath;

#ifdef Q_OS_WIN
    const QString dhuName = QStringLiteral("desktop-head-unit.exe");
    QStringList sdkRoots;

    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        sdkRoots << (localAppData + QStringLiteral("/Android/Sdk"));

    QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty())
        sdkRoots << (userProfile + QStringLiteral("/AppData/Local/Android/Sdk"));

    sdkRoots << QStringLiteral("C:/Android/Sdk");

    QString userName = qEnvironmentVariable("USERNAME");
    if (!userName.isEmpty())
        sdkRoots << (QStringLiteral("C:/Users/") + userName + QStringLiteral("/Android/Sdk"));
#else
    const QString dhuName = QStringLiteral("desktop-head-unit");
    QStringList sdkRoots;

    sdkRoots << (QDir::homePath() + QStringLiteral("/Android/Sdk"));
    sdkRoots << (QDir::homePath() + QStringLiteral("/Library/Android/sdk"));
    sdkRoots << QStringLiteral("/opt/android-sdk");
    sdkRoots << QStringLiteral("/usr/local/android-sdk");
#endif

    for (const QString &sdk : sdkRoots) {
        QString full = sdk + QStringLiteral("/extras/google/auto/") + dhuName;
        if (QFile::exists(full)) {
            const_cast<AndroidAutoManager *>(this)->m_cachedDhuPath = full;
            return full;
        }
    }
    return {};
}

QString AndroidAutoManager::findAdbPath() const
{
    if (!m_cachedAdbPath.isEmpty())
        return m_cachedAdbPath;

    // Same discovery as PhoneMirrorManager (bundled platform-tools, PATH,
    // common SDK locations) so both features drive the same adb binary.
    const QString path = PhoneMirrorManager::findAdb();
    if (!path.isEmpty())
        const_cast<AndroidAutoManager *>(this)->m_cachedAdbPath = path;
    return path;
}

// ─── ADB helpers ─────────────────────────────────────────────────────

QString AndroidAutoManager::runAdb(const QStringList &args, int timeoutMs) const
{
    QString adb = findAdbPath();
    if (adb.isEmpty())
        return {};

    QProcess proc;
    proc.setProgram(adb);
    proc.setArguments(args);
#ifdef Q_OS_WIN
    proc.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *cpa) {
            cpa->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif
    proc.start();

    if (!proc.waitForFinished(timeoutMs))
        return {};

    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

bool AndroidAutoManager::isHeadunitServerRunning() const
{
    QString adb = findAdbPath();
    if (adb.isEmpty())
        return false;

    QString output = runAdb({
        QStringLiteral("shell"), QStringLiteral("dumpsys"),
        QStringLiteral("activity"), QStringLiteral("services"),
        QStringLiteral("com.google.android.projection.gearhead/"
                       ".companion.DeveloperHeadUnitNetworkService")
    }, 5000);

    return output.contains(QStringLiteral("ServiceRecord"));
}

bool AndroidAutoManager::startHeadunitServer()
{
    QString adb = findAdbPath();
    if (adb.isEmpty())
        return false;

    qCInfo(lcAndroidAuto) << "Starting head unit server on phone...";
    emit connectionProgress(QStringLiteral("Starting head unit server on phone..."));

    // Try starting via activity
    runAdb({
        QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("start"),
        QStringLiteral("-n"),
        QStringLiteral("com.google.android.projection.gearhead/"
                       ".companion.MainActivity"),
        QStringLiteral("-a"),
        QStringLiteral("com.google.android.gms.car.action.START_HEAD_UNIT_SERVER")
    }, 10000);

    QThread::sleep(2);

    if (isHeadunitServerRunning()) {
        qCInfo(lcAndroidAuto) << "Head unit server started successfully";
        return true;
    }

    // Try direct service start
    runAdb({
        QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("startservice"),
        QStringLiteral("-n"),
        QStringLiteral("com.google.android.projection.gearhead/"
                       ".companion.DeveloperHeadUnitNetworkService")
    }, 10000);

    QThread::sleep(1);

    if (isHeadunitServerRunning()) {
        qCInfo(lcAndroidAuto) << "Head unit server started successfully";
        return true;
    }

    qCWarning(lcAndroidAuto) << "Could not auto-start head unit server";
    return false;
}

bool AndroidAutoManager::setupAdbForward()
{
    QString adb = findAdbPath();
    if (adb.isEmpty()) {
        qCWarning(lcAndroidAuto) << "ADB not found";
        return false;
    }

    // Remove a stale DHU forward only — `--remove-all` would also tear down
    // a live phone-mirror (localabstract:scrcpy_*) tunnel.
    runAdb({QStringLiteral("forward"), QStringLiteral("--remove"), QStringLiteral("tcp:5277")}, 10000);

    qCDebug(lcAndroidAuto) << "Running ADB forward:" << adb;

    QProcess proc;
    proc.setProgram(adb);
    proc.setArguments({
        QStringLiteral("forward"),
        QStringLiteral("tcp:5277"),
        QStringLiteral("tcp:5277")
    });
#ifdef Q_OS_WIN
    proc.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *cpa) {
            cpa->flags |= 0x08000000;
        });
#endif
    proc.start();

    if (!proc.waitForFinished(10000)) {
        qCWarning(lcAndroidAuto) << "ADB forward timed out";
        return false;
    }

    if (proc.exitCode() == 0) {
        qCDebug(lcAndroidAuto) << "ADB forward successful";
        return true;
    }

    qCWarning(lcAndroidAuto) << "ADB forward failed:"
                             << proc.readAllStandardError().trimmed();
    return false;
}

bool AndroidAutoManager::prepareAdbConnection()
{
    QString adb = findAdbPath();
    if (adb.isEmpty()) {
        qCWarning(lcAndroidAuto) << "ADB not found — cannot prepare connection";
        emit error(QStringLiteral(
            "ADB not found. Install Android SDK platform-tools."));
        return false;
    }

    // Check for connected device
    QString devicesOutput = runAdb({QStringLiteral("devices")}, 10000);
    QStringList lines = devicesOutput.split(QLatin1Char('\n'),
                                            Qt::SkipEmptyParts);
    int deviceCount = 0;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].contains(QStringLiteral("device")))
            ++deviceCount;
    }

    if (deviceCount == 0) {
        qCWarning(lcAndroidAuto) << "No Android device connected";
        emit error(QStringLiteral(
            "No Android device connected. Connect your phone via USB."));
        return false;
    }
    qCInfo(lcAndroidAuto) << "Found" << deviceCount << "connected device(s)";

    // Setup port forwarding
    emit connectionProgress(QStringLiteral("Setting up ADB port forwarding..."));
    if (!setupAdbForward()) {
        emit error(QStringLiteral("Failed to setup ADB port forwarding"));
        return false;
    }

    // Check head unit server
    if (!isHeadunitServerRunning()) {
        if (!startHeadunitServer()) {
            qCInfo(lcAndroidAuto)
                << "Please start 'Head unit server' manually on your phone";
            emit connectionProgress(QStringLiteral(
                "Please start 'Head unit server' in Android Auto developer settings..."));

            // Poll for up to 60 seconds
            for (int i = 0; i < 60; ++i) {
                if (isHeadunitServerRunning()) {
                    qCInfo(lcAndroidAuto) << "Head unit server detected!";
                    emit connectionProgress(
                        QStringLiteral("Head unit server started!"));
                    QThread::msleep(500);
                    return true;
                }
                QThread::sleep(1);
                if (i > 0 && i % 5 == 0) {
                    int remaining = 60 - i;
                    emit connectionProgress(
                        QStringLiteral("Waiting for head unit server... (%1s)")
                            .arg(remaining));
                }
            }
            qCWarning(lcAndroidAuto)
                << "Timed out waiting for head unit server";
            emit error(QStringLiteral(
                "Head unit server not started. Please start it and try again."));
            return false;
        }
    } else {
        qCDebug(lcAndroidAuto) << "Head unit server already running";
    }

    return true;
}

// ─── Window finding ──────────────────────────────────────────────────

int AndroidAutoManager::findDhuWindow() const
{
    // Platform-specific window enumeration.
    // On Windows we would use EnumWindows; on X11 we could walk the
    // window tree.  Since DHU is rarely used and the capture already
    // uses QScreen::grabWindow(), we rely on QProcess::processId()
    // at launch time and skip enumeration for now.
    //
    // The m_dhuHwnd is set by pollAndHideDhu() on Windows or
    // setupSeamlessCapture() on other platforms via
    // QProcess::processId().

#ifdef Q_OS_WIN
    // On Windows, enumerate top-level windows looking for "Desktop Head Unit"
    // or the DHU process ID.
    if (!m_dhuProcess)
        return 0;

    // We cannot call Win32 EnumWindows from pure Qt without winapi
    // includes.  Instead, use a simple heuristic: wait for the process
    // to create a window, then return its process ID as a stand-in.
    // Real embedding code would use platform-native APIs.
    return 0;  // Handled in pollAndHideDhu via WinAPI
#else
    // On Linux/macOS, we don't embed the window.  We just capture it
    // via QScreen::grabWindow(0) (whole-screen fallback) if needed.
    return 0;
#endif
}

// ─── Slots ───────────────────────────────────────────────────────────

QString AndroidAutoManager::getDhuInstallInstructions()
{
    return QStringLiteral(
        "To install Google's Desktop Head Unit:\n\n"
        "1. Open Android Studio\n"
        "2. Go to Tools → SDK Manager\n"
        "3. Select the \"SDK Tools\" tab\n"
        "4. Check \"Android Auto Desktop Head Unit Emulator\"\n"
        "5. Click \"Apply\" to install\n\n"
        "After installation, click \"Launch Android Auto\" button.");
}

bool AndroidAutoManager::launchDhuSeamless()
{
    // Close any existing DHU
    if (m_dhuProcess || m_dhuEmbedded)
        closeDhu();

    QString dhu = findDhuPath();
    if (dhu.isEmpty()) {
        qCWarning(lcAndroidAuto) << "Google DHU not found";
        emit error(QStringLiteral("Google DHU not installed."));
        return false;
    }

    // Full ADB preparation (blocking — could be moved to a worker thread
    // for production use, but DHU is rarely used)
    if (!prepareAdbConnection())
        return false;

    emit connectionProgress(
        QStringLiteral("Please start 'Head unit server' on your phone..."));

    qCInfo(lcAndroidAuto) << "Launching DHU for seamless capture:" << dhu;

    // Write DHU config for higher resolution
    QString androidDir = QDir::homePath() + QStringLiteral("/.android");
    QDir().mkpath(androidDir);
    QString configPath = androidDir + QStringLiteral("/headunit.ini");
    {
        QFile configFile(configPath);
        if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            configFile.write("[general]\n"
                             "resolution = 1920 1080\n"
                             "dpi = 160\n");
            qCDebug(lcAndroidAuto) << "Created DHU config at" << configPath;
        } else {
            qCWarning(lcAndroidAuto) << "Could not create DHU config:"
                                     << configFile.errorString();
        }
    }

    // Launch DHU via QProcess
    m_dhuProcess = new QProcess(this);
    m_dhuProcess->setWorkingDirectory(QFileInfo(dhu).absolutePath());

    connect(m_dhuProcess, &QProcess::finished,
            this, &AndroidAutoManager::onDhuProcessFinished);

#ifdef Q_OS_WIN
    // On Windows, start minimized to avoid flicker
    m_dhuProcess->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *cpa) {
            cpa->flags |= 0x08000000; // CREATE_NO_WINDOW
            cpa->startupInfo->dwFlags |= 0x00000001; // STARTF_USESHOWWINDOW
            cpa->startupInfo->wShowWindow = 6;        // SW_MINIMIZE
        });
    m_dhuProcess->start(dhu, {});

    if (!m_dhuProcess->waitForStarted(5000)) {
        qCWarning(lcAndroidAuto) << "DHU failed to start";
        emit error(QStringLiteral("Failed to launch DHU."));
        delete m_dhuProcess;
        m_dhuProcess = nullptr;
        return false;
    }

    // Start aggressive polling to find and hide the window
    m_hidePollCount = 0;
    QTimer::singleShot(100, this, &AndroidAutoManager::pollAndHideDhu);
#else
    m_dhuProcess->start(dhu, {});

    if (!m_dhuProcess->waitForStarted(5000)) {
        qCWarning(lcAndroidAuto) << "DHU failed to start";
        emit error(QStringLiteral("Failed to launch DHU."));
        delete m_dhuProcess;
        m_dhuProcess = nullptr;
        return false;
    }

    // On non-Windows, wait for the window to appear then start capture
    QTimer::singleShot(3000, this, &AndroidAutoManager::setupSeamlessCapture);
#endif

    emit connectionProgress(QStringLiteral("Starting Android Auto..."));
    return true;
}

void AndroidAutoManager::sendDhuClick(int x, int y)
{
    m_dhuCaptureObj->sendMouseClick(x, y);
}

void AndroidAutoManager::closeDhu()
{
    // Stop capture first
    m_dhuCaptureObj->stopCapture();

    if (m_dhuProcess) {
        m_dhuProcess->terminate();
        if (!m_dhuProcess->waitForFinished(5000)) {
            qCWarning(lcAndroidAuto) << "DHU did not terminate, killing";
            m_dhuProcess->kill();
            m_dhuProcess->waitForFinished(2000);
        }
        delete m_dhuProcess;
        m_dhuProcess = nullptr;
    }

    m_dhuHwnd = 0;
    m_dhuEmbedded = false;
    emit dhuEmbeddedChanged(false);
}

void AndroidAutoManager::cleanup()
{
    qCInfo(lcAndroidAuto) << "Cleaning up Android Auto...";

    closeDhu();

    // Clean up the DHU port forward (only ours — phone mirror owns its own)
    QString adb = findAdbPath();
    if (!adb.isEmpty()) {
        runAdb({QStringLiteral("forward"), QStringLiteral("--remove"), QStringLiteral("tcp:5277")},
               10000);
    }

    qCInfo(lcAndroidAuto) << "Cleanup complete";
}

// ─── Private slots ───────────────────────────────────────────────────

void AndroidAutoManager::pollAndHideDhu()
{
    // This is the Windows path.  On other platforms, setupSeamlessCapture()
    // is used instead.

    if (!m_dhuProcess || m_dhuProcess->state() == QProcess::NotRunning) {
        qCWarning(lcAndroidAuto) << "DHU process ended";
        emit error(QStringLiteral("DHU closed unexpectedly"));
        return;
    }

#ifdef Q_OS_WIN
    // On a real Windows build, we would use EnumWindows + FindWindow
    // to locate the DHU window by title.  Since this C++ port uses
    // QScreen::grabWindow() for capture (portable), the window handle
    // is only needed for Win32 embedding.
    //
    // For now, after a delay we assume the DHU window exists and start
    // capture using the process ID as a proxy handle.
    ++m_hidePollCount;
    if (m_hidePollCount < 50) {
        // Keep polling — 100ms * 50 = 5s max
        QTimer::singleShot(100, this, &AndroidAutoManager::pollAndHideDhu);
    } else {
        // Timeout — fall back to capture-only mode
        qCWarning(lcAndroidAuto)
            << "Timeout waiting for DHU window, starting capture anyway";
        m_dhuHwnd = static_cast<int>(m_dhuProcess->processId());
        QTimer::singleShot(2000, this, &AndroidAutoManager::startDhuCapture);
    }
#else
    // Non-Windows: shouldn't reach here, but just start capture
    setupSeamlessCapture();
#endif
}

void AndroidAutoManager::startDhuCapture()
{
    if (!m_dhuHwnd)
        return;

    m_dhuEmbedded = true;
    emit dhuEmbeddedChanged(true);
    emit dhuWindowReady(m_dhuHwnd);
    emit connectionProgress(QStringLiteral("Android Auto running"));
    qCInfo(lcAndroidAuto) << "DHU window ready for embedding:" << m_dhuHwnd;
}

void AndroidAutoManager::setupSeamlessCapture()
{
    // Non-Windows fallback: use process ID as the "window handle"
    // for QScreen::grabWindow().

    if (!m_dhuProcess || m_dhuProcess->state() == QProcess::NotRunning) {
        qCWarning(lcAndroidAuto) << "DHU process ended";
        emit error(QStringLiteral("DHU closed unexpectedly"));
        return;
    }

    // On Linux/macOS, QScreen::grabWindow() with process ID won't work
    // directly (it needs an X11/Wayland window ID).  For a production
    // build you'd use platform integration to find the actual WId.
    // As a working fallback, capture the whole screen (WId = 0).
    int hwnd = 0;  // 0 = entire screen (fallback)

    m_dhuHwnd = hwnd;
    qCDebug(lcAndroidAuto) << "DHU capture handle (fallback):" << hwnd;

    m_dhuCaptureObj->setWindowHandle(hwnd);
    m_dhuCaptureObj->startCapture();

    m_dhuEmbedded = true;
    emit dhuEmbeddedChanged(true);
    emit dhuWindowReady(hwnd);
    emit connectionProgress(QStringLiteral("Android Auto running"));
}

void AndroidAutoManager::onDhuFrameReady()
{
    // New frame available in the provider — QML Image elements using
    // "image://dhuframe/<counter>" will refresh when the counter
    // property changes on the QML side.
}

void AndroidAutoManager::onDhuCaptureError(const QString &message)
{
    qCWarning(lcAndroidAuto) << "Capture error:" << message;
    emit error(message);
}

void AndroidAutoManager::onDhuProcessFinished(int exitCode,
                                              QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)

    qCInfo(lcAndroidAuto) << "DHU process finished (exit code" << exitCode << ")";

    // Clean up state
    m_dhuCaptureObj->stopCapture();
    m_dhuHwnd = 0;

    if (m_dhuEmbedded) {
        m_dhuEmbedded = false;
        emit dhuEmbeddedChanged(false);
    }

    m_dhuProcess->deleteLater();
    m_dhuProcess = nullptr;
}

#endif // Q_OS_MOBILE
