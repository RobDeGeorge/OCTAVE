#include "aapconnection.h"

#include "../util/audiooutput.h"
#include "../util/h264decoder.h"

#include <QAudioSink>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QVideoSink>

#ifdef OCTAVE_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

Q_LOGGING_CATEGORY(lcAapConn, "octave.androidauto.connection")

namespace {
constexpr int kStopGraceMs = 2000;          // bye-bye answer before the socket is cut
constexpr int kAudioQueueMaxMs = 500;       // older audio is dropped beyond this
constexpr int kAudioDeviceBufferMs = 200;
}

// ─── worker (its own thread) ─────────────────────────────────────────

class AapWorker : public QObject
{
public:
    AapWorker(AapConnection *owner, const QString &host, quint16 port, const AapSession::Options &opt)
        : m_owner(owner), m_host(host), m_port(port), m_opt(opt), m_closeTimer(this) {}

    void run()
    {
        m_socket = new QTcpSocket(this);
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_session = new AapSession(m_opt, this);
        m_closeTimer.setSingleShot(true);
        connect(&m_closeTimer, &QTimer::timeout, this, [this] { finish(); });

        connect(m_session, &AapSession::transmit, this, [this](const QByteArray &b) {
            if (m_socket->state() == QAbstractSocket::ConnectedState)
                m_socket->write(b);
        });
        connect(m_socket, &QTcpSocket::connected, this, [this] {
            qCInfo(lcAapConn) << "connected to the head unit server at" << m_host << m_port;
            m_session->start();
        });
        connect(m_socket, &QTcpSocket::readyRead, this, [this] {
            m_session->receive(m_socket->readAll());
        });
        connect(m_socket, &QTcpSocket::disconnected, this, [this] {
            if (!m_done && m_session->state() != AapSession::State::Ended
                && m_session->state() != AapSession::State::Failed)
                post([o = m_owner] { emit o->failed(QStringLiteral("the phone closed the connection")); });
            finish();
        });
        connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
            if (e == QAbstractSocket::RemoteHostClosedError)
                return;   // disconnected() reports it
            const QString why = QStringLiteral("could not reach the head unit server: %1").arg(m_socket->errorString());
            post([o = m_owner, why] { emit o->failed(why); });
            finish();
        });

        connect(m_session, &AapSession::stateChanged, this, [this](AapSession::State s) {
            post([o = m_owner, s] { emit o->stateChanged(s); });
        });
        connect(m_session, &AapSession::running, this, [this](const QString &name) {
            post([o = m_owner, name] { emit o->running(name); });
        });
        connect(m_session, &AapSession::failed, this, [this](const QString &why) {
            post([o = m_owner, why] { emit o->failed(why); });
            finish();
        });
        connect(m_session, &AapSession::ended, this, [this](const QString &why) {
            post([o = m_owner, why] { emit o->ended(why); });
            m_socket->flush();
            finish();
        });
        connect(m_session, &AapSession::videoStreaming, this, [this](bool on) {
            post([o = m_owner, on] { emit o->videoStreaming(on); });
        });
        connect(m_session, &AapSession::audioStreaming, this, [this](Aap::AudioStream s, bool on) {
            post([o = m_owner, s, on] { o->onAudioStreaming(s, on); });
        });
        connect(m_session, &AapSession::audioData, this, [this](Aap::AudioStream s, const QByteArray &pcm, qint64) {
            post([o = m_owner, s, pcm] { o->onAudio(s, pcm); });
        });

        if (m_decoder.open()) {
            connect(m_session, &AapSession::videoCodecConfig, this, [this](const QByteArray &c) {
                m_decoder.setConfig(c);
            });
            connect(m_session, &AapSession::videoFrame, this, [this](const QByteArray &h264, qint64) {
#ifdef OCTAVE_HAVE_FFMPEG
                m_decoder.decode(h264, [this](const AVFrame *f) {
                    const QVideoFrame vf = H264Decoder::toVideoFrame(f);
                    if (vf.isValid())
                        m_owner->onFrame(vf);
                });
#else
                Q_UNUSED(h264)
#endif
            });
        } else {
            qCWarning(lcAapConn) << "no H.264 decoder: Android Auto video will not show";
        }

        m_socket->connectToHost(m_host, m_port);
    }

    void stop()
    {
        if (m_done)
            return;
        const auto s = m_session ? m_session->state() : AapSession::State::Idle;
        if (s == AapSession::State::Running || s == AapSession::State::Discovery) {
            m_session->end();
            m_closeTimer.start(kStopGraceMs);
        } else {
            finish();
        }
    }

    AapSession *session() const { return m_session; }

private:
    template <typename F>
    void post(F &&f) { QMetaObject::invokeMethod(m_owner, std::forward<F>(f), Qt::QueuedConnection); }

    void finish()
    {
        if (m_done)
            return;
        m_done = true;
        m_closeTimer.stop();
        if (m_socket) {
            m_socket->disconnect(this);
            m_socket->abort();
        }
        m_decoder.close();
        post([o = m_owner] { o->onWorkerDone(); });
    }

    AapConnection *m_owner;
    QString m_host;
    quint16 m_port;
    AapSession::Options m_opt;
    QTcpSocket *m_socket = nullptr;
    AapSession *m_session = nullptr;
    H264Decoder m_decoder;
    QTimer m_closeTimer;
    bool m_done = false;
};

// ─── AapConnection (GUI thread) ──────────────────────────────────────

AapConnection::AapConnection(QObject *parent)
    : QObject(parent)
{
    m_thread.setObjectName(QStringLiteral("AndroidAuto"));
    m_audioTimer.setInterval(10);
    connect(&m_audioTimer, &QTimer::timeout, this, &AapConnection::drainAudio);
}

AapConnection::~AapConnection()
{
    if (m_worker) {
        AapWorker *w = m_worker;
        m_worker = nullptr;
        QMetaObject::invokeMethod(w, [w] { delete w; }, Qt::BlockingQueuedConnection);
    }
    m_thread.quit();
    m_thread.wait();
    closeAudio();
}

bool AapConnection::start(const QString &host, quint16 port, const AapSession::Options &options)
{
    if (m_worker)
        return false;
    if (!m_thread.isRunning())
        m_thread.start();
    m_frameCount = 0;
    m_frameSize = QSize();
    m_worker = new AapWorker(this, host, port, options);
    m_worker->moveToThread(&m_thread);
    AapWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w] { w->run(); }, Qt::QueuedConnection);
    return true;
}

void AapConnection::stop()
{
    if (!m_worker)
        return;
    AapWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w] { w->stop(); }, Qt::QueuedConnection);
}

void AapConnection::onWorkerDone()
{
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    closeAudio();
    emit finished();
}

void AapConnection::setVideoSink(QVideoSink *sink)
{
    m_sink = sink;
}

void AapConnection::sendTouch(const QList<Aap::TouchPointer> &pointers, int actionIndex, Aap::TouchAction action)
{
    if (!m_worker)
        return;
    AapWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, pointers, actionIndex, action] {
        if (w->session())
            w->session()->sendTouch(pointers, actionIndex, action);
    }, Qt::QueuedConnection);
}

void AapConnection::setNightMode(bool night)
{
    if (!m_worker)
        return;
    AapWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, night] {
        if (w->session())
            w->session()->setNightMode(night);
    }, Qt::QueuedConnection);
}

void AapConnection::setVideoFocus(bool projected)
{
    if (!m_worker)
        return;
    AapWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, projected] {
        if (w->session())
            w->session()->setVideoFocus(projected);
    }, Qt::QueuedConnection);
}

// ─── video ───────────────────────────────────────────────────────────

void AapConnection::onFrame(const QVideoFrame &frame)
{
    // Worker thread: keep only the newest frame; the GUI thread takes it
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_latestFrame = frame;
    }
    ++m_frameCount;
    if (!m_framePending.exchange(true))
        QMetaObject::invokeMethod(this, &AapConnection::deliverFrame, Qt::QueuedConnection);
}

void AapConnection::deliverFrame()
{
    QVideoFrame frame;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        frame = m_latestFrame;
        m_latestFrame = QVideoFrame();
        m_framePending = false;
    }
    if (!frame.isValid())
        return;
    if (frame.size() != m_frameSize) {
        m_frameSize = frame.size();
        emit frameSizeChanged(m_frameSize.width(), m_frameSize.height());
    }
    if (m_sink)
        m_sink->setVideoFrame(frame);
}

// ─── audio ───────────────────────────────────────────────────────────

bool AapConnection::ensureSink(Aap::AudioStream stream, AudioOut &out)
{
    if (out.sink)
        return out.io != nullptr;
    const Aap::AudioFormat f = Aap::audioFormat(stream);
    out.format.setSampleRate(f.sampleRate);
    out.format.setChannelCount(f.channels);
    out.format.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = AudioOutput::preferred(m_audioDeviceId);
    if (device.isNull()) {
        qCWarning(lcAapConn) << "no audio output device; Android Auto audio will not play";
        return false;
    }
    out.sink = new QAudioSink(device, out.format, this);
    out.sink->setBufferSize(out.format.bytesForDuration(kAudioDeviceBufferMs * 1000));
    out.sink->setVolume(m_volume);
    out.io = out.sink->start();
    if (!out.io) {
        qCWarning(lcAapConn) << "could not start audio output" << device.description()
                             << f.sampleRate << "Hz" << f.channels << "ch:" << out.sink->error();
        return false;
    }
    return true;
}

void AapConnection::onAudio(Aap::AudioStream stream, const QByteArray &pcm)
{
    AudioOut &out = m_audio[int(stream)];
    if (!ensureSink(stream, out))
        return;
    out.queue.enqueue(pcm);
    out.queued += pcm.size();
    const qsizetype max = out.format.bytesForDuration(qint64(kAudioQueueMaxMs) * 1000);
    while (out.queued > max && out.queue.size() > 1)
        out.queued -= out.queue.dequeue().size();
    drainAudio();
    if (!m_audioTimer.isActive())
        m_audioTimer.start();
}

void AapConnection::drainAudio()
{
    bool pending = false;
    for (AudioOut &out : m_audio) {
        if (!out.io)
            continue;
        while (!out.queue.isEmpty()) {
            QByteArray &head = out.queue.head();
            const qsizetype free = out.sink->bytesFree();
            if (free <= 0)
                break;
            const qint64 n = out.io->write(head.constData(), qMin(free, head.size()));
            if (n <= 0)
                break;
            out.queued -= n;
            if (n == head.size())
                out.queue.dequeue();
            else
                head.remove(0, n);
        }
        pending = pending || !out.queue.isEmpty();
    }
    if (!pending)
        m_audioTimer.stop();
}

void AapConnection::onAudioStreaming(Aap::AudioStream stream, bool streaming)
{
    emit audioStreaming(stream, streaming);
    if (streaming)
        return;
    // Stream paused: let what is buffered play out, then free the device
    auto it = m_audio.find(int(stream));
    if (it != m_audio.end() && it->queue.isEmpty() && it->sink) {
        it->sink->stop();
        it->sink->deleteLater();
        m_audio.erase(it);
    }
}

void AapConnection::closeAudio()
{
    m_audioTimer.stop();
    for (AudioOut &out : m_audio) {
        if (out.sink) {
            out.sink->stop();
            out.sink->deleteLater();
        }
    }
    m_audio.clear();
}

void AapConnection::setVolume(float linear)
{
    m_volume = linear;
    for (AudioOut &out : m_audio) {
        if (out.sink)
            out.sink->setVolume(linear);
    }
}

void AapConnection::setAudioOutputDevice(const QString &id)
{
    if (id == m_audioDeviceId)
        return;
    m_audioDeviceId = id;
    // Each stream reopens on the new output with its next chunk
    closeAudio();
}
