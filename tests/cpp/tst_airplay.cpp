// The AirPlay receiver and its manager without an iPhone: the track metadata
// and volume decoding, the pointer path that turns taps into mouse reports,
// the touch calibration, and a real receiver answering an RTSP /info
// request on loopback. Everything runs in a temp XDG_CONFIG_HOME; the
// Bluetooth mouse is never started (no OCTAVE_BLUEZ here).

#include "airplay/airplayreceiver.h"
#include "airplay/pointerpath.h"
#include "managers/airplaymanager.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <memory>

namespace {
QByteArray dmapItem(const char *tag, const QByteArray &value)
{
    QByteArray out(tag, 4);
    char len[4];
    qToBigEndian<quint32>(quint32(value.size()), len);
    out.append(len, 4);
    return out + value;
}
}

class TstAirPlay : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
    }

    void cleanup()
    {
        m_tmp.reset();
    }

    void parsesTrackMetadata()
    {
        const QByteArray body = dmapItem("minm", "Song Title") + dmapItem("asar", "The Artist")
                                + dmapItem("asal", "An Album") + dmapItem("asgn", "Rock");
        QString title, artist, album;
        AirPlayReceiver::parseMetadata(dmapItem("mlit", body), &title, &artist, &album);
        QCOMPARE(title, QStringLiteral("Song Title"));
        QCOMPARE(artist, QStringLiteral("The Artist"));
        QCOMPARE(album, QStringLiteral("An Album"));

        // Truncated or foreign blocks leave everything empty, never read past the end
        QString t2, a2, b2;
        QByteArray cut = dmapItem("mlit", body);
        cut.chop(5);
        AirPlayReceiver::parseMetadata(cut, &t2, &a2, &b2);
        QVERIFY(t2.isEmpty() && a2.isEmpty() && b2.isEmpty());
        AirPlayReceiver::parseMetadata(dmapItem("abcd", body), &t2, &a2, &b2);
        QVERIFY(t2.isEmpty());
        AirPlayReceiver::parseMetadata(QByteArray("mlit"), &t2, &a2, &b2);
        QVERIFY(t2.isEmpty());
    }

    void mapsAirPlayVolume()
    {
        QCOMPARE(AirPlayReceiver::volumeFraction(-144.0f), 0.0);   // AirPlay's mute
        QCOMPARE(AirPlayReceiver::volumeFraction(-30.0f), 0.0);
        QCOMPARE(AirPlayReceiver::volumeFraction(-15.0f), 0.5);
        QCOMPARE(AirPlayReceiver::volumeFraction(0.0f), 1.0);
        QCOMPARE(AirPlayReceiver::volumeFraction(3.0f), 1.0);
    }

    void pointerCountsFollowTheAspect()
    {
        const QSizeF portrait = PointerPath::countsAcross(QSizeF(900, 1950), 600);
        QCOMPARE(portrait.width(), 600.0);
        QCOMPARE(portrait.height(), 1300.0);
        const QSizeF landscape = PointerPath::countsAcross(QSizeF(1950, 900), 600);
        QCOMPARE(landscape.width(), 1300.0);
        QCOMPARE(landscape.height(), 600.0);
        QVERIFY(PointerPath::countsAcross(QSizeF(0, 100), 600).isEmpty());
    }

    void homePushesIntoTheCorner()
    {
        const QList<PointerPath::Report> home = PointerPath::home();
        QCOMPARE(home.size(), PointerPath::kHomeReports);
        for (const PointerPath::Report &r : home) {
            QCOMPARE(r.dx, -PointerPath::kHomeStep);
            QCOMPARE(r.dy, -PointerPath::kHomeStep);
            QVERIFY(!r.button);
        }
    }

    void moveTakesSmallStepsThatAddUp()
    {
        const QSizeF counts(600, 1300);
        const QList<PointerPath::Report> path = PointerPath::move({0.1, 0.2}, {0.6, 0.3}, counts, true);
        int x = 0, y = 0;
        for (const PointerPath::Report &r : path) {
            QVERIFY(qAbs(r.dx) <= PointerPath::kStep);
            QVERIFY(qAbs(r.dy) <= PointerPath::kStep);
            QVERIFY(r.button);
            x += r.dx;
            y += r.dy;
        }
        QCOMPARE(x, 300);
        QCOMPARE(y, 130);
        QVERIFY(PointerPath::move({0.5, 0.5}, {0.5, 0.5}, counts, false).isEmpty());
    }

    void moveCarriesTheRemainder()
    {
        // Many tiny drag moves (0.4 counts each) must not round away to nothing
        const QSizeF counts(1000, 1000);
        QPointF carry;
        int x = 0;
        QPointF at(0.5, 0.5);
        for (int i = 0; i < 100; ++i) {
            const QPointF next(at.x() + 0.0004, at.y());
            for (const PointerPath::Report &r : PointerPath::move(at, next, counts, true, &carry))
                x += r.dx;
            at = next;
        }
        QCOMPARE(x, 40);
    }

    void calibrationRecoversTheScale()
    {
        // A true scale of 600 on a portrait 900x1950 picture: 240 counts lands
        // at 0.4 across and 240/1300 down
        const QSizeF screen(900, 1950);
        const QPointF shown(240.0 / 600, 240.0 / 1300);
        QCOMPARE(qRound(AirPlayManager::scaleFromCalibration(shown, screen)), 600);
        // A tap in the corner (or nowhere sensible) is refused
        QCOMPARE(AirPlayManager::scaleFromCalibration({0.0, 0.0}, screen), 0.0);
        QCOMPARE(AirPlayManager::scaleFromCalibration({0.99, 0.0001}, screen), 0.0);
    }

    void managerIsOffUntilEnabled()
    {
        AirPlayManager manager;
        QVERIFY(manager.available());
        QVERIFY(!manager.enabled());
        QCOMPARE(manager.state(), QStringLiteral("off"));
        // No Bluetooth in this build of the test: touch is unavailable and
        // taps are dropped without a crash
        QCOMPARE(manager.touchState(), QStringLiteral("unavailable"));
        manager.injectTouch(0, 0, 0.5, 0.5);
        manager.injectTouch(0, 1, 0.5, 0.5);
        manager.scroll(0, 1);
    }

    void receiverAnswersInfo()
    {
        AirPlayReceiver receiver;
        AirPlayReceiver::Options opt;
        opt.name = QStringLiteral("OCTAVE-test");
        opt.deviceId = QStringLiteral("02:00:00:00:00:01");
        opt.keyFile = m_tmp->path() + "/receiver.pem";
        opt.width = 800;
        opt.height = 480;
        QString error;
        if (!receiver.start(opt, &error))
            QSKIP(qPrintable(QStringLiteral("no network for the receiver here: ") + error));
        QVERIFY(receiver.isRunning());
        QVERIFY(receiver.port() > 0);
        QVERIFY(QFile::exists(opt.keyFile));

        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, receiver.port());
        QVERIFY(socket.waitForConnected(3000));
        socket.write("GET /info RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: AirPlay/690.7.1\r\n\r\n");
        QByteArray reply;
        QTRY_VERIFY_WITH_TIMEOUT((reply += socket.readAll()).contains("\r\n\r\n"), 3000);
        QVERIFY2(reply.startsWith("RTSP/1.0 200 OK"), reply.left(80).constData());
        QVERIFY(reply.contains("application/x-apple-binary-plist"));
        // The plist carries the offered display and the name
        QTRY_VERIFY_WITH_TIMEOUT((reply += socket.readAll()).contains("OCTAVE-test"), 3000);
        socket.close();

        receiver.stop();
        QVERIFY(!receiver.isRunning());
        // A second start reuses the stored key
        QVERIFY2(receiver.start(opt, &error), qPrintable(error));
        receiver.stop();
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstAirPlay)
#include "tst_airplay.moc"
