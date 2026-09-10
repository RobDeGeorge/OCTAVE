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
#include <QTimer>

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
    // Phone audio is currently being played through OCTAVE
    Q_PROPERTY(bool audioActive READ audioActive NOTIFY audioActiveChanged)
    // The phone is producing sound right now (navigation prompt, video, call)
    Q_PROPERTY(bool audioPlaying READ audioPlaying NOTIFY audioPlayingChanged)
    // The phone was put to sleep (power button / lock) during a session. Its
    // virtual display stops compositing and drops touch; OCTAVE wakes it.
    Q_PROPERTY(bool phoneAsleep READ phoneAsleep NOTIFY phoneAsleepChanged)
    // The user unlocked the phone during a session: OCTAVE leaves its panel
    // alone and does not undo a lock until they lock it again or tap
    // resumeMirroring(). The mirror keeps working while the phone is awake.
    Q_PROPERTY(bool phoneInUse READ phoneInUse NOTIFY phoneInUseChanged)

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
    bool audioActive() const;
    bool audioPlaying() const;
    bool phoneAsleep() const { return m_phoneAsleep; }
    bool phoneInUse() const { return m_phoneInUse; }
    // Factor other sources should currently apply: duck level while the phone
    // is producing sound and ducking is on, 1.0 otherwise
    float duckingFactor() const;

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
    void audioActiveChanged(bool active);
    void audioPlayingChanged(bool playing);
    void phoneAsleepChanged(bool asleep);
    void phoneInUseChanged(bool inUse);
    // Emitted whenever duckingFactor() changes; main.cpp routes it to the
    // other audio sources (MediaManager::setDucking).
    void duckingChanged(float factor);

public slots:
    // Called by VolumeController on every volume change (0..1 linear);
    // phone audio played through OCTAVE follows it.
    void setVolume(float volume);
    // Linear gain on phone PCM before OCTAVE's volume (setting scrcpyAudioGain)
    void setAudioGain(float gain);
    void setAudioEnabled(bool enabled);
    // Duck OCTAVE's other sources while the phone produces sound
    // (settings scrcpyAudioDuckEnabled / scrcpyAudioDuckLevel, linear factor)
    void setAudioDuckEnabled(bool enabled);
    void setAudioDuckLevel(float level);
    void setDisplaySize(const QString &size);
    // Setting scrcpyPhoneScreenOff: keep the phone's own panel dark while
    // mirroring. Stream and touch keep working (SurfaceControl power mode,
    // not sleep); the server restores the panel when the session ends.
    void setPhoneScreenOff(bool off);
    // Wake a sleeping phone (KEYCODE_WAKEUP never toggles it off)
    void wakePhone();
    // Take the phone back from the in-use state: wake if needed, re-apply screen-off
    void resumeMirroring();
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
    void updateDucking();
    void applyScreenOff();
    void startWakeWatch();
    void stopWakeWatch();
    void pollWakefulness();
    void onWakefulness(const QString &state, int locked);
    void setInUse(bool inUse);
    void onGraceExpired();
    void onServerLog(const QString &line);

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
    float m_audioGain = 2.0f;
    bool m_audioPlaying = false;
    bool m_duckEnabled = true;
    float m_duckLevel = 0.1f;        // -20 dB
    float m_duckFactor = 1.0f;       // last value emitted through duckingChanged
    bool m_phoneScreenOff = true;    // setting scrcpyPhoneScreenOff
    bool m_phoneAsleep = false;
    bool m_phoneInUse = false;
    QString m_serial;
    int m_vdisplayId = -1;          // --new-display id, parsed from the server log
    int m_prevLocked = -2;          // deviceLocked at the previous poll; -2 before the first
    bool m_blankOnWake = false;     // the doze ended an in-use spell: user is putting the phone down
    QTimer m_grace;                 // panel left lit after a wake until this fires
    QTimer m_wakePoll;               // asks the phone for mWakefulness while a session is up
    bool m_wakeProbeBusy = false;
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
    Q_PROPERTY(bool audioActive READ audioActive CONSTANT)
    Q_PROPERTY(bool audioPlaying READ audioPlaying CONSTANT)
    Q_PROPERTY(bool phoneAsleep READ phoneAsleep CONSTANT)
    Q_PROPERTY(bool phoneInUse READ phoneInUse CONSTANT)
public:
    explicit PhoneMirrorManager(QObject *parent = nullptr) : QObject(parent) {}
    bool audioActive() const { return false; }
    bool audioPlaying() const { return false; }
    bool phoneAsleep() const { return false; }
    bool phoneInUse() const { return false; }
    float duckingFactor() const { return 1.0f; }
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
    void setAudioGain(float) {}
    void setAudioEnabled(bool) {}
    void setAudioDuckEnabled(bool) {}
    void setAudioDuckLevel(float) {}
    void setDisplaySize(const QString &) {}
    void setPhoneScreenOff(bool) {}
    void wakePhone() {}
    void resumeMirroring() {}
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
    void audioActiveChanged(bool);
    void audioPlayingChanged(bool);
    void duckingChanged(float);
};

#endif // Q_OS_MOBILE
#endif // PHONEMIRRORMANAGER_H
