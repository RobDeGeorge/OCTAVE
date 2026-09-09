#ifndef PHONEMIRRORMANAGER_H
#define PHONEMIRRORMANAGER_H

// Phone mirroring via OCTAVE's built-in scrcpy-protocol client
// (src/phone_mirror/scrcpyclient.{h,cpp}): the bundled server jar is pushed
// to the phone over adb and OCTAVE decodes the stream and injects touch
// itself. Nothing has to be installed by the user.
// Mirrors backend/phone_mirror/manager.py.

#ifndef Q_OS_MOBILE

#include <QObject>
#include <QString>
#include <QStringList>
#include <QPair>
#include <QList>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcPhoneMirror)

class ScrcpyClient;

class PhoneMirrorManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString adbPath READ adbPath CONSTANT)
    // libavcodec linked, server jar embedded, adb found
    Q_PROPERTY(bool nativeAvailable READ nativeAvailable CONSTANT)
    Q_PROPERTY(QString serverVersion READ serverVersion CONSTANT)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    // Requested virtual display "WxH" (--new-display, Android 11+); "" = phone screen
    Q_PROPERTY(QString displaySize READ displaySize NOTIFY displaySizeChanged)
    // "WxH" of the virtual display in use, "" when mirroring the phone screen
    Q_PROPERTY(QString activeDisplaySize READ activeDisplaySize NOTIFY frameSizeChanged)
    // QML binds VideoOutput.videoSink here; decoded frames go straight to it
    Q_PROPERTY(QObject* videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameSizeChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameSizeChanged)

public:
    explicit PhoneMirrorManager(QObject *parent = nullptr);
    ~PhoneMirrorManager() override;

    QString adbPath() const { return m_adbPath; }
    bool nativeAvailable() const;
    QString serverVersion() const;
    bool isRunning() const;
    QString displaySize() const { return m_displaySize; }
    QString activeDisplaySize() const { return m_activeDisplaySize; }
    QObject *videoSink() const { return m_videoSink; }
    void setVideoSink(QObject *sink);
    int frameWidth() const { return m_frameWidth; }
    int frameHeight() const { return m_frameHeight; }

    // Validate "WxH": dimensions snapped down to multiples of 8 (scrcpy does the
    // same to a --new-display size), "" for invalid input.
    static QString normalizeDisplaySize(const QString &value);
    static constexpr int kNewDisplayMinSdk = 30;  // Android 11

signals:
    void error(const QString &message);
    // Session state (names kept from the original scrcpy-binary design;
    // PhoneMirrorView.qml depends on them)
    void scrcpyStarted(int handle);
    void scrcpyStopped();
    void scrcpyError(const QString &message);
    void isRunningChanged();
    void displaySizeChanged();
    void frameSizeChanged(int width, int height);
    void frameReady();
    void videoSinkChanged();

public slots:
    // Called by VolumeController on every volume change; audio forwarding is
    // not implemented in the built-in client yet, so this only records it.
    void setVolume(float volume) { m_volume = volume; }
    void setAudioEnabled(bool enabled);
    void setDisplaySize(const QString &size);
    bool environmentOk();
    QString getInstallInstructions();
    // "device" | "unauthorized" | "offline" | "none" | "no-adb"
    QString getDeviceState();
    static QString describeDeviceState(const QString &state);
    bool hasConnectedDevice();
    QString getDeviceSerial();
    QString getDeviceName();
    QString getDeviceResolution();
    int getDeviceSdk();
    // Touch on the mirrored display: action 0 down, 1 up, 2 move; rel 0..1
    void injectTouch(int pointerId, int action, float relX, float relY);
    void injectKey(int keycode);
    void pressHome();
    void pressBack();
    void pressAppSwitch();
    void startScrcpy();
    void stopScrcpy();
    void cleanup();

private:
    QString findAdb() const;
    QString runAdb(const QStringList &args, int timeoutMs = 10000) const;
    QList<QPair<QString, QString>> devices() const;  // (serial, state)
    void killStaleServer();
    void onConnected(int w, int h);
    void onDisconnected(const QString &reason);

    QString m_adbPath;
    bool m_audioEnabled = false;
    float m_volume = 1.0f;
    QString m_displaySize;
    QString m_activeDisplaySize;

    ScrcpyClient *m_client = nullptr;
    QObject *m_videoSink = nullptr;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    bool m_isStarting = false;
    bool m_isStopping = false;
    bool m_ready = false;
};

#else // Q_OS_MOBILE — mobile stub

#include <QObject>
#include <QString>

class PhoneMirrorManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString adbPath READ adbPath CONSTANT)
    Q_PROPERTY(bool nativeAvailable READ nativeAvailable CONSTANT)
    Q_PROPERTY(QString serverVersion READ serverVersion CONSTANT)
    Q_PROPERTY(bool isRunning READ isRunning CONSTANT)
    Q_PROPERTY(QString displaySize READ displaySize CONSTANT)
    Q_PROPERTY(QString activeDisplaySize READ activeDisplaySize CONSTANT)
    Q_PROPERTY(QObject* videoSink READ videoSink WRITE setVideoSink CONSTANT)
    Q_PROPERTY(int frameWidth READ frameWidth CONSTANT)
    Q_PROPERTY(int frameHeight READ frameHeight CONSTANT)
public:
    explicit PhoneMirrorManager(QObject *parent = nullptr) : QObject(parent) {}
    void cleanup() {}
    QString adbPath() const { return {}; }
    bool nativeAvailable() const { return false; }
    QString serverVersion() const { return {}; }
    bool isRunning() const { return false; }
    QString displaySize() const { return {}; }
    QString activeDisplaySize() const { return {}; }
    QObject *videoSink() const { return nullptr; }
    void setVideoSink(QObject *) {}
    int frameWidth() const { return 0; }
    int frameHeight() const { return 0; }

public slots:
    void setVolume(float) {}
    void setAudioEnabled(bool) {}
    void setDisplaySize(const QString &) {}
    bool environmentOk() const { return false; }
    QString getInstallInstructions() const { return QStringLiteral("Phone mirroring is desktop-only"); }
    QString getDeviceState() const { return QStringLiteral("none"); }
    bool hasConnectedDevice() const { return false; }
    void injectTouch(int, int, float, float) {}
    void injectKey(int) {}
    void pressHome() {}
    void pressBack() {}
    void pressAppSwitch() {}
    void startScrcpy() {}
    void stopScrcpy() {}

signals:
    void error(const QString &);
    void scrcpyStarted(int);
    void scrcpyStopped();
    void scrcpyError(const QString &);
    void isRunningChanged();
    void displaySizeChanged();
    void frameSizeChanged(int, int);
    void frameReady();
    void videoSinkChanged();
};

#endif // Q_OS_MOBILE
#endif // PHONEMIRRORMANAGER_H
