#ifndef PHONEMIRRORMANAGER_H
#define PHONEMIRRORMANAGER_H

// Phone mirroring via OCTAVE's built-in scrcpy-protocol client
// (src/phone_mirror/scrcpyclient.{h,cpp}): the bundled server jar is pushed
// to the phone over adb and OCTAVE decodes the stream and injects touch
// itself. Nothing has to be installed by the user.

#ifndef Q_OS_MOBILE

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QPair>
#include <QList>
#include <QHash>
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
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "phone")
    Q_CLASSINFO("octave.scope.read", "describeDeviceState environmentOk getDeviceState getInstallInstructions hasConnectedDevice")
    Q_CLASSINFO("octave.scope.full", "cleanup videoSink")

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
    // The phone's media session (phone_server PhoneMedia), reported while a
    // session is up: what is playing on the phone, whatever app plays it
    Q_PROPERTY(bool mediaAvailable READ mediaAvailable NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaArtist READ mediaArtist NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaAlbum READ mediaAlbum NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaApp READ mediaApp NOTIFY mediaChanged)          // package name
    Q_PROPERTY(int mediaDuration READ mediaDuration NOTIFY mediaChanged)    // ms, 0 = unknown
    Q_PROPERTY(bool mediaCanSeek READ mediaCanSeek NOTIFY mediaChanged)
    // file:// URL of the album art (a JPEG in the app data temp dir), "" for none
    Q_PROPERTY(QString mediaArtUrl READ mediaArtUrl NOTIFY mediaArtChanged)
    // The phone is the media source OCTAVE's controls drive: set for the
    // whole mirroring session (from the first media report); a local
    // track starting clears it (releaseMedia) until the phone plays
    // again, and the session ending clears it
    Q_PROPERTY(bool mediaActive READ mediaActive NOTIFY mediaActiveChanged)
    // The phone session's play queue (Spotify: the current track and the
    // next 49): [{id, title, artist, art}] (art: file:// URL of a JPEG in
    // the app data temp dir, "" until it arrives); mediaQueueActiveId is the playing
    // item's id (-1 none). mediaSkipToQueueItem(id) jumps to an item.
    Q_PROPERTY(QVariantList mediaQueue READ mediaQueue NOTIFY mediaQueueChanged)
    Q_PROPERTY(qint64 mediaQueueActiveId READ mediaQueueActiveId NOTIFY mediaQueueChanged)
    Q_PROPERTY(QString mediaQueueTitle READ mediaQueueTitle NOTIFY mediaQueueChanged)
    // Last `adb devices` probe, refreshed off the GUI thread (refreshDeviceState):
    // "device" | "unauthorized" | "offline" | "no-permissions" | "none" | "no-adb"
    Q_PROPERTY(QString deviceState READ deviceState NOTIFY deviceStateChanged)
    // What the last scrcpyError was about (errorKindFor): "noPhone" | "device"
    // (the phone or its link: worth retrying) | "setup" (OCTAVE's side) |
    // "fatal" (the phone cannot do it); "" after a successful start
    Q_PROPERTY(QString lastErrorKind READ lastErrorKind NOTIFY lastErrorKindChanged)
    // Where the session is: "idle" | "starting" | "streaming" | "reconnecting"
    // (a drop after streaming: hold the last frame while the manager retries,
    // up to kReconnectGraceMs) | "waitingForPhone" (a phone / link error: the
    // manager restarts the moment adb reports the phone) | "failed" (setup or
    // fatal: nothing to retry). Retries only run while setViewActive(true).
    Q_PROPERTY(QString sessionState READ sessionState NOTIFY sessionStateChanged)
    // Extra context for a phone that is not usable: an iPhone on USB, another
    // adb server fighting this one. "" when there is nothing to add.
    Q_PROPERTY(QString deviceHint READ deviceHint NOTIFY deviceStateChanged)
    // Phones adb lists: [{serial, state, wireless, model, selected}]
    // (model only for usable phones when more than one is listed)
    Q_PROPERTY(QVariantList devices READ deviceList NOTIFY deviceStateChanged)
    // Something about the running session the user should know but that does
    // not stop it (touch blocked by the phone, no audio before Android 11,
    // software encoder fallback). "" when all is well; cleared on each start.
    Q_PROPERTY(QString mirrorNotice READ mirrorNotice NOTIFY mirrorNoticeChanged)

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
    bool mediaAvailable() const { return m_mediaAvailable; }
    bool mediaPlaying() const { return m_mediaPlaying; }
    QString mediaTitle() const { return m_mediaTitle; }
    QString mediaArtist() const { return m_mediaArtist; }
    QString mediaAlbum() const { return m_mediaAlbum; }
    QString mediaApp() const { return m_mediaApp; }
    int mediaDuration() const { return m_mediaDuration; }
    bool mediaCanSeek() const { return m_mediaCanSeek; }
    QString mediaArtUrl() const { return m_mediaArtUrl; }
    bool mediaActive() const { return m_mediaActive; }
    QVariantList mediaQueue() const { return m_mediaQueue; }
    // GET /art?queue=<id>: the art file of that queue item, only if it is
    // in the queue art folder; "" otherwise
    QString queueArtFile(qint64 id) const;
    // A service stream client joined: ask the phone for a keyframe (GUI
    // thread; nothing without a session)
    void requestVideoKeyframe();
    qint64 mediaQueueActiveId() const { return m_mediaQueueActiveId; }
    QString mediaQueueTitle() const { return m_mediaQueueTitle; }
    QString deviceState() const { return m_deviceState; }
    QString lastErrorKind() const { return m_lastErrorKind; }
    QString sessionState() const { return m_sessionState; }
    static constexpr int kReconnectGraceMs = 10000;   // a USB blip is back well within this
    static constexpr int kReconnectPollMs = 1000;
    static constexpr int kWaitPollMs = 2000;
    // The kind of an error message, decided next to the strings that make it
    static QString errorKindFor(const QString &message);
    static const QString kEncoderGaveUp;
    QString deviceHint() const { return m_deviceHint; }
    QVariantList deviceList() const;
    QString mirrorNotice() const { return m_notices.join(QLatin1Char('\n')); }
    // `adb devices` output -> (serial, state) rows. Skips the header and the
    // "* daemon ..." start-up lines; "no permissions (...)" becomes "no-permissions".
    static QList<QPair<QString, QString>> parseDevices(const QString &output);
    // Rows -> the single state deviceState reports (best usable state wins)
    static QString summarizeState(const QList<QPair<QString, QString>> &rows);
    // Phone to mirror: the preferred serial when it is usable, else USB first
    static QString pickSerial(const QList<QPair<QString, QString>> &rows, const QString &preferred);
    // Classify a phone-server log line: "inject" (touch blocked), "no-audio"
    // (Android < 11), "encoder" (video encoder failed), "" for anything else
    static QString classifyServerLine(const QString &line);
    // Software H.264 encoder used when the phone's hardware encoder fails
    static QString softwareEncoderFor(int sdk);
    // On-screen text for a mirrorNotice kind ("inject", "no-audio", "software-encoder")
    static QString noticeText(const QString &kind);
    // Phone audio capture arrived in Android 11
    static constexpr int kAudioMinSdk = 30;
    // An encoder error counts as the cause of a disconnect this soon after it
    static constexpr int kFatalCauseWindowMs = 5000;
    // "WIFI:T:ADB;S:<service>;P:<password>;;" -- the payload Android's
    // "Pair device with QR code" scanner expects (Android Studio's format)
    static QString qrPairingPayload(const QString &service, const QString &password);
    // "192.168.1.5:5555" -> "192.168.1.5", "[fe80::1]:5555" -> "fe80::1"
    static QString hostOf(const QString &address);
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
    // locations, preferring the first new enough to pair (kAdbWirelessMinTools).
    // Shared with AndroidAutoManager so both find the same binary.
    static QString findAdb();
    // platform-tools major version of an adb binary, 0 if unknown (cached)
    static int adbToolsMajor(const QString &adbPath);
    // `adb pair` / `adb mdns` arrived in platform-tools 30
    static constexpr int kAdbWirelessMinTools = 30;
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
    void scrcpyError(const QString &message);   // lastErrorKind is set first
    void lastErrorKindChanged();
    void sessionStateChanged();
    void isRunningChanged();
    void displaySizeChanged();
    void frameSizeChanged(int width, int height);
    void frameReady();
    void videoSinkChanged();
    void audioActiveChanged(bool active);
    void audioPlayingChanged(bool playing);
    // Phone PCM as it is handed to the speakers (ScrcpyClient::audioPlayed);
    // main.cpp routes it to AudioAnalyzer::feed_live_pcm
    void audioPlayed(const QByteArray &pcm, int bufferedMs);
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
    // Every media state report: play/pause, position, metadata
    void mediaChanged();
    // The track itself changed (title, artist or app), or went away
    void mediaTrackChanged();
    void mediaArtChanged();
    void mediaQueueChanged();
    // main.cpp pauses OCTAVE's own sources when the phone takes over
    void mediaActiveChanged(bool active);
    void deviceStateChanged();
    void mirrorNoticeChanged();

public slots:
    // Called by VolumeController on every volume change (0..1 linear);
    // phone audio, always played through OCTAVE while mirroring, follows it.
    void setVolume(float volume);
    // OCTAVE's mute (VolumeController::mutedChanged) silences phone audio too
    void setMuted(bool muted);
    // Linear gain on phone PCM before OCTAVE's volume (setting scrcpyAudioGain)
    void setAudioGain(float gain);
    // Duck OCTAVE's other sources while the phone produces sound
    // (settings scrcpyAudioDuckEnabled / scrcpyAudioDuckLevel, linear factor)
    void setAudioDuckEnabled(bool enabled);
    void setAudioDuckLevel(float level);
    // The output phone audio plays to (settingsManager.audioOutputDevice;
    // empty = the system default); call again when the outputs change
    void setAudioOutputDevice(const QString &id);
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
    // The cached deviceState; also schedules a refresh. Never blocks.
    QString getDeviceState();
    // Probe `adb devices` on the probe thread; deviceStateChanged follows
    void refreshDeviceState();
    // The mirror page is on screen: the automatic restarts (and their adb
    // probes) run only then
    void setViewActive(bool active);
    static QString describeDeviceState(const QString &state);
    bool hasConnectedDevice();
    // Mirror this phone (from `devices`); restarts a session on another phone
    void selectDevice(const QString &serial);
    // Hide the current mirrorNotice
    void dismissNotice();
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

    // ── phone media (OCTAVE's media controls on the phone's media session) ──
    // Position now in ms, extrapolated from the last report while playing
    int mediaPosition() const;
    void mediaPlayPause();
    void mediaPlay();
    void mediaPause();
    void mediaNext();
    void mediaPrevious();
    void mediaSeek(int positionMs);
    void mediaSkipToQueueItem(qint64 id);
    // OCTAVE's local playback took over: the controls leave the
    // phone; pausePhone also pauses what the phone is playing
    void releaseMedia(bool pausePhone);

private:
    // tests/cpp drives probes, media state and notices directly
    friend class TstPhoneMirror;
    friend class TstNowPlaying;
    QString runAdb(const QStringList &args, int timeoutMs = 10000) const;
    // What a probe found (built on the probe thread, applied on the GUI thread)
    struct Probe {
        QList<QPair<QString, QString>> rows;
        QString state;
        QString hint;
        QHash<QString, QString> models;   // serial -> ro.product.model
    };
    // Blocking: run on m_probePool only
    Probe probeDevices() const;
    void applyProbe(const Probe &probe);
    // Probe-thread part of startScrcpy; the result is handed to continueStart
    struct StartPrep {
        Probe probe;
        QString serial;
        int sdk = 0;
        QString phoneId;   // getprop ro.serialno: the phone, whatever the transport
    };
    // Reattach only to the phone whose session was persisted. Unknown ids
    // (an older adbd, a getprop timeout) keep the old time-only rule.
    static bool samePhone(const QString &a, const QString &b)
    {
        return a.isEmpty() || b.isEmpty() || a == b;
    }
    void continueStart(const StartPrep &prep, int generation);
    void runProbeJob(std::function<void()> job);
    void addNotice(const QString &notice);
    void clearNotices();
    // Output and exit code whatever the result; safe from any thread
    QString runAdbFull(const QStringList &args, int timeoutMs, int *exitCode = nullptr) const;
    // ["-s", serial] for the phone getDeviceSerial() picks, [] when none
    QString describeProblem(const QString &state, const QList<QPair<QString, QString>> &rows) const;
    // Blocking helpers for the wireless worker thread
    struct MdnsService { QString name; QString type; QString address; };
    QList<MdnsService> mdnsServices() const;
    // `avahi-browse` fallback for an adb built without mDNS (distro arm64 adb)
    static QList<MdnsService> avahiMdnsServices();
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
    void killStaleServer();
    void onConnected(int w, int h);
    void onDisconnected(const QString &reason);
    void updateDucking();
    void sendKeeperPolicy();
    void onPhoneState(bool asleep, bool inUse, bool panelDark);
    void resetPhoneState();
    void onServerLog(const QString &line);
    void onMediaState(const QVariantMap &state);
    void onMediaArt(const QByteArray &jpeg);
    void onMediaQueue(const QVariantList &items, qint64 activeId, const QString &title);
    void onMediaQueueArt(const QString &key, const QByteArray &jpeg);
    void publishMediaQueue();
    void resetMedia();
    void setMediaActive(bool active);

    QString m_adbPath;
    float m_volume = 1.0f;
    bool m_muted = false;
    QString m_displaySize;
    QString m_activeDisplaySize;
    int m_viewWidth = 0;
    int m_viewHeight = 0;
    QString m_startedAutoSize;      // "auto" size the running session was started with
    QTimer m_resizeTimer;           // debounces restarts on view resize
    int m_displayDpi = 0;
    int m_bitRateUsbMbps = kDefaultUsbBitRateMbps;
    int m_bitRateWifiMbps = kDefaultWifiBitRateMbps;
    bool m_autoWifi = false;
    bool m_bitRateAuto = true;
    int m_currentBitRate = 0;
    bool m_tcpipChecked = false;    // the auto-Wi-Fi step ran for the session being started
    bool m_staleRetried = false;    // a failed Wi-Fi start already dropped its stale ip:port and retried
    int m_startGeneration = 0;      // bumped by stopScrcpy; a pending start checks it

    ScrcpyClient *m_client = nullptr;
    QObject *m_videoSink = nullptr;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    bool m_isStarting = false;
    bool m_isStopping = false;
    bool m_ready = false;
    float m_audioGain = 2.0f;
    QString m_audioOutputDevice;
    bool m_audioPlaying = false;
    bool m_duckEnabled = true;
    float m_duckLevel = 0.1f;        // -20 dB
    float m_duckFactor = 1.0f;       // last value emitted through duckingChanged
    bool m_phoneScreenOff = false;   // setting scrcpyPhoneScreenOff
    bool m_phoneAsleep = false;
    bool m_phoneInUse = false;
    QString m_serial;
    int m_vdisplayId = -1;          // --new-display id, parsed from the server log
    bool m_panelDark = false;       // as reported by the keeper
    // A session that died from a link drop leaves its server (and the
    // virtual display with the user's apps) alive on the phone for
    // ScrcpyClient::kPersistMs; the next start attaches to it.
    QString m_persistScid;
    QString m_persistPhoneId;   // ro.serialno of the phone that owns m_persistScid
    QString m_sessionPhoneId;   // ro.serialno of the phone being mirrored
    qint64 m_persistUntilMs = 0;
    bool m_attaching = false;

    QString m_deviceState = QStringLiteral("none");
    QString m_lastErrorKind;
    void reportError(const QString &message);
    void setLastErrorKind(const QString &kind);
    QString m_sessionState = QStringLiteral("idle");
    bool m_viewActive = false;
    bool m_hadStream = false;          // streamed since the last start / give-up
    qint64 m_reconnectSinceMs = 0;
    QTimer m_retryTimer;
    void setSessionState(const QString &state);
    void updateRetryTimer();
    void onRetryTick();
    void restartIfPhoneBack();
    QString m_deviceHint;
    QList<QPair<QString, QString>> m_deviceRows;
    QHash<QString, QString> m_deviceModels;
    QString m_preferredSerial;      // selectDevice()
    QThreadPool m_probePool;        // adb devices / start prep, one at a time
    bool m_probeRunning = false;
    bool m_probeAgain = false;      // a refresh was asked for while one ran
    QStringList m_notices;
    QString m_fatalCause;           // classifyServerLine() of the session's fatal error
    int m_sessionSdk = 0;
    QString m_videoEncoder;         // "" = the phone's default (hardware) encoder
    QString m_softwareEncoderSerial;  // phone whose hardware encoder failed this run
    qint64 m_fatalCauseAtMs = 0;

    QString m_wirelessAddress;
    QString m_connectionType;
    QThreadPool m_wirelessPool;
    int m_wirelessJobs = 0;                       // UI-started jobs in flight (GUI thread)
    std::atomic<bool> m_autoConnectRunning{false};
    qint64 m_lastAutoConnectMs = 0;
    QStringList m_qrRows;
    std::atomic<int> m_qrGeneration{0};           // bumped on start/cancel; a stale wait gives up

    bool m_mediaAvailable = false;
    bool m_mediaPlaying = false;
    QString m_mediaTitle;
    QString m_mediaArtist;
    QString m_mediaAlbum;
    QString m_mediaApp;
    int m_mediaDuration = 0;
    bool m_mediaCanSeek = false;
    qint64 m_mediaPositionMs = 0;      // as last reported...
    qint64 m_mediaPositionAtMs = 0;    // ...at this wall-clock time
    QString m_mediaArtUrl;
    int m_mediaArtSeq = 0;             // alternates the art file so QML reloads it
    bool m_mediaActive = false;
    bool m_mediaReported = false;      // the session's first media report arrived
    QVariantList m_mediaQueue;          // as published: items with "art" filled in
    QVariantList m_mediaQueueRaw;       // as reported: items with "art_uri"
    qint64 m_mediaQueueActiveId = -1;
    QString m_mediaQueueTitle;
    QHash<QString, QString> m_queueArt; // art_uri -> file:// URL ("" = unreadable)
    QTimer m_queueArtTimer;             // batches art arrivals into one queue update
};

#else // Q_OS_MOBILE — mobile stub

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

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
    Q_PROPERTY(QString lastErrorKind READ lastErrorKind CONSTANT)
    Q_PROPERTY(QString sessionState READ sessionState CONSTANT)
    Q_PROPERTY(bool phoneInUse READ phoneInUse CONSTANT)
    Q_PROPERTY(QString wirelessAddress READ wirelessAddress CONSTANT)
    Q_PROPERTY(QString connectionType READ connectionType CONSTANT)
    Q_PROPERTY(bool wirelessBusy READ wirelessBusy CONSTANT)
    Q_PROPERTY(QStringList qrPairingRows READ qrPairingRows CONSTANT)
    Q_PROPERTY(bool qrPairingActive READ qrPairingActive CONSTANT)
    Q_PROPERTY(int currentBitRate READ currentBitRate CONSTANT)
    Q_PROPERTY(bool mediaAvailable READ mediaAvailable CONSTANT)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying CONSTANT)
    Q_PROPERTY(QString mediaTitle READ mediaTitle CONSTANT)
    Q_PROPERTY(QString mediaArtist READ mediaArtist CONSTANT)
    Q_PROPERTY(QString mediaAlbum READ mediaAlbum CONSTANT)
    Q_PROPERTY(QString mediaApp READ mediaApp CONSTANT)
    Q_PROPERTY(int mediaDuration READ mediaDuration CONSTANT)
    Q_PROPERTY(bool mediaCanSeek READ mediaCanSeek CONSTANT)
    Q_PROPERTY(QString mediaArtUrl READ mediaArtUrl CONSTANT)
    Q_PROPERTY(bool mediaActive READ mediaActive CONSTANT)
    Q_PROPERTY(QVariantList mediaQueue READ mediaQueue CONSTANT)
    Q_PROPERTY(qint64 mediaQueueActiveId READ mediaQueueActiveId CONSTANT)
    Q_PROPERTY(QString mediaQueueTitle READ mediaQueueTitle CONSTANT)
public:
    explicit PhoneMirrorManager(QObject *parent = nullptr) : QObject(parent) {}
    bool mediaAvailable() const { return false; }
    bool mediaPlaying() const { return false; }
    QString mediaTitle() const { return {}; }
    QString mediaArtist() const { return {}; }
    QString mediaAlbum() const { return {}; }
    QString mediaApp() const { return {}; }
    int mediaDuration() const { return 0; }
    bool mediaCanSeek() const { return false; }
    QString mediaArtUrl() const { return {}; }
    bool mediaActive() const { return false; }
    QString lastErrorKind() const { return {}; }
    QString sessionState() const { return QStringLiteral("idle"); }
    void setViewActive(bool) {}
    QVariantList mediaQueue() const { return {}; }
    QString queueArtFile(qint64) const { return {}; }
    void requestVideoKeyframe() {}
    qint64 mediaQueueActiveId() const { return -1; }
    QString mediaQueueTitle() const { return {}; }
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
    void setMuted(bool) {}
    void setAudioGain(float) {}
    void setAudioDuckEnabled(bool) {}
    void setAudioDuckLevel(float) {}
    void setAudioOutputDevice(const QString &) {}
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
    int mediaPosition() const { return 0; }
    void mediaPlayPause() {}
    void mediaPlay() {}
    void mediaPause() {}
    void mediaNext() {}
    void mediaPrevious() {}
    void mediaSeek(int) {}
    void mediaSkipToQueueItem(qint64) {}
    void releaseMedia(bool) {}

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
    void mediaActiveChanged(bool);
    // Never emitted here; declared so QML Connections on them load quietly
    void mediaChanged();
    void mediaTrackChanged();
    void mediaArtChanged();
    void mediaQueueChanged();
};

#endif // Q_OS_MOBILE
#endif // PHONEMIRRORMANAGER_H
