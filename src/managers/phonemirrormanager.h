#ifndef PHONEMIRRORMANAGER_H
#define PHONEMIRRORMANAGER_H

#ifndef Q_OS_MOBILE

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcPhoneMirror)

class PhoneMirrorManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isScrcpyInstalled READ isScrcpyInstalled NOTIFY scrcpyPathChanged)
    Q_PROPERTY(QString scrcpyPath READ scrcpyPath NOTIFY scrcpyPathChanged)
    Q_PROPERTY(QString adbPath READ adbPath CONSTANT)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int scrcpyWindowHandle READ scrcpyWindowHandle NOTIFY scrcpyStarted)
    // Version string of the effective scrcpy binary ("3.3.1"), "" if unknown
    Q_PROPERTY(QString scrcpyVersion READ scrcpyVersion NOTIFY scrcpyPathChanged)
    // How mirrored video reaches QML: "window" (Windows, screen-grab),
    // "v4l2" (Linux, headless scrcpy -> v4l2loopback -> QtMultimedia), "unsupported"
    Q_PROPERTY(QString captureMode READ captureMode CONSTANT)
    // v4l2loopback node used in "v4l2" mode
    Q_PROPERTY(QString videoDevice READ videoDevice NOTIFY videoDeviceChanged)
    // Requested virtual display "WxH" (--new-display, Android 11+); "" = phone screen
    Q_PROPERTY(QString displaySize READ displaySize NOTIFY displaySizeChanged)
    // Android display id of the running mirror (-1 = phone's main display)
    Q_PROPERTY(int displayId READ displayId NOTIFY displayIdChanged)
    // "WxH" of the virtual display actually in use, "" if mirroring the phone screen
    Q_PROPERTY(QString activeDisplaySize READ activeDisplaySize NOTIFY displayIdChanged)

public:
    explicit PhoneMirrorManager(QObject *parent = nullptr);
    ~PhoneMirrorManager() override;

    // Property accessors
    bool isScrcpyInstalled() const;
    QString scrcpyPath() const;
    QString adbPath() const;
    bool isRunning() const;
    int scrcpyWindowHandle() const;
    QString scrcpyVersion() const;
    QString captureMode() const;
    QString videoDevice() const;
    QString displaySize() const;
    int displayId() const;
    QString activeDisplaySize() const;

    // Validate "WxH": dimensions snapped down to multiples of 8 (scrcpy does the
    // same to a --new-display size), "" for invalid input.
    static QString normalizeDisplaySize(const QString &value);

    static constexpr int kNewDisplayMinSdk = 30;  // Android 11

    // Oldest scrcpy whose CLI we drive (2.0 introduced --video-codec,
    // --no-audio, --no-window and the current --v4l2-sink semantics).
    static constexpr int kMinScrcpyMajor = 2;
    static constexpr int kMinScrcpyMinor = 0;

signals:
    void scrcpyPathChanged();
    void error(const QString &message);
    void scrcpyStarted(int windowHandle);
    void scrcpyStopped();
    void scrcpyError(const QString &message);
    void isRunningChanged();
    void videoDeviceChanged();
    void displaySizeChanged();
    void displayIdChanged();

public slots:
    void setScrcpyPath(const QString &path);
    void setAudioEnabled(bool enabled);
    void setVideoDevice(const QString &device);
    void setDisplaySize(const QString &size);
    int getDeviceSdk();
    bool videoDeviceExists();
    void setVolume(float volume);
    bool hasConnectedDevice();
    // "device" | "unauthorized" | "offline" | "none" | "no-adb"
    QString getDeviceState();
    static QString describeDeviceState(const QString &state);
    QString getDeviceSerial();
    QString getDeviceName();
    QString getDeviceResolution();
    QString getInstallInstructions();
    void startScrcpy();
    void stopScrcpy();
    void cleanup();

private:
    // Binary detection
    QString findScrcpy() const;
    QString findAdb() const;
    bool checkScrcpy(const QString &path) const;
    // Returns "X.Y[.Z]" or "" when it runs but prints nothing parseable;
    // sets *ok=false if the binary cannot be executed at all.
    QString probeScrcpyVersion(const QString &path, bool *ok) const;
    bool versionTooOld() const;
    QString getEffectiveScrcpyPath() const;
    QString getBundledToolsDir() const;

    // Window finding (platform-specific)
    void findScrcpyWindow();

    // v4l2 mode readiness / process supervision
    void onProcessStderr();
    void markReady();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

#ifdef Q_OS_WIN
    int findWindowByPid(qint64 pid) const;
#endif

    // ADB helper — runs an adb command synchronously and returns stdout
    QString runAdb(const QStringList &args, int timeoutMs = 10000) const;
    int findScrcpyDisplayId() const;
    void killStaleServer();

    // Members
    QString m_scrcpyPath;
    QString m_customScrcpyPath;
    QString m_adbPath;
    bool m_audioEnabled = false;
    mutable QString m_scrcpyVersion;
    QString m_captureMode;
    QString m_videoDevice;
    QString m_displaySize;
    QString m_activeDisplaySize;
    int m_displayId = -1;

    QProcess *m_process = nullptr;
    int m_scrcpyHwnd = 0;
    bool m_isStarting = false;
    bool m_isStopping = false;
    bool m_ready = false;
    QStringList m_stderrTail;
    QTimer m_readyFallbackTimer;

    QTimer m_windowPollTimer;
    int m_windowPollCount = 0;
};

#else // Q_OS_MOBILE — mobile stub

#include <QObject>
#include <QString>

class PhoneMirrorManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isScrcpyInstalled READ isScrcpyInstalled CONSTANT)
    Q_PROPERTY(QString scrcpyPath READ scrcpyPath CONSTANT)
    Q_PROPERTY(bool isRunning READ isRunning CONSTANT)
    Q_PROPERTY(int scrcpyWindowHandle READ scrcpyWindowHandle CONSTANT)
    Q_PROPERTY(QString scrcpyVersion READ scrcpyVersion CONSTANT)
    Q_PROPERTY(QString captureMode READ captureMode CONSTANT)
    Q_PROPERTY(QString videoDevice READ videoDevice CONSTANT)
    Q_PROPERTY(QString displaySize READ displaySize CONSTANT)
    Q_PROPERTY(int displayId READ displayId CONSTANT)
    Q_PROPERTY(QString activeDisplaySize READ activeDisplaySize CONSTANT)
public:
    explicit PhoneMirrorManager(QObject *parent = nullptr) : QObject(parent) {}
    void cleanup() {}
    bool isScrcpyInstalled() const { return false; }
    QString scrcpyPath() const { return {}; }
    QString scrcpyVersion() const { return {}; }
    QString captureMode() const { return QStringLiteral("unsupported"); }
    QString videoDevice() const { return {}; }
    QString displaySize() const { return {}; }
    int displayId() const { return -1; }
    QString activeDisplaySize() const { return {}; }
    bool isRunning() const { return false; }
    int scrcpyWindowHandle() const { return 0; }

public slots:
    void setScrcpyPath(const QString &) {}
    void setAudioEnabled(bool) {}
    void setVideoDevice(const QString &) {}
    void setDisplaySize(const QString &) {}
    bool videoDeviceExists() const { return false; }
    bool hasConnectedDevice() const { return false; }
    QString getInstallInstructions() const { return QStringLiteral("Phone mirroring is desktop-only"); }
    void startScrcpy() {}
    void stopScrcpy() {}

signals:
    void scrcpyPathChanged();
    void error(const QString &);
    void scrcpyError(const QString &);  // PhoneMirrorView.qml onScrcpyError
    void scrcpyStarted(int);
    void scrcpyStopped();
};

#endif // Q_OS_MOBILE
#endif // PHONEMIRRORMANAGER_H
