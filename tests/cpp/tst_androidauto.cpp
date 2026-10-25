// The native Android Auto receiver's protocol core: protobuf wire format,
// framing, and a whole session against a fake phone (an OpenSSL TLS server
// over memory BIOs, with throwaway self-signed certificates), so no device
// and no Google certificate are needed. Layouts: docs/ANDROID_AUTO_PROTOCOL.md.

#include "android_auto/aapframer.h"
#include "android_auto/aapproto.h"
#include "android_auto/aapsession.h"

#include <QSignalSpy>
#include <QtEndian>
#include <QtTest>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

using namespace Aap;

namespace {

struct Pem {
    QByteArray cert;
    QByteArray key;
};

QByteArray bioBytes(BIO *b)
{
    BUF_MEM *mem = nullptr;
    BIO_get_mem_ptr(b, &mem);
    return QByteArray(mem->data, int(mem->length));
}

Pem makeSelfSigned(const char *cn)
{
    EVP_PKEY *key = EVP_RSA_gen(2048);
    X509 *x = X509_new();
    X509_set_version(x, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_getm_notBefore(x), -3600);
    X509_gmtime_adj(X509_getm_notAfter(x), 3600);
    X509_set_pubkey(x, key);
    X509_NAME *name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>(cn), -1, -1, 0);
    X509_set_issuer_name(x, name);
    X509_sign(x, key, EVP_sha256());
    BIO *cb = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(cb, x);
    BIO *kb = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(kb, key, nullptr, nullptr, 0, nullptr, nullptr);
    Pem out{bioBytes(cb), bioBytes(kb)};
    BIO_free(cb);
    BIO_free(kb);
    X509_free(x);
    EVP_PKEY_free(key);
    return out;
}

// The phone's side: answers the head unit like Android Auto 17.9 did in
// the spike (dev/aa-spike/aa_video.py), and records what it was sent.
class FakePhone
{
public:
    explicit FakePhone(const Pem &pem)
    {
        m_ctx = SSL_CTX_new(TLS_server_method());
        SSL_CTX_set_security_level(m_ctx, 0);
        BIO *cb = BIO_new_mem_buf(pem.cert.constData(), int(pem.cert.size()));
        X509 *cert = PEM_read_bio_X509(cb, nullptr, nullptr, nullptr);
        BIO_free(cb);
        BIO *kb = BIO_new_mem_buf(pem.key.constData(), int(pem.key.size()));
        EVP_PKEY *key = PEM_read_bio_PrivateKey(kb, nullptr, nullptr, nullptr);
        BIO_free(kb);
        SSL_CTX_use_certificate(m_ctx, cert);
        SSL_CTX_use_PrivateKey(m_ctx, key);
        X509_free(cert);
        EVP_PKEY_free(key);
        // Ask for the head unit's certificate, as the phone does
        SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER, [](int, X509_STORE_CTX *) { return 1; });
        m_ssl = SSL_new(m_ctx);
        m_in = BIO_new(BIO_s_mem());
        m_out = BIO_new(BIO_s_mem());
        SSL_set_bio(m_ssl, m_in, m_out);
        SSL_set_accept_state(m_ssl);
    }
    ~FakePhone()
    {
        SSL_free(m_ssl);
        SSL_CTX_free(m_ctx);
    }

    // Bytes from the head unit; replies pile up in outbox
    void receive(const QByteArray &bytes)
    {
        m_reader.feed(bytes);
        Frame f;
        while (m_reader.next(f)) {
            QByteArray plain = f.payload;
            if (f.flags & Flag::Encrypted)
                plain = decrypt(f.payload);
            Message m;
            if (m_assembler.add(f.channel, f.flags, plain, m))
                handle(m);
        }
    }

    // Phone → head unit, one frame per message (or fragments for big ones)
    void send(quint8 channel, quint16 id, const QByteArray &body, bool encrypted = true, bool control = false)
    {
        const QByteArray msg = messageBytes(id, body);
        const quint8 flags = (encrypted ? Flag::Encrypted : 0) | (control ? Flag::Control : 0);
        if (msg.size() <= 0x4000) {
            outbox.append(encodeFrames(channel, flags, encrypted ? encrypt(msg) : msg));
            return;
        }
        // Like the phone: every fragment encrypted on its own, FIRST carries the total
        const int chunk = 0x4000;
        for (qsizetype pos = 0; pos < msg.size(); pos += chunk) {
            const QByteArray part = msg.mid(pos, chunk);
            const QByteArray payload = encrypted ? encrypt(part) : part;
            quint8 type = 0;
            if (pos == 0) type = Flag::First;
            else if (pos + chunk >= msg.size()) type = Flag::Last;
            QByteArray frame;
            frame.append(char(channel));
            frame.append(char(flags | type));
            char size[2];
            qToBigEndian(quint16(payload.size()), size);
            frame.append(size, 2);
            if (type == Flag::First) {
                char total[4];
                qToBigEndian(quint32(msg.size()), total);
                frame.append(total, 4);
            }
            frame.append(payload);
            outbox.append(frame);
        }
    }

    QByteArray outbox;
    QList<Message> received;            // every decrypted message after the handshake
    QByteArray discoveryResponse;
    bool authComplete = false;
    bool tlsDone = false;
    QString headUnitSubject;

private:
    QByteArray drain()
    {
        QByteArray out;
        char buf[4096];
        int n;
        while ((n = BIO_read(m_out, buf, sizeof buf)) > 0)
            out.append(buf, n);
        return out;
    }
    QByteArray encrypt(const QByteArray &plain)
    {
        SSL_write(m_ssl, plain.constData(), int(plain.size()));
        return drain();
    }
    QByteArray decrypt(const QByteArray &cipher)
    {
        BIO_write(m_in, cipher.constData(), int(cipher.size()));
        QByteArray out;
        char buf[16384];
        int n;
        while ((n = SSL_read(m_ssl, buf, sizeof buf)) > 0)
            out.append(buf, n);
        return out;
    }

    void handle(const Message &m)
    {
        if (m.channel == Channel::Control && m.id == Msg::VersionRequest) {
            QByteArray v(6, '\0');
            qToBigEndian(quint16(1), v.data());
            qToBigEndian(quint16(7), v.data() + 2);
            qToBigEndian(quint16(0), v.data() + 4);
            send(Channel::Control, Msg::VersionResponse, v, false);
            return;
        }
        if (m.channel == Channel::Control && m.id == Msg::EncapsulatedSsl) {
            BIO_write(m_in, m.body.constData(), int(m.body.size()));
            if (SSL_do_handshake(m_ssl) == 1 && !tlsDone) {
                tlsDone = true;
                if (X509 *peer = SSL_get1_peer_certificate(m_ssl)) {
                    char buf[256] = {};
                    X509_NAME_oneline(X509_get_subject_name(peer), buf, sizeof buf);
                    headUnitSubject = QString::fromUtf8(buf);
                    X509_free(peer);
                }
            }
            const QByteArray out = drain();
            if (!out.isEmpty())
                send(Channel::Control, Msg::EncapsulatedSsl, out, false);
            return;
        }
        if (m.channel == Channel::Control && m.id == Msg::AuthComplete) {
            authComplete = true;
            // ServiceDiscoveryRequest { label_text 4, device_name 5 }
            send(Channel::Control, Msg::ServiceDiscoveryRequest,
                 ProtoWriter().string(4, QStringLiteral("Android")).string(5, QStringLiteral("Fake Phone")).data());
            return;
        }
        if (m.channel == Channel::Control && m.id == Msg::ServiceDiscoveryResponse)
            discoveryResponse = m.body;
        received.append(m);
    }

    SSL_CTX *m_ctx = nullptr;
    SSL *m_ssl = nullptr;
    BIO *m_in = nullptr;
    BIO *m_out = nullptr;
    FrameReader m_reader;
    MessageAssembler m_assembler;
};

// Moves bytes both ways until neither side has anything to say
void pump(AapSession &hu, FakePhone &phone, QByteArray &toPhone)
{
    for (int i = 0; i < 100 && (!toPhone.isEmpty() || !phone.outbox.isEmpty()); ++i) {
        const QByteArray a = std::exchange(toPhone, QByteArray());
        if (!a.isEmpty())
            phone.receive(a);
        const QByteArray b = std::exchange(phone.outbox, QByteArray());
        if (!b.isEmpty())
            hu.receive(b);
    }
}

const Message *find(const QList<Message> &list, quint8 channel, quint16 id, int nth = 0)
{
    for (const Message &m : list) {
        if (m.channel == channel && m.id == id && nth-- == 0)
            return &m;
    }
    return nullptr;
}

} // namespace

class TstAndroidAuto : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<Aap::AudioStream>();
        m_headUnit = makeSelfSigned("Test Head Unit");
        m_phone = makeSelfSigned("Test Phone");
    }

    // ── protobuf wire format ──

    void protoRoundTrip()
    {
        const QByteArray b = ProtoWriter()
                                 .varint(1, 300)
                                 .int32(2, -1)
                                 .string(3, QStringLiteral("héllo"))
                                 .message(4, ProtoWriter().varint(1, 7))
                                 .boolean(5, true)
                                 .data();
        // 300 = 0xAC 0x02; -1 as int32 is ten bytes of varint
        QCOMPARE(b.left(3), QByteArray::fromHex("08ac02"));
        QCOMPARE(ProtoReader::varintField(b, 1).value(), quint64(300));
        QCOMPARE(qint32(qint64(ProtoReader::varintField(b, 2).value())), -1);
        QCOMPARE(QString::fromUtf8(ProtoReader::bytesField(b, 3).value()), QStringLiteral("héllo"));
        QCOMPARE(ProtoReader::varintField(ProtoReader::bytesField(b, 4).value(), 1).value(), quint64(7));
        QCOMPARE(ProtoReader::varintField(b, 5).value(), quint64(1));
        QVERIFY(!ProtoReader::varintField(b, 9).has_value());
    }

    void protoReaderRejectsTruncation()
    {
        QByteArray b = ProtoWriter().string(1, QStringLiteral("abcdef")).data();
        b.chop(2);
        ProtoReader r(b);
        ProtoReader::Field f;
        QVERIFY(!r.next(f));
        QVERIFY(!r.ok());
    }

    void sint32IsZigzag()
    {
        QCOMPARE(ProtoWriter().sint32(1, -1).data(), QByteArray::fromHex("0801"));
        QCOMPARE(ProtoWriter().sint32(1, 1).data(), QByteArray::fromHex("0802"));
    }

    // ── framing ──

    void bulkFrameRoundTrip()
    {
        const QByteArray wire = encodeFrames(3, Flag::Encrypted, QByteArray("payload"));
        // channel, flags (BULK | ENCRYPTED), size, payload
        QCOMPARE(wire.left(4), QByteArray::fromHex("030b0007"));
        FrameReader r;
        r.feed(wire.left(5));
        Frame f;
        QVERIFY(!r.next(f));            // incomplete
        r.feed(wire.mid(5));
        QVERIFY(r.next(f));
        QCOMPARE(f.channel, quint8(3));
        QCOMPARE(f.flags, quint8(0x0b));
        QCOMPARE(f.payload, QByteArray("payload"));
    }

    void fragmentsReassembleAcrossInterleavedChannels()
    {
        // A 100 KB message on channel 3 split into frames, with a channel-0
        // message arriving between two of its fragments
        QByteArray big(100 * 1024, '\0');
        for (int i = 0; i < big.size(); ++i)
            big[i] = char(i * 7);
        const QByteArray msg = messageBytes(Msg::MediaData, big);
        const QByteArray wire = encodeFrames(3, 0, msg);
        QVERIFY(wire.size() > msg.size());
        QCOMPARE(quint8(wire[1]) & Flag::FrameTypeMask, Flag::First);

        FrameReader r;
        r.feed(wire);
        QList<Frame> frames;
        Frame f;
        while (r.next(f))
            frames.append(f);
        QVERIFY(frames.size() > 2);
        const QByteArray ping = encodeFrames(0, 0, messageBytes(Msg::PingRequest, QByteArray("x")));
        r.feed(ping);
        QVERIFY(r.next(f));
        frames.insert(1, f);

        MessageAssembler a;
        QList<Message> out;
        for (const Frame &fr : frames) {
            Message m;
            QString err;
            if (a.add(fr.channel, fr.flags, fr.payload, m, &err))
                out.append(m);
            QVERIFY2(err.isEmpty(), qPrintable(err));
        }
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].channel, quint8(0));
        QCOMPARE(out[0].id, Msg::PingRequest);
        QCOMPARE(out[1].channel, quint8(3));
        QCOMPARE(out[1].id, Msg::MediaData);
        QCOMPARE(out[1].body, big);
    }

    void fragmentWithoutFirstIsAnError()
    {
        MessageAssembler a;
        Message m;
        QString err;
        QVERIFY(!a.add(3, Flag::Last, QByteArray("abc"), m, &err));
        QVERIFY(!err.isEmpty());
    }

    // ── what the head unit announces ──

    void discoveryAnnouncesEveryRequiredChannel()
    {
        HeadUnitConfig c;
        c.width = 1280;
        c.height = 720;
        c.dpi = 160;
        const QByteArray r = serviceDiscoveryResponse(c);
        QHash<int, QByteArray> services;
        for (const QByteArray &s : ProtoReader::repeatedBytes(r, 1))
            services.insert(int(ProtoReader::varintField(s, 1).value()), s);
        // Android Auto refuses a head unit without an audio sink or a mic;
        // 10 is the media playback status (what plays, for nowPlaying)
        QCOMPARE(QSet<int>(services.keyBegin(), services.keyEnd()), (QSet<int>{1, 3, 4, 5, 6, 8, 9, 10}));
        QVERIFY(ProtoReader::bytesField(services[10], 9).has_value());

        const QByteArray video = ProtoReader::bytesField(services[3], 3).value();
        QCOMPARE(ProtoReader::varintField(video, 1).value(), quint64(3));              // H.264 BP
        const QByteArray vc = ProtoReader::bytesField(video, 4).value();
        QCOMPARE(ProtoReader::varintField(vc, 1).value(), quint64(2));                 // 1280x720
        QCOMPARE(ProtoReader::varintField(vc, 2).value(), quint64(2));                 // 30 fps
        QCOMPARE(ProtoReader::varintField(vc, 5).value(), quint64(160));               // density

        const QByteArray media = ProtoReader::bytesField(services[4], 3).value();
        QCOMPARE(ProtoReader::varintField(media, 2).value(), quint64(3));              // AUDIO_STREAM_MEDIA
        const QByteArray mediaCfg = ProtoReader::bytesField(media, 3).value();
        QCOMPARE(ProtoReader::varintField(mediaCfg, 1).value(), quint64(48000));
        QCOMPARE(ProtoReader::varintField(mediaCfg, 3).value(), quint64(2));

        const QByteArray touch = ProtoReader::bytesField(ProtoReader::bytesField(services[8], 4).value(), 2).value();
        QCOMPARE(ProtoReader::varintField(touch, 1).value(), quint64(1280));
        QCOMPARE(ProtoReader::varintField(touch, 2).value(), quint64(720));
        // the media keys OCTAVE's own controls send
        QList<quint64> keys;
        ProtoReader in(ProtoReader::bytesField(services[8], 4).value());
        ProtoReader::Field f;
        while (in.next(f)) {
            if (f.number == 1)
                keys.append(f.varint);
        }
        QVERIFY(keys.contains(quint64(Key::MediaPlayPause)));
        QVERIFY(keys.contains(quint64(Key::MediaNext)));
        QVERIFY(keys.contains(quint64(Key::MediaPrevious)));

        QVERIFY(ProtoReader::bytesField(services[9], 5).has_value());                  // media source (mic)
        QCOMPARE(ProtoReader::bytesField(r, 14).value(), QByteArray("OCTAVE"));        // display name
    }

    void unsupportedResolutionFailsToStart()
    {
        AapSession::Options o;
        o.unit.width = 1000;
        o.unit.height = 600;
        o.certPem = m_headUnit.cert;
        o.keyPem = m_headUnit.key;
        AapSession s(o);
        QSignalSpy failed(&s, &AapSession::failed);
        QVERIFY(!s.start());
        QCOMPARE(failed.size(), 1);
    }

    void badCertificateFailsToStart()
    {
        AapSession::Options o;
        o.certPem = "not a certificate";
        AapSession s(o);
        QSignalSpy failed(&s, &AapSession::failed);
        QVERIFY(!s.start());
        QCOMPARE(failed.size(), 1);
        QCOMPARE(s.state(), AapSession::State::Failed);
    }

    // ── a whole session ──

    void sessionAgainstFakePhone()
    {
        AapSession::Options o;
        o.certPem = m_headUnit.cert;
        o.keyPem = m_headUnit.key;
        AapSession hu(o);
        FakePhone phone(m_phone);
        QByteArray toPhone;
        connect(&hu, &AapSession::transmit, this, [&](const QByteArray &b) { toPhone.append(b); });
        QSignalSpy running(&hu, &AapSession::running);
        QSignalSpy config(&hu, &AapSession::videoCodecConfig);
        QSignalSpy frames(&hu, &AapSession::videoFrame);
        QSignalSpy streaming(&hu, &AapSession::videoStreaming);
        QSignalSpy audio(&hu, &AapSession::audioData);
        QSignalSpy ended(&hu, &AapSession::ended);
        QSignalSpy failed(&hu, &AapSession::failed);

        // version → TLS → auth → discovery
        QVERIFY(hu.start());
        pump(hu, phone, toPhone);
        QVERIFY(phone.tlsDone);
        QCOMPARE(phone.headUnitSubject, QStringLiteral("/CN=Test Head Unit"));
        QVERIFY(phone.authComplete);
        QCOMPARE(hu.state(), AapSession::State::Running);
        QCOMPARE(running.size(), 1);
        QCOMPARE(running.at(0).at(0).toString(), QStringLiteral("Fake Phone"));
        QVERIFY(!phone.discoveryResponse.isEmpty());

        // the phone opens the video channel
        phone.send(Channel::Video, Msg::ChannelOpenRequest, ProtoWriter().sint32(1, 0).int32(2, 3).data(), true, true);
        pump(hu, phone, toPhone);
        const Message *open = find(phone.received, Channel::Video, Msg::ChannelOpenResponse);
        QVERIFY(open);
        QVERIFY(open->flags & Flag::Control);
        QCOMPARE(ProtoReader::varintField(open->body, 1).value(), quint64(0));

        // setup → config + focus; start; codec config; one big fragmented frame
        phone.send(Channel::Video, Msg::MediaSetup, ProtoWriter().varint(1, 3).data());
        pump(hu, phone, toPhone);
        const Message *cfg = find(phone.received, Channel::Video, Msg::MediaConfig);
        QVERIFY(cfg);
        QCOMPARE(ProtoReader::varintField(cfg->body, 1).value(), quint64(2));        // READY
        QCOMPARE(ProtoReader::varintField(cfg->body, 2).value(), quint64(1));        // max_unacked
        const Message *focus = find(phone.received, Channel::Video, Msg::VideoFocusNotification);
        QVERIFY(focus);
        QCOMPARE(ProtoReader::varintField(focus->body, 1).value(), quint64(1));      // PROJECTED

        phone.send(Channel::Video, Msg::MediaStart, ProtoWriter().int32(1, 7).varint(2, 0).data());
        const QByteArray spsPps = QByteArray::fromHex("000000016742801f") + QByteArray(21, 'p');
        phone.send(Channel::Video, Msg::MediaCodecConfig, spsPps);
        QByteArray idr = QByteArray::fromHex("0000000165");
        idr.append(QByteArray(60000, 'v'));
        QByteArray data(8, '\0');
        qToBigEndian(quint64(123456), data.data());
        data.append(idr);
        phone.send(Channel::Video, Msg::MediaData, data);
        pump(hu, phone, toPhone);

        QCOMPARE(streaming.size(), 1);
        QCOMPARE(config.size(), 1);
        QCOMPARE(config.at(0).at(0).toByteArray(), spsPps);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.at(0).at(0).toByteArray(), idr);
        QCOMPARE(frames.at(0).at(1).toLongLong(), 123456);
        // one ack per codec config and per frame, with the START session id
        const Message *ack0 = find(phone.received, Channel::Video, Msg::MediaAck, 0);
        const Message *ack1 = find(phone.received, Channel::Video, Msg::MediaAck, 1);
        QVERIFY(ack0 && ack1);
        QCOMPARE(ProtoReader::varintField(ack1->body, 1).value(), quint64(7));
        QCOMPARE(ProtoReader::varintField(ack1->body, 2).value(), quint64(1));

        // media audio
        phone.send(Channel::MediaAudio, Msg::MediaSetup, ProtoWriter().varint(1, 1).data());
        phone.send(Channel::MediaAudio, Msg::MediaStart, ProtoWriter().int32(1, 2).varint(2, 0).data());
        QByteArray pcm(8, '\0');
        pcm.append(QByteArray(3840, 'a'));
        phone.send(Channel::MediaAudio, Msg::MediaData, pcm);
        pump(hu, phone, toPhone);
        QCOMPARE(audio.size(), 1);
        QCOMPARE(audio.at(0).at(0).value<Aap::AudioStream>(), Aap::AudioStream::Media);
        QCOMPARE(audio.at(0).at(1).toByteArray().size(), 3840);
        QVERIFY(find(phone.received, Channel::MediaAudio, Msg::MediaAck));

        // sensors answer with the current value
        phone.send(Channel::Sensor, Msg::SensorRequest, ProtoWriter().varint(1, 13).varint(2, 0).data());
        pump(hu, phone, toPhone);
        const Message *batch = find(phone.received, Channel::Sensor, Msg::SensorBatch);
        QVERIFY(batch);
        QCOMPARE(ProtoReader::varintField(ProtoReader::bytesField(batch->body, 13).value(), 1).value(), quint64(0));

        // control requests the phone makes
        phone.send(Channel::Control, Msg::AudioFocusRequest, ProtoWriter().varint(1, 1).data());
        phone.send(Channel::Control, Msg::PingRequest, ProtoWriter().varint(1, 42).data());
        pump(hu, phone, toPhone);
        const Message *af = find(phone.received, Channel::Control, Msg::AudioFocusNotification);
        QVERIFY(af);
        QCOMPARE(ProtoReader::varintField(af->body, 1).value(), quint64(1));         // GAIN
        const Message *pong = find(phone.received, Channel::Control, Msg::PingResponse);
        QVERIFY(pong);
        QCOMPARE(ProtoReader::varintField(pong->body, 1).value(), quint64(42));

        // touch goes out as an input report in video pixels
        hu.sendTouch({TouchPointer{640, 360, 0}}, 0, TouchAction::Down);
        pump(hu, phone, toPhone);
        const Message *input = find(phone.received, Channel::Input, Msg::InputReport);
        QVERIFY(input);
        const QByteArray touch = ProtoReader::bytesField(input->body, 3).value();
        const QByteArray pointer = ProtoReader::bytesField(touch, 1).value();
        QCOMPARE(ProtoReader::varintField(pointer, 1).value(), quint64(640));
        QCOMPARE(ProtoReader::varintField(pointer, 2).value(), quint64(360));
        QCOMPARE(ProtoReader::varintField(touch, 3).value(), quint64(0));            // ACTION_DOWN

        // polite end
        hu.end();
        pump(hu, phone, toPhone);
        QVERIFY(find(phone.received, Channel::Control, Msg::ByeByeRequest));
        phone.send(Channel::Control, Msg::ByeByeResponse, QByteArray());
        pump(hu, phone, toPhone);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(hu.state(), AapSession::State::Ended);
        QCOMPARE(failed.size(), 0);
    }

    void phoneByeByeEndsTheSession()
    {
        AapSession::Options o;
        o.certPem = m_headUnit.cert + m_headUnit.key;   // one PEM holding both
        AapSession hu(o);
        FakePhone phone(m_phone);
        QByteArray toPhone;
        connect(&hu, &AapSession::transmit, this, [&](const QByteArray &b) { toPhone.append(b); });
        QSignalSpy ended(&hu, &AapSession::ended);
        QVERIFY(hu.start());
        pump(hu, phone, toPhone);
        QCOMPARE(hu.state(), AapSession::State::Running);
        phone.send(Channel::Control, Msg::ByeByeRequest, ProtoWriter().varint(1, 1).data());
        pump(hu, phone, toPhone);
        QCOMPARE(ended.size(), 1);
        QVERIFY(find(phone.received, Channel::Control, Msg::ByeByeResponse));
    }

    void microphoneOnlyWhileOpen()
    {
        AapSession::Options o;
        o.certPem = m_headUnit.cert;
        o.keyPem = m_headUnit.key;
        AapSession hu(o);
        FakePhone phone(m_phone);
        QByteArray toPhone;
        connect(&hu, &AapSession::transmit, this, [&](const QByteArray &b) { toPhone.append(b); });
        QSignalSpy requested(&hu, &AapSession::microphoneRequested);
        QVERIFY(hu.start());
        pump(hu, phone, toPhone);
        QCOMPARE(hu.state(), AapSession::State::Running);

        const QByteArray pcm(640, 'm');     // 20 ms of 16 kHz mono s16
        hu.sendMicrophoneAudio(pcm);         // not open yet: dropped
        pump(hu, phone, toPhone);
        QVERIFY(!find(phone.received, Channel::Microphone, Msg::MediaData));

        phone.send(Channel::Microphone, Msg::MicrophoneRequest, ProtoWriter().boolean(1, true).data());
        pump(hu, phone, toPhone);
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.at(0).at(0).toBool(), true);
        QVERIFY(find(phone.received, Channel::Microphone, Msg::MicrophoneResponse));
        hu.sendMicrophoneAudio(pcm);
        pump(hu, phone, toPhone);
        const Message *data = find(phone.received, Channel::Microphone, Msg::MediaData);
        QVERIFY(data);
        QCOMPARE(data->body.size(), 8 + pcm.size());   // timestamp, then the PCM
        QCOMPARE(data->body.mid(8), pcm);

        phone.send(Channel::Microphone, Msg::MicrophoneRequest, ProtoWriter().boolean(1, false).data());
        pump(hu, phone, toPhone);
        QVERIFY(!hu.microphoneOpen());
        hu.sendMicrophoneAudio(pcm);
        pump(hu, phone, toPhone);
        QVERIFY(!find(phone.received, Channel::Microphone, Msg::MediaData, 1));
    }

    void mediaFocusDecidedOutside()
    {
        AapSession::Options o;
        o.certPem = m_headUnit.cert;
        o.keyPem = m_headUnit.key;
        o.externalAudioFocus = true;
        AapSession hu(o);
        FakePhone phone(m_phone);
        QByteArray toPhone;
        connect(&hu, &AapSession::transmit, this, [&](const QByteArray &b) { toPhone.append(b); });
        QSignalSpy requested(&hu, &AapSession::audioFocusRequested);
        QVERIFY(hu.start());
        pump(hu, phone, toPhone);
        QCOMPARE(hu.state(), AapSession::State::Running);

        // GAIN waits for the answer
        phone.send(Channel::Control, Msg::AudioFocusRequest, ProtoWriter().varint(1, Focus::RequestGain).data());
        pump(hu, phone, toPhone);
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.at(0).at(0).toInt(), Focus::RequestGain);
        QVERIFY(!find(phone.received, Channel::Control, Msg::AudioFocusNotification));
        hu.answerAudioFocus(Focus::Loss);
        pump(hu, phone, toPhone);
        const Message *refused = find(phone.received, Channel::Control, Msg::AudioFocusNotification);
        QVERIFY(refused);
        QCOMPARE(ProtoReader::varintField(refused->body, 1).value(), quint64(Focus::Loss));
        QCOMPARE(ProtoReader::varintField(refused->body, 2).value(), quint64(0));    // an answer

        // transient requests (prompts, Assistant) are granted on the spot
        phone.send(Channel::Control, Msg::AudioFocusRequest,
                   ProtoWriter().varint(1, Focus::RequestGainTransientMayDuck).data());
        pump(hu, phone, toPhone);
        const Message *transient = find(phone.received, Channel::Control, Msg::AudioFocusNotification, 1);
        QVERIFY(transient);
        QCOMPARE(ProtoReader::varintField(transient->body, 1).value(), quint64(Focus::Gain));
        QCOMPARE(requested.size(), 2);

        // the head unit takes focus back: unsolicited LOSS
        hu.setAudioFocus(Focus::Loss);
        pump(hu, phone, toPhone);
        const Message *taken = find(phone.received, Channel::Control, Msg::AudioFocusNotification, 2);
        QVERIFY(taken);
        QCOMPARE(ProtoReader::varintField(taken->body, 1).value(), quint64(Focus::Loss));
        QCOMPARE(ProtoReader::varintField(taken->body, 2).value(), quint64(1));      // unsolicited
    }

    void mediaKeysAndPlaybackStatus()
    {
        AapSession::Options o;
        o.certPem = m_headUnit.cert;
        o.keyPem = m_headUnit.key;
        AapSession hu(o);
        FakePhone phone(m_phone);
        QByteArray toPhone;
        connect(&hu, &AapSession::transmit, this, [&](const QByteArray &b) { toPhone.append(b); });
        QVERIFY(hu.start());
        pump(hu, phone, toPhone);

        // a key press is a down and an up report
        hu.sendKey(Key::MediaNext);
        pump(hu, phone, toPhone);
        for (int i = 0; i < 2; ++i) {
            const Message *report = find(phone.received, Channel::Input, Msg::InputReport, i);
            QVERIFY(report);
            const QByteArray key = ProtoReader::bytesField(ProtoReader::bytesField(report->body, 4).value(), 1).value();
            QCOMPARE(ProtoReader::varintField(key, 1).value(), quint64(Key::MediaNext));
            QCOMPARE(ProtoReader::varintField(key, 2).value(), quint64(i == 0 ? 1 : 0));
        }

        // what plays
        QSignalSpy status(&hu, &AapSession::playbackStatus);
        QSignalSpy metadata(&hu, &AapSession::playbackMetadata);
        phone.send(Channel::MediaStatus, Msg::PlaybackStatus,
                   ProtoWriter().varint(1, PlaybackStatus::Playing).string(2, QStringLiteral("Spotify"))
                       .varint(3, 42).boolean(4, true).data());
        const QByteArray art = QByteArray::fromHex("ffd8ffe0") + QByteArray(30000, 'j');   // fragmented
        phone.send(Channel::MediaStatus, Msg::PlaybackMetadata,
                   ProtoWriter().string(1, QStringLiteral("Song")).string(2, QStringLiteral("Artist"))
                       .string(3, QStringLiteral("Album")).bytes(4, art).varint(6, 215).data());
        pump(hu, phone, toPhone);
        QCOMPARE(status.size(), 1);
        const auto st = status.at(0).at(0).value<PlaybackStatus>();
        QCOMPARE(st.state, int(PlaybackStatus::Playing));
        QCOMPARE(st.source, QStringLiteral("Spotify"));
        QCOMPARE(st.positionS, 42);
        QVERIFY(st.shuffle);
        QCOMPARE(metadata.size(), 1);
        const auto md = metadata.at(0).at(0).value<PlaybackMetadata>();
        QCOMPARE(md.title, QStringLiteral("Song"));
        QCOMPARE(md.artist, QStringLiteral("Artist"));
        QCOMPARE(md.album, QStringLiteral("Album"));
        QCOMPARE(md.art, art);
        QCOMPARE(md.durationS, 215);
    }

    void silentPhoneTimesOut()
    {
        AapSession::Options o;
        o.certPem = m_headUnit.cert;
        o.keyPem = m_headUnit.key;
        o.pingIntervalMs = 20;
        o.pingTimeoutMs = 100;
        AapSession hu(o);
        FakePhone phone(m_phone);
        QByteArray toPhone;
        connect(&hu, &AapSession::transmit, this, [&](const QByteArray &b) { toPhone.append(b); });
        QSignalSpy failed(&hu, &AapSession::failed);
        QVERIFY(hu.start());
        pump(hu, phone, toPhone);
        QCOMPARE(hu.state(), AapSession::State::Running);
        // pings go out but nothing is pumped back
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QCOMPARE(hu.state(), AapSession::State::Failed);
    }

private:
    Pem m_headUnit;
    Pem m_phone;
};

QTEST_MAIN(TstAndroidAuto)
#include "tst_androidauto.moc"
