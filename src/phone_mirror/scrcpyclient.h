#ifndef SCRCPYCLIENT_H
#define SCRCPYCLIENT_H

#ifndef Q_OS_MOBILE

// Built-in scrcpy-protocol client for OCTAVE.
//
// Replaces the scrcpy desktop binary (and, on Linux, v4l2loopback + ffmpeg):
// OCTAVE pushes the bundled scrcpy *server* jar to the phone over adb, starts
// it, and talks the scrcpy protocol itself — an H.264 elementary stream on
// one socket, control messages (touch, keys) on another. Frames are decoded
// with libavcodec and handed to a QVideoSink for QML's VideoOutput.
//
// Protocol details (server 3.x) are documented in
// docs/PHONE_MIRROR_NATIVE_PLAN.md and were verified against scrcpy v3.3.4.
// Mirrors backend/phone_mirror/scrcpy_client.py.

#include <QObject>
#include <QString>
#include <QVideoFrame>
#include <QLoggingCategory>
#include <QByteArray>
#include <QList>

#include <atomic>
#include <mutex>
#include <thread>

Q_DECLARE_LOGGING_CATEGORY(lcScrcpyClient)

class QVideoSink;
class QTcpSocket;
class QProcess;
class QAudioSink;
class QTimer;
class QIODevice;

class ScrcpyClient : public QObject
{
    Q_OBJECT
public:
    // OCTAVE's fork of the scrcpy server (phone_server/, org.octave.phoneserver);
    // the version string must match the jar's BuildConfig exactly.
    static constexpr const char *kServerVersion = "3.3.4-octave";
    static constexpr const char *kServerClass = "org.octave.phoneserver.Server";
    static constexpr const char *kServerProcessPattern = "org.octave.phoneserver";

    // Android MotionEvent / KeyEvent actions
    enum Action { ActionDown = 0, ActionUp = 1, ActionMove = 2 };
    // Android keycodes we expose
    enum Keycode { KeycodeHome = 3, KeycodeBack = 4, KeycodeAppSwitch = 187, KeycodePower = 26 };

    // True when this build can decode (libavcodec linked) and the bundled
    // server resource is present.
    static bool available();
    // Writes the embedded server jar to a temp file; returns its path ("" on failure).
    static QString bundledServerJar();

    explicit ScrcpyClient(const QString &adbPath, const QString &serverJar, QObject *parent = nullptr);
    ~ScrcpyClient() override;

    bool isRunning() const { return m_running.load(); }
    int frameWidth() const { return m_width.load(); }
    int frameHeight() const { return m_height.load(); }
    quint64 frameCount() const { return m_frameCount.load(); }

    void setVideoSink(QObject *sink);
    // 0..1 linear, as produced by VolumeController; phone audio follows it
    void setVolume(float linear);
    // Linear gain applied to phone PCM before the sink (soft-limited)
    void setAudioGain(float gain) { m_audioGain = qBound(0.25f, gain, 8.0f); }
    bool audioActive() const { return m_audioActive.load(); }
    // The phone is producing sound right now (level above kAudioSignalThreshold
    // within the last kAudioHoldMs); drives ducking of OCTAVE's other sources
    bool audioPlaying() const { return m_audioPlaying; }

    bool start(const QString &serial, const QString &displaySize = QString(),
               int maxFps = 60, int bitRate = 8000000, bool audio = false, bool stayAwake = true);
    void stop();

    // Touch on the mirrored display, x/y in frame pixels
    void injectTouch(qint64 pointerId, int action, int x, int y, float pressure = 1.0f);
    void injectKey(int keycode, int action = ActionDown, int meta = 0);
    void pressKey(int keycode);
    void setDisplayPower(bool on);

signals:
    void connected(int width, int height);
    void disconnected(const QString &reason);   // "" for a requested stop
    void frameSizeChanged(int width, int height);
    void frameReady();                          // a decoded frame is waiting (GUI thread pulls it)
    void audioReady();                          // PCM chunks are queued (GUI thread writes them)
    void audioStateChanged(bool active);        // phone audio is (not) being played through OCTAVE
    void audioPlayingChanged(bool playing);     // the phone is (not) producing sound (GUI thread)
    void serverLog(const QString &line);

private slots:
    void deliverFrame();
    void deliverAudio();

private:
    void session(QString displaySize, int maxFps, int bitRate, bool audio, bool stayAwake);
    bool adbRun(const QStringList &args, QString *output, int timeoutMs = 15000);
    QTcpSocket *connectUntilReady(qint64 deadlineMs);
    void videoLoop(QTcpSocket *video, qint64 t0);
    void audioLoop(QTcpSocket *audio);
    bool ensureAudioSink();
    void stopAudioSink();
    void setAudioPlaying(bool playing);
    void pushFrame(const QVideoFrame &frame);
    void fail(const QString &reason);
    void sendControl(const QByteArray &msg);
    static int freePort();

    QString m_adb;
    QString m_jar;
    QString m_serial;
    QString m_scid;
    int m_port = 0;

    std::thread m_thread;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_running{false};
    std::atomic<int> m_width{0};
    std::atomic<int> m_height{0};
    std::atomic<quint64> m_frameCount{0};

    // Owned by the worker thread; the control descriptor is written from any
    // thread with ::send() under m_sendMutex.
    QProcess *m_proc = nullptr;
    QTcpSocket *m_video = nullptr;
    QTcpSocket *m_audio = nullptr;
    QTcpSocket *m_control = nullptr;
    std::atomic<qintptr> m_controlFd{-1};
    std::mutex m_sendMutex;

    QVideoSink *m_sink = nullptr;
    QVideoFrame m_latestFrame;
    std::mutex m_frameMutex;

    // Audio: PCM s16le 48 kHz stereo from the audio socket, played through
    // QAudioSink on the GUI thread (QAudioSink is not thread-safe).
    std::thread m_audioThread;
    std::atomic<bool> m_audioActive{false};
    std::atomic<float> m_volume{1.0f};
    std::atomic<float> m_audioGain{2.0f};
    // Sound detection for ducking: the audio thread flags a chunk with level,
    // the GUI thread turns that into audioPlaying with a release hold.
    std::atomic<bool> m_audioSignalSeen{false};
    bool m_audioPlaying = false;
    QTimer *m_audioHoldTimer = nullptr;
    QList<QByteArray> m_audioQueue;
    qsizetype m_audioQueueBytes = 0;
    std::mutex m_audioMutex;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioIo = nullptr;
};

#endif // Q_OS_MOBILE
#endif // SCRCPYCLIENT_H
