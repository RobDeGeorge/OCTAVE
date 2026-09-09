#ifndef SCRCPYCAPTURE_H
#define SCRCPYCAPTURE_H

#ifndef Q_OS_MOBILE

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QProcess>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QTimer>
#include <Qt>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcScrcpyCapture)

class PhoneMirrorManager;

// ─── Frame provider ───────────────────────────────────────────────────

class ScrcpyFrameProvider : public QQuickImageProvider
{
public:
    ScrcpyFrameProvider();

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;
    void updateFrame(const QImage &frame);
    bool hasFrame() const;

private:
    QImage m_currentFrame;
    mutable QMutex m_mutex;
};

// ─── ScrcpyCapture (QObject) ─────────────────────────────────────────

class ScrcpyCapture : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isCapturing READ isCapturing NOTIFY captureStarted)
    Q_PROPERTY(int windowHandle READ windowHandle CONSTANT)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameSizeChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameSizeChanged)

public:
    explicit ScrcpyCapture(QObject *parent = nullptr);
    ~ScrcpyCapture() override;

    // Accessors
    bool isCapturing() const { return m_capturing; }
    int  windowHandle() const { return m_hwnd; }
    int  frameWidth() const { return m_lastWidth; }
    int  frameHeight() const { return m_lastHeight; }

    ScrcpyFrameProvider *frameProvider() const { return m_frameProvider; }

signals:
    void frameReady();
    void captureStarted();
    void captureStopped();
    void error(const QString &message);
    void frameSizeChanged(int width, int height);

public slots:
    void setWindowHandle(int hwnd);
    // v4l2 mode: QML reports the size of frames it is rendering so touch
    // coordinate mapping knows the orientation
    void setFrameSize(int width, int height);
    void startCapture();
    void stopCapture();

    // Touch / input forwarding via ADB
    void sendTouchEvent(float relX, float relY, bool pressed);
    void sendTouchMove(float relX, float relY);
    void sendTap(float relX, float relY);
    void sendSwipe(float relX1, float relY1,
                   float relX2, float relY2,
                   int durationMs = 300);

    // Manager linkage
    void setPhoneMirrorManager(QObject *manager);

private slots:
    void captureFrame();

private:
    // v4l2 mode (Linux): ffmpeg subprocess decodes the loopback node to raw
    // BGRA on stdout; frames are pushed into the provider from readyRead.
    void startV4l2Reader();
    void stopV4l2Reader();
    void onV4l2ReadyRead();
    void onV4l2Finished(int exitCode, QProcess::ExitStatus status);
    void startSeedGrab(const QString &device);
    void onSeedFinished(int exitCode, QProcess::ExitStatus status);
    QSize probeV4l2Size(const QString &device) const;

    // Coordinate mapping
    bool isLandscape() const;
    QPair<int, int> convertToDeviceCoords(float relX, float relY) const;
    void fetchDeviceInfo();
    void runAdbAsync(const QStringList &args);

    // Members
    int m_hwnd = 0;
    bool m_capturing = false;
    QTimer *m_captureTimer = nullptr;
    ScrcpyFrameProvider *m_frameProvider = nullptr;
    int m_targetFps = 60;
    int m_lastWidth = 0;
    int m_lastHeight = 0;
    int m_frameCount = 0;

    // ADB input state
    QString m_adbPath;
    QString m_deviceSerial;
    int m_deviceWidth = 0;
    int m_deviceHeight = 0;
    int m_displayId = -1;          // target display for `input -d`
    bool m_fixedDisplay = false;   // virtual display: geometry never rotates
    PhoneMirrorManager *m_manager = nullptr;
    bool m_warnedNoGeometry = false;

    // v4l2 reader state
    QProcess *m_v4l2Proc = nullptr;
    QByteArray m_v4l2Buffer;
    int m_v4l2Width = 0;
    int m_v4l2Height = 0;
    bool m_v4l2GotFrame = false;
    QTimer *m_v4l2RetryTimer = nullptr;
    qint64 m_v4l2Deadline = 0;
    QProcess *m_seedProc = nullptr;   // one-shot grab racing the stream for the first frame
    bool m_seedPending = false;
    qint64 m_seedStartedMs = 0;

    // Touch tracking for tap/swipe detection
    float m_touchStartX = 0.0f;
    float m_touchStartY = 0.0f;
    bool m_touchMoved = false;
};

// ─── ScrcpyCaptureItem (QQuickItem) ──────────────────────────────────

class ScrcpyCaptureItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int frameCounter READ frameCounter NOTIFY frameRefresh)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY streamStarted)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY streamStarted)
    Q_PROPERTY(bool isStreaming READ isStreaming NOTIFY streamStarted)

public:
    explicit ScrcpyCaptureItem(QQuickItem *parent = nullptr);

    int  frameCounter() const { return m_frameCounter; }
    int  frameWidth() const;
    int  frameHeight() const;
    bool isStreaming() const;

    void setCapture(ScrcpyCapture *capture);

signals:
    void streamStarted();
    void streamStopped();
    void errorOccurred(const QString &message);
    void frameRefresh();

public slots:
    Q_INVOKABLE void connectToManager(QObject *manager);
    Q_INVOKABLE void handleClick(float x, float y);
    void detach();
    void reattach();

protected:
    void componentComplete() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void onScrcpyStarted(int hwnd);
    void onScrcpyStopped();
    void onScrcpyError(const QString &msg);
    void onFrameReady();
    void onCaptureError(const QString &msg);

private:
    QObject *m_manager = nullptr;
    ScrcpyCapture *m_capture = nullptr;
    int m_frameCounter = 0;
};

#else // Q_OS_MOBILE — mobile stubs

#include <QObject>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class ScrcpyFrameProvider : public QQuickImageProvider
{
public:
    ScrcpyFrameProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *, const QSize &) override { return {}; }
};

class ScrcpyCapture : public QObject
{
    Q_OBJECT
public:
    explicit ScrcpyCapture(QObject *parent = nullptr)
        : QObject(parent), m_provider(new ScrcpyFrameProvider()) {}
    ~ScrcpyCapture() override { delete m_provider; }
    ScrcpyFrameProvider *frameProvider() const { return m_provider; }
    void setPhoneMirrorManager(QObject *) {}

public slots:
    void setWindowHandle(int) {}
    void setFrameSize(int, int) {}
    void startCapture() {}
    void stopCapture() {}
    void sendTouchEvent(float, float, bool) {}
    void sendTouchMove(float, float) {}

signals:
    void frameReady();
    void captureStarted();
    void captureStopped();
    void error(const QString &);  // PhoneMirrorView.qml onError

private:
    ScrcpyFrameProvider *m_provider;
};

class ScrcpyCaptureItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit ScrcpyCaptureItem(QQuickItem *parent = nullptr) : QQuickItem(parent) {}
signals:
    void streamStarted();
    void streamStopped();
    void errorOccurred(const QString &);
    void frameRefresh();
};

#endif // Q_OS_MOBILE
#endif // SCRCPYCAPTURE_H
