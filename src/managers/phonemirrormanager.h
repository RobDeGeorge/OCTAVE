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

public:
    explicit PhoneMirrorManager(QObject *parent = nullptr);
    ~PhoneMirrorManager() override;

    // Property accessors
    bool isScrcpyInstalled() const;
    QString scrcpyPath() const;
    QString adbPath() const;
    bool isRunning() const;
    int scrcpyWindowHandle() const;

signals:
    void scrcpyPathChanged();
    void error(const QString &message);
    void scrcpyStarted(int windowHandle);
    void scrcpyStopped();
    void scrcpyError(const QString &message);
    void isRunningChanged();

public slots:
    void setScrcpyPath(const QString &path);
    void setAudioEnabled(bool enabled);
    void setVolume(float volume);
    bool hasConnectedDevice();
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
    QString getEffectiveScrcpyPath() const;
    QString getBundledToolsDir() const;

    // Window finding (platform-specific)
    void findScrcpyWindow();

#ifdef Q_OS_WIN
    int findWindowByPid(qint64 pid) const;
#endif

    // ADB helper — runs an adb command synchronously and returns stdout
    QString runAdb(const QStringList &args, int timeoutMs = 10000) const;

    // Members
    QString m_scrcpyPath;
    QString m_customScrcpyPath;
    QString m_adbPath;
    bool m_audioEnabled = false;

    QProcess *m_process = nullptr;
    int m_scrcpyHwnd = 0;
    bool m_isStarting = false;

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
public:
    explicit PhoneMirrorManager(QObject *parent = nullptr) : QObject(parent) {}
    void cleanup() {}
    bool isScrcpyInstalled() const { return false; }
    QString scrcpyPath() const { return {}; }
    bool isRunning() const { return false; }
    int scrcpyWindowHandle() const { return 0; }

public slots:
    void setScrcpyPath(const QString &) {}
    void setAudioEnabled(bool) {}
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
