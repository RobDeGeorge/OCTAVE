/*
 * Android Auto Manager for OCTAVE (C++ port)
 *
 * Manages Google's Desktop Head Unit (DHU) subprocess:
 *   - Auto-detects DHU and ADB in standard Android SDK paths
 *   - Sets up ADB port forwarding (tcp:5277)
 *   - Launches DHU, captures its window frames via QScreen::grabWindow()
 *   - Provides frames to QML through a QQuickImageProvider ("dhuframe")
 *   - Forwards click input to the DHU window
 *
 * Desktop-only — excluded on mobile builds.
 */

#ifndef ANDROIDAUTOMANAGER_H
#define ANDROIDAUTOMANAGER_H

#ifndef Q_OS_MOBILE

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcAndroidAuto)

// ─── DhuFrameProvider ─────────────────────────────────────────────────
// QQuickImageProvider subclass that serves the most recent captured frame
// to QML via the "image://dhuframe/<counter>" URL scheme.

class DhuFrameProvider : public QQuickImageProvider
{
public:
    DhuFrameProvider();

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;

    void updateFrame(const QImage &image);

private:
    QImage  m_currentImage;
    QMutex  m_mutex;
};

// ─── DhuCapture ───────────────────────────────────────────────────────
// Captures the DHU window at a fixed interval and pushes frames into
// DhuFrameProvider.  Also handles mouse-click forwarding.

class DhuCapture : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isCapturing READ isCapturing NOTIFY captureStarted)
    Q_PROPERTY(int  windowHandle READ windowHandle WRITE setWindowHandle)

public:
    explicit DhuCapture(QObject *parent = nullptr);
    ~DhuCapture() override;

    // Accessors
    bool isCapturing() const;
    int  windowHandle() const;

    // The image provider handed to the QML engine
    DhuFrameProvider *frameProvider() const;

public slots:
    void setWindowHandle(int hwnd);
    void startCapture();
    void stopCapture();
    void sendMouseClick(int x, int y);

signals:
    void frameReady();
    void captureStarted();
    void captureStopped();
    void error(const QString &message);

private slots:
    void captureFrame();

private:
    int              m_hwnd      = 0;
    bool             m_capturing = false;
    int              m_targetFps = 30;
    QTimer          *m_captureTimer = nullptr;
    DhuFrameProvider *m_frameProvider = nullptr;
};

// ─── AndroidAutoManager ───────────────────────────────────────────────

class AndroidAutoManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    isDhuInstalled  READ isDhuInstalled  CONSTANT)
    Q_PROPERTY(QString dhuPath         READ dhuPath         CONSTANT)
    Q_PROPERTY(bool    isDhuEmbedded   READ isDhuEmbedded   NOTIFY dhuEmbeddedChanged)
    Q_PROPERTY(int     dhuWindowHandle READ dhuWindowHandle  NOTIFY dhuWindowReady)
    Q_PROPERTY(QObject* dhuCapture     READ dhuCapture      CONSTANT)

public:
    explicit AndroidAutoManager(QObject *parent = nullptr);
    ~AndroidAutoManager() override;

    // Property accessors
    bool    isDhuInstalled() const;
    QString dhuPath() const;
    bool    isDhuEmbedded() const;
    int     dhuWindowHandle() const;
    QObject *dhuCapture() const;

    // Gives main.cpp access to the provider so it can register it with
    // the QML engine before load.
    DhuFrameProvider *frameProvider() const;

signals:
    void connectionProgress(const QString &message);
    void error(const QString &message);
    void dhuWindowReady(int windowHandle);
    void dhuEmbeddedChanged(bool embedded);

public slots:
    QString getDhuInstallInstructions();
    bool    launchDhuSeamless();
    void    sendDhuClick(int x, int y);
    void    closeDhu();
    void    cleanup();

private slots:
    void pollAndHideDhu();
    void startDhuCapture();
    void setupSeamlessCapture();

    void onDhuFrameReady();
    void onDhuCaptureError(const QString &message);

    void onDhuProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    // Binary detection
    QString findDhuPath() const;
    QString findAdbPath() const;

    // ADB helpers
    QString runAdb(const QStringList &args, int timeoutMs = 10000) const;
    bool    isHeadunitServerRunning() const;
    bool    startHeadunitServer();
    bool    setupAdbForward();
    bool    prepareAdbConnection();

    // Window finding (platform-specific)
    int findDhuWindow() const;

    // Members
    QProcess    *m_dhuProcess      = nullptr;
    DhuCapture  *m_dhuCaptureObj   = nullptr;
    int          m_dhuHwnd         = 0;
    bool         m_dhuEmbedded     = false;
    int          m_hidePollCount   = 0;
    QString      m_cachedDhuPath;
    QString      m_cachedAdbPath;
};

#else // Q_OS_MOBILE — mobile stub. DhuFrameProvider/DhuCapture not provided.

#include <QObject>
#include <QString>
#include <QQuickImageProvider>

// Stub image provider so main.cpp can still pass it to addImageProvider
class DhuFrameProvider : public QQuickImageProvider
{
public:
    DhuFrameProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *, const QSize &) override { return {}; }
};

class AndroidAutoManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDhuInstalled READ isDhuInstalled CONSTANT)
    Q_PROPERTY(bool isDhuEmbedded READ isDhuEmbedded CONSTANT)
public:
    explicit AndroidAutoManager(QObject *parent = nullptr)
        : QObject(parent), m_provider(new DhuFrameProvider()) {}
    ~AndroidAutoManager() override { delete m_provider; }
    void cleanup() {}
    bool isDhuInstalled() const { return false; }
    bool isDhuEmbedded() const { return false; }
    DhuFrameProvider *frameProvider() const { return m_provider; }

public slots:
    bool launchDhuSeamless() { return false; }
    void closeDhu() {}

signals:
    void dhuEmbeddedChanged(bool);
    // Mirrored from the desktop class for AndroidAutoView.qml handlers.
    void error(const QString &);
    void dhuWindowReady(int);

private:
    DhuFrameProvider *m_provider;
};

#endif // Q_OS_MOBILE
#endif // ANDROIDAUTOMANAGER_H
