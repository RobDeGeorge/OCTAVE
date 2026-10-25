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
 *
 * Native mode (androidautomanager_native.cpp, OCTAVE_HAVE_NATIVE_AA): no
 * DHU at all. OCTAVE is the head unit and speaks the Android Auto Protocol
 * to the phone's head unit server through an adb forward
 * (src/android_auto/, docs/ANDROID_AUTO_PROTOCOL.md); video lands on a
 * QVideoSink, audio goes through the volume system, touch goes back.
 * Picked by the androidAutoBackend setting ("dhu" | "native").
 */

#ifndef ANDROIDAUTOMANAGER_H
#define ANDROIDAUTOMANAGER_H

#include "androidautomedia.h"

#ifndef Q_OS_MOBILE

#include <QObject>
#include <functional>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QVideoSink>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcAndroidAuto)

class AapConnection;
class SettingsManager;

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
    // Every frame also goes to `tap` (the service's "androidAutoManager.
    // frames" stream, set once at startup; unset = nothing extra)
    static void setFrameTap(std::function<void(const QImage &)> tap);

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
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "phone")
    Q_CLASSINFO("octave.scope.read", "getDhuInstallInstructions")
    Q_CLASSINFO("octave.scope.full", "cleanup")
    Q_CLASSINFO("octave.experimental",
                "backend nativeAvailable nativeState nativeStatus phoneName videoStreaming "
                "certificateDir videoSink startNative stopNative injectTouch setViewVisible "
                "backendChanged nativeStateChanged videoStreamingChanged videoSinkChanged")

    Q_PROPERTY(bool    isDhuInstalled  READ isDhuInstalled  CONSTANT)
    Q_PROPERTY(QString dhuPath         READ dhuPath         CONSTANT)
    Q_PROPERTY(bool    isDhuEmbedded   READ isDhuEmbedded   NOTIFY dhuEmbeddedChanged)
    Q_PROPERTY(int     dhuWindowHandle READ dhuWindowHandle  NOTIFY dhuWindowReady)
    Q_PROPERTY(QObject* dhuCapture     READ dhuCapture      CONSTANT)

    // Native mode. backend: "native" (OCTAVE is the head unit) or "dhu".
    // nativeState: idle | connecting | waitingForPhone | waitingForServer |
    // needsCertificate | running | error; nativeStatus says the same for a person.
    Q_PROPERTY(QString backend         READ backend         NOTIFY backendChanged)
    Q_PROPERTY(bool    nativeAvailable READ nativeAvailable CONSTANT)
    Q_PROPERTY(QString nativeState     READ nativeState     NOTIFY nativeStateChanged)
    Q_PROPERTY(QString nativeStatus    READ nativeStatus    NOTIFY nativeStateChanged)
    Q_PROPERTY(QString phoneName       READ phoneName       NOTIFY nativeStateChanged)
    Q_PROPERTY(bool    videoStreaming  READ videoStreaming  NOTIFY videoStreamingChanged)
    // Where headunit.crt + headunit.key are looked for when
    // androidAutoCertPath is empty
    Q_PROPERTY(QString certificateDir  READ certificateDir  CONSTANT)
    Q_PROPERTY(QObject* videoSink      READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

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

    // Native mode
    QString backend() const;
    bool    nativeAvailable() const;
    QString nativeState() const { return m_nativeState; }
    QString nativeStatus() const { return m_nativeStatus; }
    QString phoneName() const { return m_phoneName; }
    bool    videoStreaming() const { return m_videoStreaming; }
    QString certificateDir() const;
    QObject *videoSink() const { return m_videoSink; }
    void    setVideoSink(QObject *sink);

    // Wired by AppCore: settings (backend, certificate, video mode), the
    // volume system, the audio output and night mode follow these
    void setSettingsManager(SettingsManager *settings);
    void setVolume(float linear);
    void setMuted(bool muted);
    void setAudioOutputDevice(const QString &id);
    void setNightMode(bool night);
    // Android Auto's music for NowPlaying (native mode)
    AndroidAutoMedia *media() { return &m_media; }

    // Connects to the phone's head unit server (adb) and runs a session.
    // False when it can't even begin; nativeState says why.
    Q_INVOKABLE bool startNative();
    Q_INVOKABLE void stopNative();
    // A touch on the picture: pointerId from the touch area, action 0 down /
    // 1 up / 2 move, x / y in 0..1 of the video
    Q_INVOKABLE void injectTouch(int pointerId, int action, double x, double y);
    // The view is on screen (false: the phone stops sending video)
    Q_INVOKABLE void setViewVisible(bool visible);

signals:
    void connectionProgress(const QString &message);
    void error(const QString &message);
    void dhuWindowReady(int windowHandle);
    void dhuEmbeddedChanged(bool embedded);
    void backendChanged();
    void nativeStateChanged();
    void videoStreamingChanged();
    void videoSinkChanged();

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

    // Native mode (androidautomanager_native.cpp)
    void setNativeState(const QString &state, const QString &status);
    void nativeStep();                  // one connect attempt, adb work off the GUI thread
    void onNativeFinished();
    void scheduleRetry(int ms);
    void removeForward();
    bool loadCertificate(QByteArray &cert, QByteArray &key, QString &why) const;
    QVariant setting(const QString &key, const QVariant &fallback) const;

    SettingsManager *m_settings = nullptr;
    AapConnection   *m_aap = nullptr;
    QPointer<QVideoSink> m_videoSink;
    QTimer          *m_retryTimer = nullptr;
    QString          m_nativeState = QStringLiteral("idle");
    QString          m_nativeStatus;
    QString          m_phoneName;
    QString          m_serial;
    int              m_forwardPort = 0;
    bool             m_nativeWanted = false;
    bool             m_stepRunning = false;
    bool             m_videoStreaming = false;
    bool             m_viewVisible = true;
    bool             m_nightMode = true;
    float            m_volume = 1.0f;
    bool             m_muted = false;
    QString          m_audioDeviceId;
    int              m_videoWidth = 1280;
    int              m_videoHeight = 720;
    QHash<int, int>  m_touchSlots;     // touch area pointId → Android Auto pointer id
    QHash<int, QPoint> m_touchPos;     // Android Auto pointer id → last position (video px)
    AndroidAutoMedia m_media;
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
    Q_PROPERTY(QString backend READ backend CONSTANT)
    Q_PROPERTY(bool nativeAvailable READ nativeAvailable CONSTANT)
    Q_PROPERTY(QString nativeState READ nativeState CONSTANT)
    Q_PROPERTY(QString nativeStatus READ nativeStatus CONSTANT)
    Q_PROPERTY(QString phoneName READ phoneName CONSTANT)
    Q_PROPERTY(bool videoStreaming READ videoStreaming CONSTANT)
    Q_PROPERTY(QString certificateDir READ certificateDir CONSTANT)
    Q_PROPERTY(QObject* videoSink READ videoSink WRITE setVideoSink)
public:
    explicit AndroidAutoManager(QObject *parent = nullptr)
        : QObject(parent), m_provider(new DhuFrameProvider()) {}
    // The provider is not ours to delete, as on desktop: main.cpp hands it
    // to the QML engine (addImageProvider takes ownership), so deleting it
    // here freed it twice at exit
    ~AndroidAutoManager() override = default;
    void cleanup() {}
    bool isDhuInstalled() const { return false; }
    bool isDhuEmbedded() const { return false; }
    DhuFrameProvider *frameProvider() const { return m_provider; }
    QString backend() const { return QStringLiteral("dhu"); }
    bool nativeAvailable() const { return false; }
    QString nativeState() const { return QStringLiteral("idle"); }
    QString nativeStatus() const { return QString(); }
    QString phoneName() const { return QString(); }
    bool videoStreaming() const { return false; }
    QString certificateDir() const { return QString(); }
    QObject *videoSink() const { return nullptr; }
    void setVideoSink(QObject *) {}
    void setSettingsManager(class SettingsManager *) {}
    void setVolume(float) {}
    void setMuted(bool) {}
    void setAudioOutputDevice(const QString &) {}
    void setNightMode(bool) {}
    AndroidAutoMedia *media() { return &m_media; }
    Q_INVOKABLE bool startNative() { return false; }
    Q_INVOKABLE void stopNative() {}
    Q_INVOKABLE void injectTouch(int, int, double, double) {}
    Q_INVOKABLE void setViewVisible(bool) {}

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
    AndroidAutoMedia m_media;      // never connected
};

#endif // Q_OS_MOBILE
#endif // ANDROIDAUTOMANAGER_H
