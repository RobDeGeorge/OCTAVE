#include "aapsession.h"

#include <QLoggingCategory>
#include <QtEndian>

Q_LOGGING_CATEGORY(lcAap, "octave.androidauto.session")

using namespace Aap;

AapSession::AapSession(const Options &options, QObject *parent)
    : QObject(parent), m_opt(options)
{
    m_pingTimer.setInterval(m_opt.pingIntervalMs);
    connect(&m_pingTimer, &QTimer::timeout, this, &AapSession::onPingTimer);
}

AapSession::~AapSession() = default;

void AapSession::setState(State s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged(s);
}

void AapSession::fail(const QString &reason)
{
    if (m_state == State::Failed || m_state == State::Ended)
        return;
    m_error = reason;
    qCWarning(lcAap) << "session failed:" << reason;
    m_pingTimer.stop();
    setState(State::Failed);
    emit failed(reason);
}

bool AapSession::start()
{
    if (m_state != State::Idle)
        return false;
    if (videoResolutionCode(m_opt.unit.width, m_opt.unit.height) == 0) {
        fail(QStringLiteral("Android Auto has no %1x%2 video mode").arg(m_opt.unit.width).arg(m_opt.unit.height));
        return false;
    }
    m_tls = std::make_unique<AapTls>();
    const QByteArray key = m_opt.keyPem.isEmpty() ? m_opt.certPem : m_opt.keyPem;
    if (!m_tls->init(m_opt.certPem, key)) {
        fail(m_tls->error());
        return false;
    }
    m_clock.start();
    setState(State::Version);
    // aasdk announces 1.6; the phone answers with its own (1.7 on Android Auto 17.9)
    send(Channel::Control, Msg::VersionRequest, versionRequest(1, 6), false);
    return true;
}

void AapSession::send(quint8 channel, quint16 id, const QByteArray &body, bool encrypted, bool control)
{
    QByteArray payload = messageBytes(id, body);
    quint8 flags = control ? Flag::Control : 0;
    if (encrypted) {
        QByteArray cipher;
        if (!m_tls || !m_tls->encrypt(payload, cipher)) {
            fail(m_tls ? m_tls->error() : QStringLiteral("not connected"));
            return;
        }
        payload = cipher;
        flags |= Flag::Encrypted;
    }
    emit transmit(encodeFrames(channel, flags, payload));
}

void AapSession::receive(const QByteArray &bytes)
{
    if (m_state == State::Failed || m_state == State::Ended)
        return;
    m_reader.feed(bytes);
    Frame f;
    while (m_reader.next(f)) {
        QByteArray plain = f.payload;
        // Every encrypted frame, in arrival order: one TLS stream across channels
        if (f.flags & Flag::Encrypted) {
            if (!m_tls || !m_tls->isEstablished()) {
                fail(QStringLiteral("encrypted data before the TLS handshake finished"));
                return;
            }
            if (!m_tls->decrypt(f.payload, plain)) {
                fail(m_tls->error());
                return;
            }
        }
        Message m;
        QString error;
        if (m_assembler.add(f.channel, f.flags, plain, m, &error)) {
            handleMessage(m);
            if (m_state == State::Failed || m_state == State::Ended)
                return;
        } else if (!error.isEmpty()) {
            fail(error);
            return;
        }
    }
}

void AapSession::handleMessage(const Message &m)
{
    // A channel-open request arrives on the channel it opens, as a control message
    if (m.id == Msg::ChannelOpenRequest && (m.channel != Channel::Control || (m.flags & Flag::Control))) {
        const quint64 service = ProtoReader::varintField(m.body, 2).value_or(m.channel);
        qCDebug(lcAap) << "channel open" << m.channel << "service" << service;
        send(m.channel, Msg::ChannelOpenResponse, channelOpenResponse(0), true, true);
        return;
    }
    switch (m.channel) {
    case Channel::Control: handleControl(m); break;
    case Channel::Video:
    case Channel::MediaAudio:
    case Channel::GuidanceAudio:
    case Channel::SystemAudio: handleMediaSink(m); break;
    case Channel::Sensor: handleSensor(m); break;
    case Channel::Input: handleInput(m); break;
    case Channel::Microphone: handleMicrophone(m); break;
    case Channel::MediaStatus: handleMediaStatus(m); break;
    default:
        qCDebug(lcAap) << "message" << m.id << "on unannounced channel" << m.channel;
    }
}

void AapSession::handleControl(const Message &m)
{
    switch (m.id) {
    case Msg::VersionResponse: {
        if (m_state != State::Version || m.body.size() < 6) {
            fail(QStringLiteral("unexpected version response"));
            return;
        }
        const quint16 major = qFromBigEndian<quint16>(m.body.constData());
        const quint16 minor = qFromBigEndian<quint16>(m.body.constData() + 2);
        const quint16 status = qFromBigEndian<quint16>(m.body.constData() + 4);
        qCInfo(lcAap) << "phone speaks AAP" << major << "." << minor << "status" << status;
        if (status != 0) {
            fail(QStringLiteral("the phone refused protocol version 1.6 (status %1)").arg(qint16(status)));
            return;
        }
        setState(State::Handshake);
        QByteArray out;
        if (m_tls->handshake(QByteArray(), out) == AapTls::Step::Failed) {
            fail(m_tls->error());
            return;
        }
        if (!out.isEmpty())
            send(Channel::Control, Msg::EncapsulatedSsl, out, false);
        break;
    }
    case Msg::EncapsulatedSsl: {
        if (m_state != State::Handshake) {
            fail(QStringLiteral("TLS data outside the handshake"));
            return;
        }
        QByteArray out;
        const AapTls::Step step = m_tls->handshake(m.body, out);
        if (!out.isEmpty())
            send(Channel::Control, Msg::EncapsulatedSsl, out, false);
        if (step == AapTls::Step::Failed) {
            fail(m_tls->error());
        } else if (step == AapTls::Step::Done) {
            qCInfo(lcAap) << "TLS established," << m_tls->cipherName() << "phone" << m_tls->peerSubject();
            send(Channel::Control, Msg::AuthComplete, authComplete(0), false);
            setState(State::Discovery);
        }
        break;
    }
    case Msg::ServiceDiscoveryRequest: {
        m_phoneName = QString::fromUtf8(ProtoReader::bytesField(m.body, 5).value_or(QByteArray()));   // device_name
        qCInfo(lcAap) << "service discovery from" << m_phoneName;
        send(Channel::Control, Msg::ServiceDiscoveryResponse, serviceDiscoveryResponse(m_opt.unit));
        m_lastPongUs = nowUs();
        m_pingTimer.start();
        setState(State::Running);
        emit running(m_phoneName);
        break;
    }
    case Msg::PingRequest: {
        const qint64 ts = qint64(ProtoReader::varintField(m.body, 1).value_or(0));
        send(Channel::Control, Msg::PingResponse, pingResponse(ts), m.flags & Flag::Encrypted);
        break;
    }
    case Msg::PingResponse:
        m_lastPongUs = nowUs();
        break;
    case Msg::AudioFocusRequest: {
        const int type = int(ProtoReader::varintField(m.body, 1).value_or(Focus::RequestGain));
        qCInfo(lcAap) << "audio focus request" << type;
        if (!(m_opt.externalAudioFocus && type == Focus::RequestGain))
            send(Channel::Control, Msg::AudioFocusNotification, audioFocusNotification(audioFocusStateFor(type)));
        emit audioFocusRequested(type);
        break;
    }
    case Msg::NavFocusRequest:
        send(Channel::Control, Msg::NavFocusNotification, navFocusNotification(2));
        break;
    case Msg::ByeByeRequest: {
        const int reason = int(ProtoReader::varintField(m.body, 1).value_or(0));
        qCInfo(lcAap) << "phone ends the session, reason" << reason;
        send(Channel::Control, Msg::ByeByeResponse, byeByeResponse());
        m_pingTimer.stop();
        setState(State::Ended);
        emit ended(QStringLiteral("the phone ended Android Auto"));
        break;
    }
    case Msg::ByeByeResponse:
        m_pingTimer.stop();
        setState(State::Ended);
        emit ended(QStringLiteral("ended"));
        break;
    case Msg::VoiceSessionNotification:
        break;
    default:
        qCDebug(lcAap) << "control message" << m.id << "ignored," << m.body.size() << "bytes";
    }
}

void AapSession::handleMediaSink(const Message &m)
{
    const bool video = m.channel == Channel::Video;
    const auto stream = audioStreamForChannel(m.channel);
    switch (m.id) {
    case Msg::MediaSetup:
        send(m.channel, Msg::MediaConfig, mediaConfig(1));
        if (video)
            send(m.channel, Msg::VideoFocusNotification, videoFocusNotification(m_videoFocusProjected, false));
        break;
    case Msg::MediaStart:
        m_mediaSession[m.channel] = int(ProtoReader::varintField(m.body, 1).value_or(0));
        m_streaming[m.channel] = true;
        if (video)
            emit videoStreaming(true);
        else if (stream)
            emit audioStreaming(*stream, true);
        break;
    case Msg::MediaStop:
        m_streaming[m.channel] = false;
        if (video)
            emit videoStreaming(false);
        else if (stream)
            emit audioStreaming(*stream, false);
        break;
    case Msg::MediaCodecConfig:
    case Msg::MediaData: {
        // Ack first: with max_unacked = 1 the phone waits for it
        send(m.channel, Msg::MediaAck, mediaAck(m_mediaSession.value(m.channel)));
        if (m.id == Msg::MediaCodecConfig) {
            if (video)
                emit videoCodecConfig(m.body);
            break;
        }
        if (m.body.size() < 8)
            break;
        const qint64 ts = qint64(qFromBigEndian<quint64>(m.body.constData()));
        const QByteArray payload = m.body.mid(8);
        if (video)
            emit videoFrame(payload, ts);
        else if (stream)
            emit audioData(*stream, payload, ts);
        break;
    }
    case Msg::VideoFocusRequest:
        send(m.channel, Msg::VideoFocusNotification, videoFocusNotification(m_videoFocusProjected, false));
        break;
    default:
        qCDebug(lcAap) << "media message" << m.id << "on channel" << m.channel << "ignored";
    }
}

void AapSession::handleSensor(const Message &m)
{
    if (m.id != Msg::SensorRequest) {
        qCDebug(lcAap) << "sensor message" << m.id << "ignored";
        return;
    }
    const int type = int(ProtoReader::varintField(m.body, 1).value_or(0));
    send(Channel::Sensor, Msg::SensorResponse, sensorResponse(0));
    if (type == 13) {           // driving status
        m_sensorDrivingWanted = true;
        send(Channel::Sensor, Msg::SensorBatch, sensorBatchDrivingStatus(m_drivingStatus));
    } else if (type == 10) {    // night mode
        m_sensorNightWanted = true;
        send(Channel::Sensor, Msg::SensorBatch, sensorBatchNightMode(m_nightMode));
    }
}

void AapSession::handleInput(const Message &m)
{
    if (m.id == Msg::KeyBindingRequest)
        send(Channel::Input, Msg::KeyBindingResponse, keyBindingResponse(0));
    else
        qCDebug(lcAap) << "input message" << m.id << "ignored";
}

void AapSession::handleMicrophone(const Message &m)
{
    if (m.id == Msg::MicrophoneRequest) {
        const bool open = ProtoReader::varintField(m.body, 1).value_or(0) != 0;
        send(Channel::Microphone, Msg::MicrophoneResponse, microphoneResponse(0, 0));
        m_micOpen = open;
        emit microphoneRequested(open);
    } else if (m.id == Msg::MediaSetup) {
        send(Channel::Microphone, Msg::MediaConfig, mediaConfig(1));
    } else if (m.id == Msg::MediaAck) {
        // the phone acknowledging microphone audio; no flow control kept
    } else {
        qCDebug(lcAap) << "microphone message" << m.id << "ignored";
    }
}

void AapSession::handleMediaStatus(const Message &m)
{
    switch (m.id) {
    case Msg::PlaybackStatus:
        emit playbackStatus(parsePlaybackStatus(m.body));
        break;
    case Msg::PlaybackMetadata:
        emit playbackMetadata(parsePlaybackMetadata(m.body));
        break;
    default:
        qCDebug(lcAap) << "media status message" << m.id << "ignored," << m.body.size() << "bytes";
    }
}

void AapSession::onPingTimer()
{
    if (m_state != State::Running)
        return;
    if (nowUs() - m_lastPongUs > qint64(m_opt.pingTimeoutMs) * 1000) {
        fail(QStringLiteral("the phone stopped answering"));
        return;
    }
    send(Channel::Control, Msg::PingRequest, pingRequest(nowUs()));
}

void AapSession::end()
{
    if (m_state != State::Running && m_state != State::Discovery)
        return;
    setState(State::Ending);
    send(Channel::Control, Msg::ByeByeRequest, byeByeRequest(1));   // USER_SELECTION
}

void AapSession::sendTouch(const QList<TouchPointer> &pointers, int actionIndex, TouchAction action)
{
    if (m_state != State::Running || pointers.isEmpty())
        return;
    send(Channel::Input, Msg::InputReport, touchInputReport(nowUs() * 1000, pointers, actionIndex, action));
}

void AapSession::sendMicrophoneAudio(const QByteArray &pcm)
{
    if (m_state != State::Running || !m_micOpen || pcm.isEmpty())
        return;
    // Same layout as the phone's media data: 8-byte timestamp (µs), then PCM
    QByteArray data(8, '\0');
    qToBigEndian(quint64(nowUs()), data.data());
    data.append(pcm);
    send(Channel::Microphone, Msg::MediaData, data);
}

void AapSession::answerAudioFocus(int state)
{
    if (m_state != State::Running)
        return;
    qCInfo(lcAap) << "audio focus answer" << state;
    send(Channel::Control, Msg::AudioFocusNotification, audioFocusNotification(state, false));
}

void AapSession::setAudioFocus(int state)
{
    if (m_state != State::Running)
        return;
    qCInfo(lcAap) << "audio focus set" << state;
    send(Channel::Control, Msg::AudioFocusNotification, audioFocusNotification(state, true));
}

void AapSession::sendKey(int keycode)
{
    if (m_state != State::Running)
        return;
    const qint64 ns = nowUs() * 1000;
    send(Channel::Input, Msg::InputReport, keyInputReport(ns, keycode, true));
    send(Channel::Input, Msg::InputReport, keyInputReport(ns + 1000, keycode, false));
}

void AapSession::setNightMode(bool night)
{
    if (night == m_nightMode)
        return;
    m_nightMode = night;
    if (m_state == State::Running && m_sensorNightWanted)
        send(Channel::Sensor, Msg::SensorBatch, sensorBatchNightMode(night));
}

void AapSession::setDrivingStatus(int status)
{
    if (status == m_drivingStatus)
        return;
    m_drivingStatus = status;
    if (m_state == State::Running && m_sensorDrivingWanted)
        send(Channel::Sensor, Msg::SensorBatch, sensorBatchDrivingStatus(status));
}

void AapSession::setVideoFocus(bool projected)
{
    if (projected == m_videoFocusProjected)
        return;
    m_videoFocusProjected = projected;
    if (m_state == State::Running)
        send(Channel::Video, Msg::VideoFocusNotification, videoFocusNotification(projected, true));
}
