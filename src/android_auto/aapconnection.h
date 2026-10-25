#ifndef AAPCONNECTION_H
#define AAPCONNECTION_H

// One Android Auto session over a TCP socket (the adb-forwarded head unit
// server): the socket, the AapSession and the H.264 decoder live on a worker
// thread; decoded frames, audio and state come back to the GUI thread. The
// caller sets up the adb forward and hands over the local port.

#include "aapproto.h"
#include "aapsession.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVideoFrame>

#include <atomic>
#include <mutex>

class QAudioSink;
class QIODevice;
class QVideoSink;
class AapWorker;

class AapConnection : public QObject
{
    Q_OBJECT
public:
    explicit AapConnection(QObject *parent = nullptr);
    ~AapConnection() override;

    // Connects to host:port and runs a session; one at a time
    bool start(const QString &host, quint16 port, const AapSession::Options &options);
    // Bye-bye, then close (finished() follows); immediate when not running
    void stop();
    bool isActive() const { return m_worker != nullptr; }

    void setVideoSink(QVideoSink *sink);
    // Touch in video pixels (the HeadUnitConfig size)
    void sendTouch(const QList<Aap::TouchPointer> &pointers, int actionIndex, Aap::TouchAction action);
    void setNightMode(bool night);
    void setVideoFocus(bool projected);

    // Audio goes to this output (AudioOutput::preferred) at this volume
    void setVolume(float linear);
    void setAudioOutputDevice(const QString &id);

    // Frames decoded so far in this session (tests, diagnostics)
    int frameCount() const { return m_frameCount.load(); }

signals:
    void stateChanged(AapSession::State state);
    void running(const QString &phoneName);
    void videoStreaming(bool streaming);
    void audioStreaming(Aap::AudioStream stream, bool streaming);
    void frameSizeChanged(int width, int height);
    void failed(const QString &reason);
    void ended(const QString &reason);
    // The session is over and the worker gone; start() may be called again
    void finished();

private:
    friend class AapWorker;
    void onFrame(const QVideoFrame &frame);     // worker thread
    void deliverFrame();                        // GUI thread
    void onAudio(Aap::AudioStream stream, const QByteArray &pcm);
    void onAudioStreaming(Aap::AudioStream stream, bool streaming);
    void drainAudio();
    void closeAudio();
    void onWorkerDone();

    struct AudioOut {
        QAudioSink *sink = nullptr;
        QIODevice *io = nullptr;
        QQueue<QByteArray> queue;
        qsizetype queued = 0;
        QAudioFormat format;
    };
    bool ensureSink(Aap::AudioStream stream, AudioOut &out);

    QThread m_thread;
    AapWorker *m_worker = nullptr;
    QPointer<QVideoSink> m_sink;
    std::mutex m_frameMutex;
    QVideoFrame m_latestFrame;
    std::atomic<bool> m_framePending{false};
    std::atomic<int> m_frameCount{0};
    QSize m_frameSize;
    QHash<int, AudioOut> m_audio;               // by Aap::AudioStream
    QTimer m_audioTimer;
    float m_volume = 1.0f;
    QString m_audioDeviceId;
};

#endif // AAPCONNECTION_H
