// StreamHub (src/service/streamhub.{h,cpp}): the H.264 helpers on the dev
// test stream (tests/data/stream-test.h264: 640x360 then 1280x720), and
// the hub's rules against real Connection queues: nothing until a keyframe,
// parameter sets on every keyframe, streamConfig on a change, a subscriber
// that falls behind dropped back to the next keyframe, keyframe requests
// rate-limited across subscribers, and a closed connection unsubscribed.

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QtEndian>

#include <memory>

#include "service/commandserver.h"
#include "service/streamhub.h"

using namespace OctaveService;

namespace {
const QString kStream = QStringLiteral(OCTAVE_SOURCE_DIR "/tests/data/stream-test.h264");

struct Rec {
    int stream;
    int flags;
    qint64 pts;
    QByteArray payload;
};
QList<Rec> records(Connection &c, QList<QJsonObject> *events = nullptr)
{
    QList<Rec> out;
    for (const Connection::Item &i : c.takeAll()) {
        if (!i.binary) {
            if (events)
                events->append(QJsonDocument::fromJson(i.bytes).object());
            continue;
        }
        out.append({quint8(i.bytes.at(1)), quint8(i.bytes.at(2)), qint64(qFromBigEndian<quint64>(i.bytes.constData() + 4)),
                    i.bytes.mid(12)});
    }
    return out;
}
int nalType(const QByteArray &nal) { return quint8(nal.at(0)) & 0x1F; }
} // namespace

class TstStreamHub : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QFile f(kStream);
        QVERIFY2(f.open(QIODevice::ReadOnly), "run tools/stream/make_test_h264.sh");
        m_units = H264::accessUnits(f.readAll());
    }

    void splitsTheTestStream()
    {
        QCOMPARE(m_units.size(), 120);   // 4 s at 30 fps
        int keys = 0, sps = 0;
        for (const QByteArray &au : m_units) {
            bool key = false;
            for (const QByteArray &nal : H264::nalUnits(au)) {
                key = key || nalType(nal) == 5;   // x264 may cut a picture into several slices
                sps += nalType(nal) == 7;
            }
            keys += key;
        }
        QCOMPARE(keys, 4);   // one a second
        QVERIFY(sps >= 2);   // a config per resolution at least
    }

    void readsTheSps()
    {
        QList<QPair<int, int>> sizes;
        QString codec;
        for (const QByteArray &au : m_units)
            for (const QByteArray &nal : H264::nalUnits(au)) {
                int w = 0, h = 0;
                if (H264::parseSps(nal, &codec, &w, &h) && (sizes.isEmpty() || sizes.last() != qMakePair(w, h)))
                    sizes.append({w, h});
            }
        QCOMPARE(sizes, (QList<QPair<int, int>>{{640, 360}, {1280, 720}}));
        QCOMPARE(codec, QStringLiteral("avc1.42c01f"));   // constrained baseline, level 3.1
        QVERIFY(!H264::parseSps(QByteArray("\x68\x01", 2), nullptr, nullptr, nullptr));   // a PPS
    }

    void joinsAtAKeyframeWithItsParameterSets()
    {
        StreamHub &hub = StreamHub::instance();
        int keyRequests = 0;
        hub.declare("test.video", [&keyRequests] { ++keyRequests; });
        auto c = std::make_shared<Connection>();
        // Mid-GOP: the config arrives first (as the phone sends it), then frames
        publish(hub, 0, 45);   // through the first keyframe and into the second second
        QVERIFY(!hub.wanted("test.video"));
        const int id = hub.subscribe("test.video", c);
        QCOMPARE(id, 1);
        QVERIFY(hub.wanted("test.video"));
        QCOMPARE(hub.info("test.video").value("width").toInt(), 640);
        QCOMPARE(keyRequests, 1);   // asked at once for a fast start

        publish(hub, 45, 75);   // deltas, then the keyframe at 60
        const QList<Rec> got = records(*c);
        QCOMPARE(got.size(), 15);   // 60..74
        QCOMPARE(got.first().stream, id);
        QCOMPARE(got.first().flags, StreamHub::Key | StreamHub::Config | StreamHub::Discontinuity);
        const QList<QByteArray> nals = H264::nalUnits(got.first().payload);
        QCOMPARE(nalType(nals.at(0)), 7);   // SPS, PPS, then the IDR: a decoder can start here
        QCOMPARE(got.first().pts, 60 * 33333);
        QCOMPARE(got.at(1).flags, 0);
        hub.unsubscribeAll(c.get());
        QVERIFY(!hub.wanted("test.video"));
    }

    void configChangesAreAnnounced()
    {
        StreamHub &hub = StreamHub::instance();
        hub.declare("test.video");
        auto c = std::make_shared<Connection>();
        publish(hub, 0, 1);
        const int id = hub.subscribe("test.video", c);
        publish(hub, 30, 120);   // the second half is 1280x720
        QList<QJsonObject> events;
        const QList<Rec> got = records(*c, &events);
        QCOMPARE(got.size(), 90);
        QJsonObject config;
        for (const QJsonObject &e : events)
            if (e.value("event").toString() == "streamConfig")
                config = e;
        QCOMPARE(config.value("stream").toInt(), id);
        QCOMPARE(config.value("width").toInt(), 1280);
        QCOMPARE(config.value("height").toInt(), 720);
        QCOMPARE(config.value("codec").toString(), QStringLiteral("avc1.42c01f"));
        hub.unsubscribeAll(c.get());
    }

    void fallingBehindDropsToTheNextKeyframe()
    {
        StreamHub &hub = StreamHub::instance();
        hub.declare("test.video");
        auto c = std::make_shared<Connection>();
        publish(hub, 0, 1);
        hub.subscribe("test.video", c);
        // Nobody reads: the queue passes its cap within the first seconds
        publish(hub, 0, 120);
        QList<QJsonObject> events;
        const QList<Rec> got = records(*c, &events);
        bool dropped = false;
        for (const QJsonObject &e : events)
            dropped = dropped || e.value("event").toString() == "streamDropped";
        QVERIFY(dropped);
        QVERIFY(got.size() < 120);
        QVERIFY(!got.isEmpty());
        QVERIFY(got.first().flags & StreamHub::Key);   // what is left starts at a keyframe
        QVERIFY(got.first().flags & StreamHub::Discontinuity);
        hub.unsubscribeAll(c.get());
    }

    void keyRequestsAreRateLimitedAcrossSubscribers()
    {
        StreamHub &hub = StreamHub::instance();
        int keyRequests = 0;
        hub.declare("test.video2", [&keyRequests] { ++keyRequests; });
        QTest::qWait(StreamHub::kKeyRequestIntervalMs + 50);
        QList<std::shared_ptr<Connection>> many;
        for (int i = 0; i < 20; ++i) {
            many.append(std::make_shared<Connection>());
            hub.subscribe("test.video2", many.last());
        }
        QCOMPARE(keyRequests, 1);
        for (const auto &c : many)
            hub.unsubscribeAll(c.get());
    }

    void closedConnectionsAreForgotten()
    {
        StreamHub &hub = StreamHub::instance();
        hub.declare("test.video");
        auto c = std::make_shared<Connection>();
        hub.subscribe("test.video", c);
        c->close();
        publish(hub, 0, 2);   // the next publish notices
        QVERIFY(!hub.wanted("test.video"));
        QCOMPARE(hub.subscribe("nope.video", c), 0);
    }

private:
    // The test stream's access units [from, to) as the phone would send
    // them: parameter sets as a config packet, then the picture
    void publish(StreamHub &hub, int from, int to)
    {
        for (int i = from; i < to; ++i) {
            QByteArray config, picture;
            bool key = false;
            for (const QByteArray &nal : H264::nalUnits(m_units.at(i))) {
                const QByteArray annexB = QByteArray("\x00\x00\x00\x01", 4) + nal;
                if (nalType(nal) == 7 || nalType(nal) == 8)
                    config += annexB;
                else
                    picture += annexB;
                key = key || nalType(nal) == 5;
            }
            if (!config.isEmpty())
                hub.publishH264("test.video", config, 0, false, true);
            hub.publishH264("test.video", picture, i * 33333LL, key, false);
            hub.publishH264("test.video2", picture, i * 33333LL, key, false);
        }
    }

    QList<QByteArray> m_units;
};

QTEST_GUILESS_MAIN(TstStreamHub)
#include "tst_streamhub.moc"
