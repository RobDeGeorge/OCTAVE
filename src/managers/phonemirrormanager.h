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
#include <QThreadPool>
#include <atomic>
#include <functional>

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
    // Requested virtual display "WxH" (--new-display, Android 11+); "auto" = the
    // view's own pixel size (setViewSize), "" = phone screen
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
    // Wi-Fi (adb over TCP): "ip:port" of the phone last connected wirelessly.
    // OCTAVE reconnects to it on its own (setting scrcpyWirelessAddress).
    Q_PROPERTY(QString wirelessAddress READ wirelessAddress NOTIFY wirelessAddressChanged)
    // "usb" | "wifi" for the running session, "" when idle
    Q_PROPERTY(QString connectionType READ connectionType NOTIFY connectionTypeChanged)
    // A pair / connect / scan started from the UI is in progress
    Q_PROPERTY(bool wirelessBusy READ wirelessBusy NOTIFY wirelessBusyChanged)
    // QR pairing (startQrPairing): the code's modules, one string of '0'/'1'
    // per row (empty when no pairing is waiting), for QML to draw
    Q_PROPERTY(QStringList qrPairingRows READ qrPairingRows NOTIFY qrPairingChanged)
    Q_PROPERTY(bool qrPairingActive READ qrPairingActive NOTIFY qrPairingChanged)
    // Video bit rate of the running session in bps (follows adaptive changes), 0 when idle
    Q_PROPERTY(int currentBitRate READ currentBitRate NOTIFY currentBitRateChanged)

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
    QString wirelessAddress() const { return m_wirelessAddress; }
    QString connectionType() const { return m_connectionType; }
    bool wirelessBusy() const { return m_wirelessJobs > 0; }
    QStringList qrPairingRows() const { return m_qrRows; }
    bool qrPairingActive() const { return !m_qrRows.isEmpty(); }
    int currentBitRate() const { return m_currentBitRate; }
    // "WIFI:T:ADB;S:<service>;P:<password>;;" -- the payload Android's
    // "Pair device with QR code" scanner expects (Android Studio's format)
    static QString qrPairingPayload(const QString &service, const QString &password);
    // How long a shown QR code waits for the phone to scan it
    static constexpr int kQrPairingTimeoutMs = 120000;
    // Factor other sources should currently apply: duck level while the phone
    // is producing sound and ducking is on, 1.0 otherwise
    float duckingFactor() const;

    // Validate "WxH": dimensions snapped down to multiples of 8 (scrcpy does the
    // same to a --new-display size), "auto" kept as is, "" for invalid input.
    static QString normalizeDisplaySize(const QString &value);
    // "auto": the view's pixel size, scaled down to fit kAutoMaxSide (phone
    // encoders reject very large frames) and snapped to multiples of 8.
    // Falls back to kDefaultDisplaySize before the view has reported a size.
    static QString autoDisplaySize(int viewWidth, int viewHeight);
    static constexpr int kAutoMaxSide = 1920;
    // Port a USB phone's adbd is switched to for adb over TCP (setting scrcpyAutoWifi)
    static constexpr int kTcpipPort = 5555;
    static constexpr int kNewDisplayMinSdk = 30;  // Android 11
    // adb discovery: bundled platform-tools, then PATH, then common SDK
    // locations. Shared with AndroidAutoManager so both find the same binary.
    static QString findAdb();
    // Video bit rate defaults, Mbps (settings scrcpyBitRateUsb / scrcpyBitRateWifi)
    static constexpr int kDefaultUsbBitRateMbps = 16;
    static constexpr int kDefaultWifiBitRateMbps = 8;
    // Adaptive quality: floor, and where a Wi-Fi session starts (fraction of
    // the setting, which is the ceiling). USB starts at the ceiling.
    static constexpr int kAdaptiveMinBps = 2000000;
    static constexpr double kAdaptiveWifiStart = 0.6;

signals:
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
    // Wi-Fi: main.cpp saves wirelessAddressChanged to the settings
    void wirelessAddressChanged(const QString &address);
    void connectionTypeChanged();
    void wirelessBusyChanged();
    // Outcome of QR pairing, message ready to show
    void wirelessResult(bool ok, const QString &message);
    void qrPairingChanged();
    void currentBitRateChanged();

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
    // The mirror view's size in device pixels; drives the "auto" display size.
    // A running auto session is restarted (debounced) when it changes.
    void setViewSize(int width, int height);
    // Virtual display density (setting scrcpyDisplayDpi), 0 = scaled from the phone's
    void setDisplayDpi(int dpi);
    // Video bit rate per link in Mbps (settings scrcpyBitRateUsb / scrcpyBitRateWifi)
    void setBitRateUsb(int mbps);
    void setBitRateWifi(int mbps);
    // Setting scrcpyBitRateAuto: adapt the rate to the link, with the
    // per-link settings as the ceiling (AdaptiveBitRate)
    void setBitRateAuto(bool enabled);
    // Setting scrcpyAutoWifi: before a USB session, switch the phone's adbd to
    // TCP port kTcpipPort and remember its Wi-Fi address, so it can be reached
    // without a cable (and without the Wireless debugging toggle) until it reboots.
    void setAutoWifi(bool enabled);
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

    // ── Wi-Fi ──
    // Remembered phone (from settings at startup); tries to reach it at once
    void setWirelessAddress(const QString &address);
    // Disconnect the Wi-Fi phone and stop reconnecting to it
    void forgetWireless();
    // Show a QR code for the phone's "Pair device with QR code" scanner, wait
    // for the phone to advertise it over mDNS, then pair and connect.
    // Outcome via wirelessResult; qrPairingRows clears when it ends.
    void startQrPairing();
    void cancelQrPairing();

private:
    QString runAdb(const QStringList &args, int timeoutMs = 10000) const;
    // Output and exit code whatever the result; safe from any thread
    QString runAdbFull(const QStringList &args, int timeoutMs, int *exitCode = nullptr) const;
    // ["-s", serial] for the phone getDeviceSerial() picks, [] when none
    QStringList serialArgs();
    QString describeProblem(const QString &state);
    // Blocking helpers for the wireless worker thread
    struct MdnsService { QString name; QString type; QString address; };
    QList<MdnsService> mdnsServices() const;
    QString mdnsConnectAddressFor(const QString &host) const;
    bool adbConnect(const QString &address, QString *message) const;
    // After a successful adb pair: find the phone's connect port over mDNS
    // (polled up to 8 s) and connect. Returns the address reached, "" if none.
    QString connectPairedHost(const QString &host) const;
    void finishQrPairing();
    // Runs on m_wirelessPool (one job at a time); ui jobs drive wirelessBusy
    void runWirelessJob(std::function<void()> job, bool ui);
    void maybeReconnectWireless();
    // Worker thread: the phone's Wi-Fi IPv4 address ("" when it has none)
    QString phoneWifiAddress(const QString &serial) const;
    // Worker thread: switch a USB phone to adb over TCP; returns "ip:port", "" if not possible
    QString enableTcpip(const QString &serial) const;
    // Size actually requested from the server: "WxH[/dpi]", "" = phone screen
    QString requestedDisplay() const;
    void restartSession();
    void setConnectionType(const QString &type);
    QList<QPair<QString, QString>> devices() const;  // (serial, state)
    void killStaleServer();
    void onConnected(int w, int h);
    void onDisconnected(const QString &reason);
    void updateDucking();
    void sendKeeperPolicy();
    void onPhoneState(bool asleep, bool inUse, bool panelDark);
    void resetPhoneState();
    void onServerLog(const QString &line);

    QString m_adbPath;
    bool m_audioEnabled = false;
    float m_volume = 1.0f;
    QString m_displaySize;
    QString m_activeDisplaySize;
    int m_viewWidth = 0;
    int m_viewHeight = 0;
    QString m_startedAutoSize;      // "auto" size the running session was started with
    QTimer m_resizeTimer;           // debounces restarts on view resize
    int m_displayDpi = 0;
    int m_bitRateUsbMbps = kDefaultUsbBitRateMbps;
    int m_bitRateWifiMbps = kDefaultWifiBitRateMbps;
    bool m_autoWifi = true;
    bool m_bitRateAuto = true;
    int m_currentBitRate = 0;
    bool m_tcpipChecked = false;    // the auto-Wi-Fi step ran for the session being started
    int m_startGeneration = 0;      // bumped by stopScrcpy; a pending start checks it

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
    bool m_panelDark = false;       // as reported by the keeper
    // A session that died from a link drop leaves its server (and the
    // virtual display with the user's apps) alive on the phone for
    // ScrcpyClient::kPersistMs; the next start attaches to it.
    QString m_persistScid;
    qint64 m_persistUntilMs = 0;
    bool m_attaching = false;

    QString m_wirelessAddress;
    QString m_connectionType;
    QThreadPool m_wirelessPool;
    int m_wirelessJobs = 0;                       // UI-started jobs in flight (GUI thread)
    std::atomic<bool> m_autoConnectRunning{false};
    qint64 m_lastAutoConnectMs = 0;
    QStringList m_qrRows;
    std::atomic<int> m_qrGeneration{0};           // bumped on start/cancel; a stale wait gives up
};

#else // Q_OS_MOBILE — mobile stub

#include <QObject>
#include <QString>
#include <QStringList>

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
    Q_PROPERTY(QString wirelessAddress READ wirelessAddress CONSTANT)
    Q_PROPERTY(QString connectionType READ connectionType CONSTANT)
    Q_PROPERTY(bool wirelessBusy READ wirelessBusy CONSTANT)
    Q_PROPERTY(QStringList qrPairingRows READ qrPairingRows CONSTANT)
    Q_PROPERTY(bool qrPairingActive READ qrPairingActive CONSTANT)
    Q_PROPERTY(int currentBitRate READ currentBitRate CONSTANT)
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
    QString wirelessAddress() const { return {}; }
    QString connectionType() const { return {}; }
    bool wirelessBusy() const { return false; }
    QStringList qrPairingRows() const { return {}; }
    bool qrPairingActive() const { return false; }
    int currentBitRate() const { return 0; }

public slots:
    void setVolume(float) {}
    void setAudioGain(float) {}
    void setAudioEnabled(bool) {}
    void setAudioDuckEnabled(bool) {}
    void setAudioDuckLevel(float) {}
    void setDisplaySize(const QString &) {}
    void setViewSize(int, int) {}
    void setDisplayDpi(int) {}
    void setBitRateUsb(int) {}
    void setBitRateWifi(int) {}
    void setBitRateAuto(bool) {}
    void setAutoWifi(bool) {}
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
    void setWirelessAddress(const QString &) {}
    void forgetWireless() {}
    void startQrPairing() {}
    void cancelQrPairing() {}

signals:
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
    void wirelessAddressChanged(const QString &);
    void wirelessResult(bool, const QString &);
};

#endif // Q_OS_MOBILE
#endif // PHONEMIRRORMANAGER_H
