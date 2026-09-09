#ifndef Q_OS_MOBILE

#include "scrcpycapture.h"
#include "../managers/phonemirrormanager.h"

#include <QDateTime>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <signal.h>
#endif
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>

#include <cmath>

Q_LOGGING_CATEGORY(lcScrcpyCapture, "octave.scrcpy.capture")

// =====================================================================
// ScrcpyFrameProvider
// =====================================================================

ScrcpyFrameProvider::ScrcpyFrameProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage ScrcpyFrameProvider::requestImage(const QString &id, QSize *size,
                                         const QSize &requestedSize)
{
    Q_UNUSED(id)
    Q_UNUSED(requestedSize)

    QMutexLocker lock(&m_mutex);
    if (!m_currentFrame.isNull()) {
        if (size)
            *size = m_currentFrame.size();
        return m_currentFrame;
    }
    // Placeholder when no frame is available yet
    QImage placeholder(720, 1280, QImage::Format_RGB32);
    placeholder.fill(Qt::black);
    if (size)
        *size = placeholder.size();
    return placeholder;
}

void ScrcpyFrameProvider::updateFrame(const QImage &frame)
{
    QMutexLocker lock(&m_mutex);
    m_currentFrame = frame;
}

bool ScrcpyFrameProvider::hasFrame() const
{
    QMutexLocker lock(&m_mutex);
    return !m_currentFrame.isNull();
}

// =====================================================================
// ScrcpyCapture
// =====================================================================

ScrcpyCapture::ScrcpyCapture(QObject *parent)
    : QObject(parent)
    , m_frameProvider(new ScrcpyFrameProvider)
{
}

ScrcpyCapture::~ScrcpyCapture()
{
    stopCapture();
    // Note: m_frameProvider ownership transfers to the QML engine when
    // registered via addImageProvider — do NOT delete it here.
}

// ─── Window handle ────────────────────────────────────────────────────

void ScrcpyCapture::setWindowHandle(int hwnd)
{
    m_hwnd = hwnd;
    qCDebug(lcScrcpyCapture) << "Window handle set to:" << hwnd;
}

// ─── Start / Stop ─────────────────────────────────────────────────────

void ScrcpyCapture::setFrameSize(int width, int height)
{
    if (width == m_lastWidth && height == m_lastHeight)
        return;
    m_lastWidth = width;
    m_lastHeight = height;
    qCDebug(lcScrcpyCapture) << "Frame size (external):" << width << "x" << height;
    emit frameSizeChanged(width, height);
}

void ScrcpyCapture::startCapture()
{
    if (m_capturing)
        return;

    if (!m_hwnd) {
        emit error(QStringLiteral("No window handle set"));
        return;
    }

    if (m_manager && m_manager->captureMode() == QLatin1String("v4l2")) {
        // Read the loopback node ourselves through ffmpeg and push frames into
        // the image provider — the same QML path the Windows grab uses.
        // QtMultimedia's camera enumeration is deliberately avoided: it
        // snapshots the device list at first use per process and never
        // refreshes, so a node that only appears once scrcpy streams is
        // never found.
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
            emit error(QStringLiteral("ffmpeg not found — install it (apt install ffmpeg) for Linux phone mirroring"));
            return;
        }
        m_capturing = true;
        m_frameCount = 0;
        fetchDeviceInfo();
        m_v4l2Deadline = QDateTime::currentMSecsSinceEpoch() + 20000;
        m_v4l2GotFrame = false;
        startV4l2Reader();
        emit captureStarted();
        return;
    }

#ifndef Q_OS_WIN
    emit error(QStringLiteral("Screen capture only supported on Windows"));
    return;
#endif

    qCDebug(lcScrcpyCapture) << "Starting capture of window" << m_hwnd
                              << "at" << m_targetFps << "FPS";

    m_capturing = true;
    m_frameCount = 0;

    // Fetch device info for ADB-based touch input
    fetchDeviceInfo();

    // Create capture timer
    if (!m_captureTimer) {
        m_captureTimer = new QTimer(this);
        connect(m_captureTimer, &QTimer::timeout, this, &ScrcpyCapture::captureFrame);
    }
    m_captureTimer->start(1000 / m_targetFps);

    emit captureStarted();
}

void ScrcpyCapture::stopCapture()
{
    if (!m_capturing)
        return;

    qCDebug(lcScrcpyCapture) << "Stopping capture (captured" << m_frameCount << "frames)";
    m_capturing = false;

    stopV4l2Reader();

    if (m_captureTimer) {
        m_captureTimer->stop();
    }

    emit captureStopped();
}

// ─── Frame capture ────────────────────────────────────────────────────

void ScrcpyCapture::captureFrame()
{
    if (!m_capturing || !m_hwnd)
        return;

    // Portable fallback: use QScreen::grabWindow()
    // This works on all desktop platforms without platform-specific APIs
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }

    // grabWindow with WId — captures the specific window
    QPixmap pixmap = screen->grabWindow(static_cast<WId>(m_hwnd));
    if (pixmap.isNull())
        return;

    QImage image = pixmap.toImage();
    if (image.isNull())
        return;

    const int w = image.width();
    const int h = image.height();

    // Track size changes
    if (w != m_lastWidth || h != m_lastHeight) {
        m_lastWidth = w;
        m_lastHeight = h;
        emit frameSizeChanged(w, h);
        qCDebug(lcScrcpyCapture) << "Frame size:" << w << "x" << h;
    }

    // Ensure consistent format for the provider
    if (image.format() != QImage::Format_ARGB32
        && image.format() != QImage::Format_ARGB32_Premultiplied
        && image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }

    m_frameProvider->updateFrame(image);
    ++m_frameCount;
    emit frameReady();
}

// ─── v4l2 reader (Linux) ─────────────────────────────────────────────

QSize ScrcpyCapture::probeV4l2Size(const QString &device) const
{
    // Virtual display: geometry is known and fixed
    if (m_fixedDisplay && m_deviceWidth > 0)
        return {m_deviceWidth, m_deviceHeight};

    const QString v4l2ctl = QStandardPaths::findExecutable(QStringLiteral("v4l2-ctl"));
    if (!v4l2ctl.isEmpty()) {
        QProcess p;
        p.start(v4l2ctl, {QStringLiteral("-d"), device, QStringLiteral("--get-fmt-video")});
        if (p.waitForFinished(5000)) {
            static const QRegularExpression re(QStringLiteral("Width/Height\\s*:\\s*(\\d+)/(\\d+)"));
            const auto m = re.match(QString::fromUtf8(p.readAllStandardOutput()));
            if (m.hasMatch())
                return {m.captured(1).toInt(), m.captured(2).toInt()};
        }
    }
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!ffprobe.isEmpty()) {
        QProcess p;
        p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                          QStringLiteral("-show_entries"), QStringLiteral("stream=width,height"),
                          QStringLiteral("-of"), QStringLiteral("csv=p=0"), device});
        if (p.waitForFinished(10000)) {
            static const QRegularExpression re(QStringLiteral("(\\d+),(\\d+)"));
            const auto m = re.match(QString::fromUtf8(p.readAllStandardOutput()));
            if (m.hasMatch())
                return {m.captured(1).toInt(), m.captured(2).toInt()};
        }
    }
    return {};
}

void ScrcpyCapture::startV4l2Reader()
{
    if (!m_capturing || !m_manager)
        return;

    const QString device = m_manager->videoDevice();
    const QSize size = probeV4l2Size(device);
    if (size.isEmpty()) {
        if (QDateTime::currentMSecsSinceEpoch() > m_v4l2Deadline) {
            m_capturing = false;
            emit error(QStringLiteral("No video stream on %1 after 20 s. Is v4l2loopback loaded "
                                      "(exclusive_caps=0) and scrcpy streaming into it?").arg(device));
            return;
        }
        if (!m_v4l2RetryTimer) {
            m_v4l2RetryTimer = new QTimer(this);
            m_v4l2RetryTimer->setSingleShot(true);
            connect(m_v4l2RetryTimer, &QTimer::timeout, this, &ScrcpyCapture::startV4l2Reader);
        }
        m_v4l2RetryTimer->start(500);
        return;
    }

    m_v4l2Width = size.width();
    m_v4l2Height = size.height();
    if (m_v4l2Width != m_lastWidth || m_v4l2Height != m_lastHeight) {
        m_lastWidth = m_v4l2Width;
        m_lastHeight = m_v4l2Height;
        emit frameSizeChanged(m_lastWidth, m_lastHeight);
    }
    m_v4l2Buffer.clear();

    // Android stops producing buffers for a virtual display whose content is
    // not changing, so the continuous stream can sit at zero frames on a
    // healthy idle phone. A one-shot grab still returns the held frame, so
    // seed the provider with it and let the stream take over on motion.
    {
        QProcess seed;
        QStringList seedArgs{QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin")};
        if (device.startsWith(QLatin1String("/dev/")))
            seedArgs << QStringLiteral("-f") << QStringLiteral("v4l2");
        seedArgs << QStringLiteral("-i") << device
                 << QStringLiteral("-frames:v") << QStringLiteral("1")
                 << QStringLiteral("-f") << QStringLiteral("rawvideo")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("bgra") << QStringLiteral("-");
        seed.start(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")), seedArgs);
        const qsizetype frameBytes = qsizetype(m_v4l2Width) * m_v4l2Height * 4;
        if (seed.waitForFinished(5000)) {
            const QByteArray out = seed.readAllStandardOutput();
            if (out.size() >= frameBytes) {
                QImage image(reinterpret_cast<const uchar *>(out.constData()),
                             m_v4l2Width, m_v4l2Height, m_v4l2Width * 4, QImage::Format_ARGB32);
                m_frameProvider->updateFrame(image.copy());
                ++m_frameCount;
                m_v4l2GotFrame = true;  // proves the node is readable
                emit frameReady();
                qCInfo(lcScrcpyCapture) << "v4l2 capture: seeded first frame from the held buffer";
            }
        } else {
            seed.kill();
        }
    }

    m_v4l2Proc = new QProcess(this);
    m_v4l2Proc->setReadChannel(QProcess::StandardOutput);
#ifdef Q_OS_LINUX
    // SIGKILL, not SIGTERM: blocked in the v4l2 driver on a starved device,
    // ffmpeg ignores SIGTERM entirely.
    m_v4l2Proc->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
#endif
    connect(m_v4l2Proc, &QProcess::readyReadStandardOutput, this, &ScrcpyCapture::onV4l2ReadyRead);
    connect(m_v4l2Proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ScrcpyCapture::onV4l2Finished);
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    QStringList args{QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin"),
                     QStringLiteral("-fflags"), QStringLiteral("nobuffer"),
                     QStringLiteral("-flags"), QStringLiteral("low_delay")};
    if (device.startsWith(QLatin1String("/dev/")))
        args << QStringLiteral("-f") << QStringLiteral("v4l2") << QStringLiteral("-i") << device;
    else  // developer convenience: a media file loops as a fake phone feed
        args << QStringLiteral("-re") << QStringLiteral("-stream_loop") << QStringLiteral("-1")
             << QStringLiteral("-i") << device;
    args << QStringLiteral("-f") << QStringLiteral("rawvideo")
         << QStringLiteral("-pix_fmt") << QStringLiteral("bgra") << QStringLiteral("-");
    m_v4l2Proc->start(ffmpeg, args);
    qCInfo(lcScrcpyCapture) << "v4l2 capture:" << device << m_v4l2Width << "x" << m_v4l2Height
                            << "via ffmpeg";
}

void ScrcpyCapture::stopV4l2Reader()
{
    if (m_v4l2RetryTimer)
        m_v4l2RetryTimer->stop();
    if (m_v4l2Proc) {
        QProcess *p = m_v4l2Proc;
        m_v4l2Proc = nullptr;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(1000);
        p->deleteLater();
    }
    m_v4l2Buffer.clear();
}

void ScrcpyCapture::onV4l2ReadyRead()
{
    if (!m_v4l2Proc || !m_capturing)
        return;
    const qsizetype frameBytes = qsizetype(m_v4l2Width) * m_v4l2Height * 4;
    if (frameBytes <= 0)
        return;
    m_v4l2Buffer += m_v4l2Proc->readAllStandardOutput();
    if (m_v4l2Buffer.size() < frameBytes)
        return;
    // Keep only the newest complete frame if we fell behind
    const qsizetype complete = m_v4l2Buffer.size() / frameBytes;
    const qsizetype offset = (complete - 1) * frameBytes;
    QImage image(reinterpret_cast<const uchar *>(m_v4l2Buffer.constData()) + offset,
                 m_v4l2Width, m_v4l2Height, m_v4l2Width * 4, QImage::Format_ARGB32);
    m_frameProvider->updateFrame(image.copy());
    m_v4l2Buffer.remove(0, complete * frameBytes);
    ++m_frameCount;
    m_v4l2GotFrame = true;
    emit frameReady();
}

void ScrcpyCapture::onV4l2Finished(int exitCode, QProcess::ExitStatus)
{
    if (!m_v4l2Proc)
        return;
    const QString err = QString::fromUtf8(m_v4l2Proc->readAllStandardError()).trimmed().right(200);
    m_v4l2Proc->deleteLater();
    m_v4l2Proc = nullptr;
    if (!m_capturing)
        return;
    const bool giveUp = !m_v4l2GotFrame && QDateTime::currentMSecsSinceEpoch() > m_v4l2Deadline;
    qCWarning(lcScrcpyCapture) << "v4l2 capture: ffmpeg exited" << exitCode << err
                               << (giveUp ? "(giving up)" : "(restarting)");
    if (giveUp) {
        m_capturing = false;
        emit error(QStringLiteral("Could not read video from %1: %2")
                       .arg(m_manager ? m_manager->videoDevice() : QString(),
                            err.isEmpty() ? QStringLiteral("ffmpeg produced no frames") : err));
        return;
    }
    if (!m_v4l2RetryTimer) {
        m_v4l2RetryTimer = new QTimer(this);
        m_v4l2RetryTimer->setSingleShot(true);
        connect(m_v4l2RetryTimer, &QTimer::timeout, this, &ScrcpyCapture::startV4l2Reader);
    }
    m_v4l2RetryTimer->start(500);
}

// ─── Manager linkage ──────────────────────────────────────────────────

void ScrcpyCapture::setPhoneMirrorManager(QObject *manager)
{
    m_manager = qobject_cast<PhoneMirrorManager *>(manager);
    qCDebug(lcScrcpyCapture) << "PhoneMirrorManager linked:" << (m_manager != nullptr);
}

void ScrcpyCapture::fetchDeviceInfo()
{
    if (!m_manager)
        return;

    m_adbPath = m_manager->adbPath();
    qCDebug(lcScrcpyCapture) << "ADB path:" << m_adbPath;

    m_deviceSerial = m_manager->getDeviceSerial();
    qCDebug(lcScrcpyCapture) << "Device serial:" << m_deviceSerial;

    // Virtual display (--new-display): its geometry IS the frame geometry,
    // never rotates, and input must be addressed to its display id.
    m_displayId = m_manager->displayId();
    const QString active = m_manager->activeDisplaySize();
    if (active.contains(QLatin1Char('x'))) {
        const QStringList wh = active.split(QLatin1Char('x'));
        m_deviceWidth  = wh.value(0).toInt();
        m_deviceHeight = wh.value(1).toInt();
        m_fixedDisplay = true;
        qCDebug(lcScrcpyCapture) << "Virtual display" << m_deviceWidth << "x" << m_deviceHeight
                                 << "id" << m_displayId;
        return;
    }
    m_fixedDisplay = false;

    const QString resStr = m_manager->getDeviceResolution();
    if (!resStr.isEmpty() && resStr.contains(QLatin1Char('x'))) {
        const QStringList parts = resStr.split(QLatin1Char('x'));
        if (parts.size() == 2) {
            bool okW = false, okH = false;
            const int w = parts[0].toInt(&okW);
            const int h = parts[1].toInt(&okH);
            if (okW && okH) {
                m_deviceWidth = w;
                m_deviceHeight = h;
                qCDebug(lcScrcpyCapture) << "Device resolution:" << w << "x" << h;
            }
        }
    }
}

// ─── Coordinate mapping ──────────────────────────────────────────────

bool ScrcpyCapture::isLandscape() const
{
    if (m_fixedDisplay)
        return false;  // virtual display geometry already matches the frame
    return m_lastWidth > m_lastHeight;
}

QPair<int, int> ScrcpyCapture::convertToDeviceCoords(float relX, float relY) const
{
    int deviceX, deviceY;

    if (isLandscape()) {
        // Landscape: effective width = portrait height, effective height = portrait width
        deviceX = static_cast<int>(relX * m_deviceHeight);
        deviceY = static_cast<int>(relY * m_deviceWidth);
    } else {
        // Portrait: direct mapping
        deviceX = static_cast<int>(relX * m_deviceWidth);
        deviceY = static_cast<int>(relY * m_deviceHeight);
    }

    // Clamp
    deviceX = qBound(0, deviceX, m_deviceWidth - 1);
    deviceY = qBound(0, deviceY, m_deviceHeight - 1);

    return {deviceX, deviceY};
}

// ─── ADB async runner ─────────────────────────────────────────────────

void ScrcpyCapture::runAdbAsync(const QStringList &args)
{
    if (m_adbPath.isEmpty())
        return;

    QStringList fullArgs;
    if (!m_deviceSerial.isEmpty())
        fullArgs << QStringLiteral("-s") << m_deviceSerial;
    fullArgs << args;
    // `input -d <id>` routes the event to scrcpy's virtual display
    if (m_displayId >= 0) {
        const int idx = fullArgs.indexOf(QStringLiteral("input"));
        if (idx >= 0)
            fullArgs.insert(idx + 1, QStringLiteral("-d"));
        if (idx >= 0)
            fullArgs.insert(idx + 2, QString::number(m_displayId));
    }

    // Fire-and-forget process (auto-deletes on finish)
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            proc, &QProcess::deleteLater);
    connect(proc, &QProcess::errorOccurred, proc, &QProcess::deleteLater);
    proc->start(m_adbPath, fullArgs);
}

// ─── Touch input ──────────────────────────────────────────────────────

void ScrcpyCapture::sendTap(float relX, float relY)
{
    if (m_adbPath.isEmpty() || m_deviceWidth <= 0) {
        if (!m_warnedNoGeometry) {
            m_warnedNoGeometry = true;
            qCWarning(lcScrcpyCapture) << "Touch dropped: adb path or device geometry unknown";
        }
        return;
    }

    const auto [dx, dy] = convertToDeviceCoords(relX, relY);

    qCDebug(lcScrcpyCapture) << "ADB tap: rel(" << relX << relY << ") -> device("
                              << dx << dy << ")"
                              << (isLandscape() ? "[landscape]" : "[portrait]");

    runAdbAsync({
        QStringLiteral("shell"), QStringLiteral("input"),
        QStringLiteral("tap"),
        QString::number(dx), QString::number(dy)
    });
}

void ScrcpyCapture::sendSwipe(float relX1, float relY1,
                               float relX2, float relY2,
                               int durationMs)
{
    if (m_adbPath.isEmpty() || m_deviceWidth <= 0) {
        if (!m_warnedNoGeometry) {
            m_warnedNoGeometry = true;
            qCWarning(lcScrcpyCapture) << "Touch dropped: adb path or device geometry unknown";
        }
        return;
    }

    const auto [x1, y1] = convertToDeviceCoords(relX1, relY1);
    const auto [x2, y2] = convertToDeviceCoords(relX2, relY2);

    qCDebug(lcScrcpyCapture) << "ADB swipe:" << x1 << y1 << "->" << x2 << y2
                              << "duration=" << durationMs << "ms"
                              << (isLandscape() ? "[landscape]" : "[portrait]");

    runAdbAsync({
        QStringLiteral("shell"), QStringLiteral("input"),
        QStringLiteral("swipe"),
        QString::number(x1), QString::number(y1),
        QString::number(x2), QString::number(y2),
        QString::number(durationMs)
    });
}

void ScrcpyCapture::sendTouchEvent(float relX, float relY, bool pressed)
{
    if (pressed) {
        m_touchStartX = relX;
        m_touchStartY = relY;
        m_touchMoved = false;
        qCDebug(lcScrcpyCapture) << "Touch START at rel(" << relX << relY << ")";
    } else {
        qCDebug(lcScrcpyCapture) << "Touch END at rel(" << relX << relY << ")";
        const float dx = std::abs(relX - m_touchStartX);
        const float dy = std::abs(relY - m_touchStartY);

        if (m_touchMoved && (dx > 0.05f || dy > 0.05f)) {
            qCDebug(lcScrcpyCapture) << "Detected SWIPE (dx=" << dx << "dy=" << dy << ")";
            sendSwipe(m_touchStartX, m_touchStartY, relX, relY, 200);
        } else {
            qCDebug(lcScrcpyCapture) << "Detected TAP";
            sendTap(m_touchStartX, m_touchStartY);
        }
    }
}

void ScrcpyCapture::sendTouchMove(float relX, float relY)
{
    Q_UNUSED(relX)
    Q_UNUSED(relY)
    m_touchMoved = true;
}

// =====================================================================
// ScrcpyCaptureItem
// =====================================================================

ScrcpyCaptureItem::ScrcpyCaptureItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    qCDebug(lcScrcpyCapture) << "ScrcpyCaptureItem created";
}

int ScrcpyCaptureItem::frameWidth() const
{
    return m_capture ? m_capture->frameWidth() : 0;
}

int ScrcpyCaptureItem::frameHeight() const
{
    return m_capture ? m_capture->frameHeight() : 0;
}

bool ScrcpyCaptureItem::isStreaming() const
{
    return m_capture != nullptr && m_capture->isCapturing();
}

void ScrcpyCaptureItem::setCapture(ScrcpyCapture *capture)
{
    m_capture = capture;
    if (capture) {
        connect(capture, &ScrcpyCapture::frameReady,
                this, &ScrcpyCaptureItem::onFrameReady);
        connect(capture, &ScrcpyCapture::error,
                this, &ScrcpyCaptureItem::onCaptureError);
    }
}

void ScrcpyCaptureItem::connectToManager(QObject *manager)
{
    // Disconnect previous
    if (m_manager) {
        m_manager->disconnect(this);
    }

    m_manager = manager;
    if (!manager)
        return;

    qCDebug(lcScrcpyCapture) << "ScrcpyCaptureItem connecting to manager";

    // Use PhoneMirrorManager signals
    auto *pmm = qobject_cast<PhoneMirrorManager *>(manager);
    if (!pmm)
        return;

    connect(pmm, &PhoneMirrorManager::scrcpyStarted,
            this, &ScrcpyCaptureItem::onScrcpyStarted, Qt::QueuedConnection);
    connect(pmm, &PhoneMirrorManager::scrcpyStopped,
            this, &ScrcpyCaptureItem::onScrcpyStopped, Qt::QueuedConnection);
    connect(pmm, &PhoneMirrorManager::scrcpyError,
            this, &ScrcpyCaptureItem::onScrcpyError, Qt::QueuedConnection);

    // If already running, kick off capture
    if (pmm->isRunning() && pmm->scrcpyWindowHandle()) {
        qCDebug(lcScrcpyCapture) << "Manager already has running scrcpy";
        QTimer::singleShot(100, this, [this, hwnd = pmm->scrcpyWindowHandle()]() {
            onScrcpyStarted(hwnd);
        });
    }
}

void ScrcpyCaptureItem::handleClick(float x, float y)
{
    if (m_capture && m_capture->isCapturing()) {
        const float relX = x / static_cast<float>(width());
        const float relY = y / static_cast<float>(height());
        m_capture->sendTap(relX, relY);
    }
}

void ScrcpyCaptureItem::detach()
{
    qCDebug(lcScrcpyCapture) << "ScrcpyCaptureItem detaching (pausing capture)";
    if (m_capture)
        m_capture->stopCapture();
}

void ScrcpyCaptureItem::reattach()
{
    qCDebug(lcScrcpyCapture) << "ScrcpyCaptureItem reattaching (resuming capture)";
    auto *pmm = qobject_cast<PhoneMirrorManager *>(m_manager);
    if (pmm && pmm->isRunning() && m_capture) {
        const int hwnd = pmm->scrcpyWindowHandle();
        if (hwnd) {
            m_capture->setWindowHandle(hwnd);
            m_capture->startCapture();
            emit streamStarted();
        }
    }
}

void ScrcpyCaptureItem::componentComplete()
{
    QQuickItem::componentComplete();
    qCDebug(lcScrcpyCapture) << "ScrcpyCaptureItem component complete";
}

// ─── Mouse events ─────────────────────────────────────────────────────

void ScrcpyCaptureItem::mousePressEvent(QMouseEvent *event)
{
    if (m_capture && m_capture->isCapturing()) {
        const QPointF pos = event->position();
        const float relX = static_cast<float>(pos.x() / width());
        const float relY = static_cast<float>(pos.y() / height());
        m_capture->sendTouchEvent(relX, relY, true);
    }
    event->accept();
}

void ScrcpyCaptureItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_capture && m_capture->isCapturing()) {
        const QPointF pos = event->position();
        const float relX = static_cast<float>(pos.x() / width());
        const float relY = static_cast<float>(pos.y() / height());
        m_capture->sendTouchEvent(relX, relY, false);
    }
    event->accept();
}

void ScrcpyCaptureItem::mouseMoveEvent(QMouseEvent *event)
{
    if (m_capture && m_capture->isCapturing()) {
        const QPointF pos = event->position();
        const float relX = static_cast<float>(pos.x() / width());
        const float relY = static_cast<float>(pos.y() / height());
        m_capture->sendTouchMove(relX, relY);
    }
    event->accept();
}

// ─── Private slots ────────────────────────────────────────────────────

void ScrcpyCaptureItem::onScrcpyStarted(int hwnd)
{
    qCDebug(lcScrcpyCapture) << "Scrcpy started, hwnd=" << hwnd;

    if (!m_capture) {
        qCDebug(lcScrcpyCapture) << "No capture instance set!";
        return;
    }

    m_capture->setWindowHandle(hwnd);
    QTimer::singleShot(200, m_capture, &ScrcpyCapture::startCapture);
    emit streamStarted();
}

void ScrcpyCaptureItem::onScrcpyStopped()
{
    qCDebug(lcScrcpyCapture) << "Scrcpy stopped";
    if (m_capture)
        m_capture->stopCapture();
    emit streamStopped();
}

void ScrcpyCaptureItem::onScrcpyError(const QString &msg)
{
    qCDebug(lcScrcpyCapture) << "Error:" << msg;
    if (m_capture)
        m_capture->stopCapture();
    emit errorOccurred(msg);
}

void ScrcpyCaptureItem::onFrameReady()
{
    ++m_frameCounter;
    emit frameRefresh();
}

void ScrcpyCaptureItem::onCaptureError(const QString &msg)
{
    emit errorOccurred(msg);
}

#endif // Q_OS_MOBILE
