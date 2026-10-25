#ifndef AAPSESSION_H
#define AAPSESSION_H

// One Android Auto session from the head unit's side, with no I/O of its
// own: bytes from the phone go in through receive(), bytes for the phone
// come out of transmit(). The transport (the adb head unit server socket
// today, a USB accessory link later) only moves them. Flow and layouts:
// docs/ANDROID_AUTO_PROTOCOL.md.

#include "aapframer.h"
#include "aapproto.h"
#include "aaptls.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <memory>

class AapSession : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Idle,
        Version,        // version request sent
        Handshake,      // TLS in progress
        Discovery,      // authenticated, waiting for the phone's service discovery
        Running,        // discovery answered: channels open, media flows
        Ending,         // bye-bye sent
        Ended,
        Failed
    };
    Q_ENUM(State)

    struct Options {
        Aap::HeadUnitConfig unit;
        QByteArray certPem;
        QByteArray keyPem;          // may be empty when certPem holds both
        int pingIntervalMs = 1500;
        int pingTimeoutMs = 10000;  // no ping answer for this long → failed
    };

    explicit AapSession(const Options &options, QObject *parent = nullptr);
    ~AapSession() override;

    // Loads the certificate and sends the version request. False (and
    // failed() emitted) when the certificate can't be used.
    bool start();
    void receive(const QByteArray &bytes);

    // Polite end: bye-bye, then ended() when the phone answers
    void end();

    // Head unit → phone
    void sendTouch(const QList<Aap::TouchPointer> &pointers, int actionIndex, Aap::TouchAction action);
    void setNightMode(bool night);
    void setDrivingStatus(int status);          // 0 = unrestricted
    void setVideoFocus(bool projected);         // false: the phone stops sending video

    State state() const { return m_state; }
    QString phoneName() const { return m_phoneName; }
    QString lastError() const { return m_error; }

signals:
    void transmit(const QByteArray &bytes);
    void stateChanged(AapSession::State state);
    // Service discovery answered: the phone is projecting
    void running(const QString &phoneName);
    void videoCodecConfig(const QByteArray &spsPps);
    void videoFrame(const QByteArray &annexB, qint64 timestampUs);
    void videoStreaming(bool streaming);
    void audioStreaming(Aap::AudioStream stream, bool streaming);
    void audioData(Aap::AudioStream stream, const QByteArray &pcm, qint64 timestampUs);
    void microphoneRequested(bool open);
    void failed(const QString &reason);
    void ended(const QString &reason);

private:
    void setState(State s);
    void fail(const QString &reason);
    void send(quint8 channel, quint16 id, const QByteArray &body, bool encrypted = true, bool control = false);
    void handleMessage(const Aap::Message &m);
    void handleControl(const Aap::Message &m);
    void handleMediaSink(const Aap::Message &m);
    void handleSensor(const Aap::Message &m);
    void handleInput(const Aap::Message &m);
    void handleMicrophone(const Aap::Message &m);
    void onPingTimer();
    qint64 nowUs() const { return m_clock.nsecsElapsed() / 1000; }

    Options m_opt;
    State m_state = State::Idle;
    std::unique_ptr<AapTls> m_tls;
    Aap::FrameReader m_reader;
    Aap::MessageAssembler m_assembler;
    QTimer m_pingTimer;
    QElapsedTimer m_clock;
    qint64 m_lastPongUs = 0;
    QHash<quint8, int> m_mediaSession;          // channel → session id from START
    QHash<quint8, bool> m_streaming;
    bool m_nightMode = false;
    int m_drivingStatus = 0;
    bool m_sensorNightWanted = false;
    bool m_sensorDrivingWanted = false;
    bool m_videoFocusProjected = true;
    QString m_phoneName;
    QString m_error;
};

#endif // AAPSESSION_H
