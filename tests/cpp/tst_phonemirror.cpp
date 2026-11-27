// PhoneMirrorManager without a phone: the adb parsing / state / serial-choice
// / server-log tables, Wi-Fi serials and messages, QR pairing, the phone media
// follow / hand-back rules, notices, the encoder fallback, and the async start
// flow with tests/cpp/fake-adb.sh standing in for adb.

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoSink>
#include <QtEndian>

#include <memory>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <QStandardPaths>
#include <QTemporaryFile>
#include <QProcess>
#include "managers/phonemirrormanager.h"
#include "phone_mirror/scrcpyclient.h"

using Rows = QList<QPair<QString, QString>>;
Q_DECLARE_METATYPE(Rows)

namespace {
const QString kFakeAdb = QStringLiteral(OCTAVE_SOURCE_DIR "/tests/cpp/fake-adb.sh");
const QString USB = QStringLiteral("R5CT1234ABC");
const QString USB2 = QStringLiteral("1A2B3C4D");
const QString TCP = QStringLiteral("192.168.1.5:37199");
const QString MDNS = QStringLiteral("adb-R5CT1234ABC-AbCdEf._adb-tls-connect._tcp");

QVariantMap mediaState(bool playing, const QString &title = "Song", bool hasSession = true)
{
    return {{"has_session", hasSession}, {"playing", playing}, {"can_seek", true}, {"position_ms", 1000},
            {"duration_ms", 200000}, {"package", "com.example.music"}, {"title", title}, {"artist", "A"},
            {"album", "B"}};
}
} // namespace

class TstPhoneMirror : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("HOME", m_tmp.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + "/config").toUtf8());
        qputenv("FAKE_ADB_LOG", m_tmp.filePath("adb.log").toUtf8());
        qputenv("FAKE_ADB_DEVICES", m_tmp.filePath("devices").toUtf8());
        qputenv("FAKE_ADB_GATE", m_tmp.filePath("gate").toUtf8());
        // Never the machine's own USB bus: a developer's phone may be plugged in
        QVERIFY(QDir().mkpath(m_tmp.filePath("usb")));
        qputenv("OCTAVE_USB_SYSFS", m_tmp.filePath("usb").toUtf8());
        // ... nor its adb server, which a developer's machine usually runs
        qputenv("ANDROID_ADB_SERVER_PORT", "1");
    }

    void init()
    {
        QFile::remove(m_tmp.filePath("adb.log"));
        QFile::remove(m_tmp.filePath("gate"));
        setDevices({});
    }

    // ── Tables ──────────────────────────────────────────────────────────
    void parseDevices_data()
    {
        QTest::addColumn<QString>("output");
        QTest::addColumn<Rows>("rows");
        QTest::newRow("empty list") << "List of devices attached\n" << Rows{};
        QTest::newRow("nothing") << "" << Rows{};
        QTest::newRow("device") << "List of devices attached\n" + USB + "\tdevice\n" << Rows{{USB, "device"}};
        QTest::newRow("unauthorized") << "List of devices attached\n" + USB + "\tunauthorized\n"
                                      << Rows{{USB, "unauthorized"}};
        QTest::newRow("offline") << "List of devices attached\n" + USB + "\toffline\n" << Rows{{USB, "offline"}};
        QTest::newRow("daemon starting")
            << "* daemon not running; starting now at tcp:5037\n* daemon started successfully\n"
               "List of devices attached\n" + USB + "\tdevice\n"
            << Rows{{USB, "device"}};
        QTest::newRow("no udev rules")
            << "List of devices attached\n" + USB
                   + "\tno permissions (user in plugdev group; are your udev rules wrong?); "
                     "see [http://developer.android.com/tools/device.html]\n"
            << Rows{{USB, "no-permissions"}};
        QTest::newRow("mdns + tcp") << "List of devices attached\n" + MDNS + "\tdevice\n" + TCP + "\toffline\n"
                                    << Rows{{MDNS, "device"}, {TCP, "offline"}};
        QTest::newRow("-l columns, CRLF")
            << "List of devices attached\r\n" + USB + "       device usb:1-1 product:x model:SM_S918B\r\n"
            << Rows{{USB, "device"}};
        QTest::newRow("version clash")
            << "adb server version (41) doesn't match this client (39); killing...\n"
               "* daemon started successfully\nList of devices attached\n" + USB + "\tdevice\n"
            << Rows{{USB, "device"}};
        QTest::newRow("no daemon") << "adb: failed to check server version: cannot connect to daemon\n" << Rows{};
    }

    void parseDevices()
    {
        QFETCH(QString, output);
        QFETCH(Rows, rows);
        QCOMPARE(PhoneMirrorManager::parseDevices(output), rows);
    }

    void autoSizeWorthRestart_data()
    {
        QTest::addColumn<QString>("started");
        QTest::addColumn<QString>("wanted");
        QTest::addColumn<bool>("restart");
        QTest::newRow("same") << "1224x632" << "1224x632" << false;
        QTest::newRow("a few pixels") << "1224x632" << "1232x640" << false;
        QTest::newRow("rotated") << "1224x632" << "632x1224" << true;
        QTest::newRow("much bigger") << "1224x632" << "1920x992" << true;
        QTest::newRow("narrower") << "1224x632" << "1104x632" << true;
        QTest::newRow("unparsable") << "" << "1224x632" << true;
    }

    void autoSizeWorthRestart()
    {
        QFETCH(QString, started);
        QFETCH(QString, wanted);
        QFETCH(bool, restart);
        QCOMPARE(PhoneMirrorManager::autoSizeWorthRestart(started, wanted), restart);
    }

    void summarizeState_data()
    {
        QTest::addColumn<Rows>("rows");
        QTest::addColumn<QString>("state");
        QTest::newRow("none") << Rows{} << "none";
        QTest::newRow("device") << Rows{{USB, "device"}} << "device";
        QTest::newRow("any device") << Rows{{USB, "offline"}, {TCP, "device"}} << "device";
        QTest::newRow("unauthorized") << Rows{{USB, "offline"}, {USB2, "unauthorized"}} << "unauthorized";
        QTest::newRow("no-permissions") << Rows{{USB, "offline"}, {USB2, "no-permissions"}} << "no-permissions";
        QTest::newRow("unauthorized first") << Rows{{USB, "no-permissions"}, {USB2, "unauthorized"}} << "unauthorized";
        QTest::newRow("offline") << Rows{{TCP, "offline"}} << "offline";
        QTest::newRow("recovery") << Rows{{USB, "recovery"}} << "none";
    }

    void summarizeState()
    {
        QFETCH(Rows, rows);
        QFETCH(QString, state);
        QCOMPARE(PhoneMirrorManager::summarizeState(rows), state);
    }

    void pickSerial_data()
    {
        QTest::addColumn<Rows>("rows");
        QTest::addColumn<QString>("preferred");
        QTest::addColumn<QString>("serial");
        QTest::newRow("none") << Rows{} << "" << "";
        QTest::newRow("one") << Rows{{USB, "device"}} << "" << USB;
        QTest::newRow("USB first") << Rows{{TCP, "device"}, {USB, "device"}} << "" << USB;
        QTest::newRow("usable Wi-Fi") << Rows{{TCP, "device"}, {USB, "unauthorized"}} << "" << TCP;
        QTest::newRow("first network") << Rows{{MDNS, "device"}, {TCP, "device"}} << "" << MDNS;
        QTest::newRow("first USB") << Rows{{USB, "device"}, {USB2, "device"}} << "" << USB;
        QTest::newRow("picked") << Rows{{USB, "device"}, {USB2, "device"}} << USB2 << USB2;
        QTest::newRow("picked Wi-Fi") << Rows{{USB, "device"}, {TCP, "device"}} << TCP << TCP;
        QTest::newRow("picked not usable") << Rows{{USB, "device"}, {USB2, "offline"}} << USB2 << USB;
        QTest::newRow("nothing usable") << Rows{{USB, "offline"}} << USB << "";
    }

    void pickSerial()
    {
        QFETCH(Rows, rows);
        QFETCH(QString, preferred);
        QFETCH(QString, serial);
        QCOMPARE(PhoneMirrorManager::pickSerial(rows, preferred), serial);
    }

    void classifyServerLine_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("kind");
        QTest::newRow("inject")
            << "[server] ERROR: Could not inject event: java.lang.SecurityException: Injecting input events requires "
               "the caller (or the source of the instrumentation, if any) to have the INJECT_EVENTS permission."
            << "inject";
        QTest::newRow("no audio") << "[server] WARN: Audio disabled: it is not supported before Android 11"
                                  << "no-audio";
        QTest::newRow("codec exception")
            << "[server] ERROR: Capture/encoding error: android.media.MediaCodec$CodecException: Error 0xfffffff4"
            << "encoder";
        QTest::newRow("encoding error") << "[server] ERROR: Video encoding error" << "encoder";
        QTest::newRow("default encoder") << "[server] ERROR: Could not create default video encoder for h264" << "encoder";
        QTest::newRow("create encoder") << "[server] ERROR: Could not create video encoder" << "encoder";
        QTest::newRow("encoder not found") << "[server] ERROR: Video encoder 'c2.foo.avc.encoder' for h264 not found"
                                           << "encoder";
        QTest::newRow("device") << "[server] INFO: Device: [samsung] samsung SM-S918B (Android 16)" << "";
        QTest::newRow("display") << "[server] INFO: New display: 1280x800/160 (id=12)" << "";
        QTest::newRow("audio by user") << "Audio disabled by the user" << "";
        QTest::newRow("empty") << "" << "";
    }

    void classifyServerLine()
    {
        QFETCH(QString, line);
        QFETCH(QString, kind);
        QCOMPARE(PhoneMirrorManager::classifyServerLine(line), kind);
    }

    void softwareEncoderFor()
    {
        QCOMPARE(PhoneMirrorManager::softwareEncoderFor(0), QStringLiteral("c2.android.avc.encoder"));    // unknown
        QCOMPARE(PhoneMirrorManager::softwareEncoderFor(-1), QStringLiteral("c2.android.avc.encoder"));
        QCOMPARE(PhoneMirrorManager::softwareEncoderFor(28), QStringLiteral("OMX.google.h264.encoder"));  // Android 9
        QCOMPARE(PhoneMirrorManager::softwareEncoderFor(29), QStringLiteral("c2.android.avc.encoder"));   // Android 10
        QCOMPARE(PhoneMirrorManager::softwareEncoderFor(36), QStringLiteral("c2.android.avc.encoder"));
    }

    void noticeTexts()
    {
        for (const char *kind : {"inject", "no-audio", "software-encoder"})
            QVERIFY2(!PhoneMirrorManager::noticeText(kind).isEmpty(), kind);
        QCOMPARE(PhoneMirrorManager::noticeText("encoder"), QString());
        QVERIFY(PhoneMirrorManager::noticeText("inject").contains("Xiaomi"));
        QVERIFY(!PhoneMirrorManager::describeDeviceState("no-permissions").contains("no permissions"));
        QVERIFY(PhoneMirrorManager::describeDeviceState("no-permissions").contains("udev"));
    }

    // ── Wi-Fi ───────────────────────────────────────────────────────────
    void networkSerials()
    {
        QVERIFY(!ScrcpyClient::isNetworkSerial(USB));
        QVERIFY(!ScrcpyClient::isNetworkSerial("emulator-5554"));
        QVERIFY(ScrcpyClient::isNetworkSerial(TCP));
        QVERIFY(ScrcpyClient::isNetworkSerial("[fe80::1]:5555"));
        QVERIFY(ScrcpyClient::isNetworkSerial(MDNS));
        QCOMPARE(PhoneMirrorManager::hostOf(TCP), QStringLiteral("192.168.1.5"));
        QCOMPARE(PhoneMirrorManager::hostOf("[fe80::1]:5555"), QStringLiteral("fe80::1"));
        QCOMPARE(PhoneMirrorManager::hostOf("192.168.1.5"), QStringLiteral("192.168.1.5"));
    }

    void wifiMessagesKeepRetryPhrases()
    {
        // PhoneMirrorView auto-retries on /disconnected|No Android device|not authorized|offline/
        PhoneMirrorManager m;
        const QString offline = m.describeProblem("offline", {{TCP, "offline"}});
        QVERIFY(offline.contains("Wi-Fi") && offline.contains("offline"));
        QVERIFY(m.describeProblem("unauthorized", {{TCP, "unauthorized"}}).contains("not authorized"));
        QVERIFY(m.describeProblem("offline", {{USB, "offline"}}).contains("cable"));
        QVERIFY(m.describeProblem("no-permissions", {{USB, "no-permissions"}}).contains("USB permissions"));
        m.m_wirelessAddress = TCP;
        const QString none = m.describeProblem("none", {});
        QVERIFY(none.startsWith("No Android device") && none.contains(TCP));
        QVERIFY(ScrcpyClient::disconnectedMessage(TCP).contains("Wi-Fi"));
        QVERIFY(ScrcpyClient::disconnectedMessage(TCP).contains("disconnected"));
        QVERIFY(ScrcpyClient::disconnectedMessage(USB).contains("USB"));
    }

    void everyMessageHasItsKind()
    {
        // The view retries on kind "device" / "noPhone" and shows the QR
        // pairing screen on "noPhone"; it no longer pattern-matches the text.
        // Same split as the regex it used: /disconnected|No Android device|
        // not authorized|offline|USB permissions/i, plus the final encoder
        // failure, which is the phone's limit, not a link problem.
        PhoneMirrorManager m;
        auto kind = &PhoneMirrorManager::errorKindFor;
        QCOMPARE(kind(m.describeDeviceState("none")), QStringLiteral("noPhone"));
        QCOMPARE(kind(m.describeDeviceState("unauthorized")), QStringLiteral("device"));
        QCOMPARE(kind(m.describeDeviceState("offline")), QStringLiteral("device"));
        QCOMPARE(kind(m.describeDeviceState("no-permissions")), QStringLiteral("device"));
        QCOMPARE(kind(m.describeDeviceState("no-adb")), QStringLiteral("setup"));
        QCOMPARE(kind(m.describeProblem("offline", {{TCP, "offline"}})), QStringLiteral("device"));
        QCOMPARE(kind(m.describeProblem("unauthorized", {{TCP, "unauthorized"}})), QStringLiteral("device"));
        m.m_wirelessAddress = TCP;
        QCOMPARE(kind(m.describeProblem("none", {})), QStringLiteral("noPhone"));
        QCOMPARE(kind(ScrcpyClient::disconnectedMessage(TCP)), QStringLiteral("device"));
        QCOMPARE(kind(ScrcpyClient::disconnectedMessage(USB)), QStringLiteral("device"));
        QCOMPARE(kind(QStringLiteral("Phone mirroring needs adb (install it).")), QStringLiteral("setup"));
        QCOMPARE(kind(QStringLiteral("Bundled scrcpy server could not be extracted")), QStringLiteral("setup"));
        QCOMPARE(kind(QStringLiteral("could not open H.264 decoder")), QStringLiteral("setup"));
        QCOMPARE(kind(QStringLiteral("could not push scrcpy server: error: device offline")), QStringLiteral("device"));
        QCOMPARE(kind(PhoneMirrorManager::kEncoderGaveUp), QStringLiteral("fatal"));
    }

    void startWithoutPhoneSetsTheKind()
    {
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        QSignalSpy error(&m, &PhoneMirrorManager::scrcpyError);
        PhoneMirrorManager::StartPrep prep;   // the probe found nothing
        prep.probe.state = QStringLiteral("none");
        m.continueStart(prep, m.m_startGeneration);
        QCOMPARE(error.count(), 1);
        QCOMPARE(m.lastErrorKind(), QStringLiteral("noPhone"));
    }

    void qrPairingPayloadAndCancel()
    {
        QCOMPARE(PhoneMirrorManager::qrPairingPayload("octave-abc", "pw123"),
                 QStringLiteral("WIFI:T:ADB;S:octave-abc;P:pw123;;"));
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;   // reports platform-tools 35: new enough to pair
        m.startQrPairing();
        const QStringList rows = m.m_qrRows;
        QVERIFY(rows.size() >= 21);
        for (const QString &r : rows) {
            QCOMPARE(r.size(), rows.size());
            for (QChar c : r)
                QVERIFY(c == '0' || c == '1');
        }
        // Finder pattern in the top-left corner: a 7-module dark ring
        QCOMPARE(rows[0].left(7), QStringLiteral("1111111"));
        QCOMPARE(rows[6].left(7), QStringLiteral("1111111"));
        m.cancelQrPairing();
        QVERIFY(m.m_qrRows.isEmpty());
    }

    // ── Phone media ─────────────────────────────────────────────────────
    void phoneHasControlsForTheWholeSession()
    {
        PhoneMirrorManager m;
        QSignalSpy active(&m, &PhoneMirrorManager::mediaActiveChanged);
        m.onMediaState(mediaState(false, "Song", false));
        QVERIFY(m.mediaActive() && !m.m_mediaAvailable);   // first report: the phone is the source
        m.onMediaState(mediaState(true));
        QVERIFY(m.mediaActive() && m.m_mediaPlaying && m.m_mediaTitle == "Song");
        m.onMediaState(mediaState(false));
        m.onMediaState(mediaState(false, "Song", false));
        QVERIFY(m.mediaActive());                          // paused, or no media app: still the phone
        m.releaseMedia(false);                             // a local track started
        m.onMediaState(mediaState(false));
        QVERIFY(!m.mediaActive());                         // a paused phone does not take them back
        m.onMediaState(mediaState(true));
        QVERIFY(m.mediaActive());                          // playing again does
        m.resetMedia();
        QList<bool> changes;
        for (const auto &a : active)
            changes << a.at(0).toBool();
        QCOMPARE(changes, (QList<bool>{true, false, true, false}));
    }

    void trackChangedOnlyForANewTrack()
    {
        // mediaChanged fires for every state report (play/pause, position,
        // metadata); mediaTrackChanged only when the track itself changes, so
        // consumers stop diffing title + artist themselves
        PhoneMirrorManager m;
        QSignalSpy track(&m, &PhoneMirrorManager::mediaTrackChanged);
        m.onMediaState(mediaState(true, "Song"));
        QCOMPARE(track.count(), 1);                // first report
        m.onMediaState(mediaState(false, "Song"));  // pause
        m.onMediaState(mediaState(true, "Song"));   // play, new position
        QCOMPARE(track.count(), 1);
        m.onMediaState(mediaState(true, "Other"));
        QCOMPARE(track.count(), 2);
        QVariantMap otherApp = mediaState(true, "Other");
        otherApp.insert("package", "com.example.podcasts");   // same title, another app
        m.onMediaState(otherApp);
        QCOMPARE(track.count(), 3);
        m.resetMedia();                            // the track is gone
        QCOMPARE(track.count(), 4);
        m.resetMedia();
        QCOMPARE(track.count(), 4);
    }

    void muteSilencesPhoneAudio()
    {
        PhoneMirrorManager m;
        m.m_client = new ScrcpyClient(kFakeAdb, "/nonexistent.jar", &m);
        m.setVolume(0.5f);
        QCOMPARE(m.m_client->m_volume.load(), 0.5f);
        m.setMuted(true);
        QCOMPARE(m.m_client->m_volume.load(), 0.0f);
        m.setVolume(0.7f);   // the slider moves while muted: stays silent
        QCOMPARE(m.m_client->m_volume.load(), 0.0f);
        m.setMuted(false);
        QCOMPARE(m.m_client->m_volume.load(), 0.7f);
    }

#ifdef Q_OS_UNIX
    void releaseHandsControlsBackAndPausesPhone()
    {
        PhoneMirrorManager m;
        m.m_client = new ScrcpyClient(kFakeAdb, "/nonexistent.jar", &m);
        int fds[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        m.m_client->m_controlFd = fds[0];
        m.m_ready = true;   // a running session; commands only go to one
        auto sent = [&]() {
            char buf[64];
            const ssize_t n = ::recv(fds[1], buf, sizeof buf, MSG_DONTWAIT);
            return n > 0 ? QByteArray(buf, int(n)) : QByteArray();
        };
        m.onMediaState(mediaState(true));
        m.releaseMedia(false);
        QVERIFY(!m.mediaActive());
        QCOMPARE(sent(), QByteArray());
        m.onMediaState(mediaState(false));
        m.onMediaState(mediaState(true));
        m.releaseMedia(true);
        QVERIFY(!m.mediaActive());
        const QByteArray pause = sent();
        m.m_client->m_controlFd = -1;
        ::close(fds[0]);
        ::close(fds[1]);
        QCOMPARE(pause.size(), 10);
        QCOMPARE(int(quint8(pause[0])), 103);
        QCOMPARE(int(quint8(pause[1])), int(ScrcpyClient::MediaPause));
    }
#endif

    void queueArtOnlyFromTheQueueArtFolder()
    {
        // GET /art?queue=<id>: a queue item's own art, nothing else
        PhoneMirrorManager m;
        m.onMediaQueue({QVariantMap{{"id", 7}, {"title", "A"}, {"art_uri", "content://a"}},
                        QVariantMap{{"id", 8}, {"title", "B"}, {"art_uri", "content://b"}}}, 7, "Q");
        QVERIFY(m.queueArtFile(7).isEmpty());   // art not there yet
        m.onMediaQueueArt("content://a", QByteArray("\xff\xd8\xffjpeg"));
        m.publishMediaQueue();
        const QString file = m.queueArtFile(7);
        QVERIFY(!file.isEmpty());
        QVERIFY(file.contains("/temp/phone_queue_art/"));
        QVERIFY(m.queueArtFile(8).isEmpty());
        QVERIFY(m.queueArtFile(99).isEmpty());   // not in the queue
        // An item whose art names a file outside that folder
        QTemporaryDir elsewhere;
        QFile o(elsewhere.filePath("x.jpg"));
        QVERIFY(o.open(QIODevice::WriteOnly));
        o.close();
        m.m_mediaQueue = {QVariantMap{{"id", 9}, {"art", QUrl::fromLocalFile(o.fileName()).toString()}}};
        QVERIFY(m.queueArtFile(9).isEmpty());
    }

    void resetClearsMedia()
    {
        PhoneMirrorManager m;
        m.onMediaState(mediaState(true));
        m.resetMedia();
        QVERIFY(!m.mediaActive() && !m.m_mediaAvailable && m.m_mediaTitle.isEmpty() && m.mediaArtUrl().isEmpty());
    }

    void cablePullKeepsTheSongUntilTheReconnectGivesUp()
    {
        // The music plays on through a link drop: the music page must not
        // flash "nothing playing" while the session comes back over Wi-Fi
        PhoneMirrorManager m;
        m.onMediaState(mediaState(true));
        m.m_hadStream = true;
        m.setSessionState(QStringLiteral("streaming"));
        QSignalSpy active(&m, &PhoneMirrorManager::mediaActiveChanged);

        m.onDisconnected(ScrcpyClient::disconnectedMessage(USB));   // cable pulled
        QCOMPARE(m.sessionState(), QStringLiteral("reconnecting"));
        QVERIFY(m.mediaActive() && m.m_mediaPlaying && m.m_mediaTitle == "Song");
        m.onDisconnected(m.describeDeviceState("none"));            // a retry found no phone yet
        QVERIFY(m.mediaActive());
        m.setSessionState(QStringLiteral("streaming"));             // back over Wi-Fi
        QVERIFY(m.mediaActive());
        QCOMPARE(active.count(), 0);

        m.onDisconnected(ScrcpyClient::disconnectedMessage(USB));
        m.m_reconnectSinceMs -= PhoneMirrorManager::kReconnectGraceMs + 1;
        m.onRetryTick();                                            // gave up
        QCOMPARE(m.sessionState(), QStringLiteral("waitingForPhone"));
        QVERIFY(!m.mediaActive() && m.m_mediaTitle.isEmpty());
        m.stopScrcpy();
    }

    // ── Probes and notices ──────────────────────────────────────────────
    void devicesListAndNotices()
    {
        PhoneMirrorManager m;
        m.applyProbe({{{USB, "device"}, {TCP, "device"}}, "device", "", {{USB, "SM-S918B"}, {TCP, ""}}});
        QVariantList rows = m.deviceList();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toMap().value("serial").toString(), USB);
        QCOMPARE(rows[1].toMap().value("serial").toString(), TCP);
        QCOMPARE(rows[0].toMap().value("model").toString(), QStringLiteral("SM-S918B"));
        QVERIFY(!rows[0].toMap().value("wireless").toBool() && rows[1].toMap().value("wireless").toBool());
        QVERIFY(!rows[0].toMap().value("selected").toBool() && !rows[1].toMap().value("selected").toBool());
        // Models are kept when a later probe (one phone) does not fetch them
        m.applyProbe({{{USB, "device"}}, "device", "", {}});
        QCOMPARE(m.deviceList()[0].toMap().value("model").toString(), QStringLiteral("SM-S918B"));

        m.onServerLog("[server] WARN: Audio disabled: it is not supported before Android 11");
        m.onServerLog("[server] WARN: Audio disabled: it is not supported before Android 11");
        QCOMPARE(m.mirrorNotice(), PhoneMirrorManager::noticeText("no-audio"));
        m.onServerLog("[server] ERROR: Capture/encoding error: boom");
        QCOMPARE(m.m_fatalCause, QStringLiteral("encoder"));
        QCOMPARE(m.mirrorNotice(), PhoneMirrorManager::noticeText("no-audio"));
        m.dismissNotice();
        QCOMPARE(m.mirrorNotice(), QString());
    }

    void encoderFailureRetriesOnceInSoftware()
    {
        PhoneMirrorManager m;
        QSignalSpy errors(&m, &PhoneMirrorManager::scrcpyError);
        const QString cable = QStringLiteral("Phone disconnected. Reconnect the USB cable.");
        m.m_serial = USB;
        m.m_sessionSdk = 34;
        m.m_fatalCause = "encoder";
        m.m_fatalCauseAtMs = QDateTime::currentMSecsSinceEpoch();
        m.onDisconnected(cable);
        QCOMPARE(m.m_softwareEncoderSerial, USB);   // a restart in software mode is queued
        QVERIFY(errors.isEmpty());
        // Software encoder failed too: a final error, not the retry-forever "disconnected"
        m.m_fatalCause = "encoder";
        m.m_fatalCauseAtMs = QDateTime::currentMSecsSinceEpoch();
        m.onDisconnected(cable);
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.last().at(0).toString().contains("even in software mode"));
        // An old encoder line is not the cause of a later link drop
        m.m_fatalCause = "encoder";
        m.m_fatalCauseAtMs = QDateTime::currentMSecsSinceEpoch() - 10000;
        m.onDisconnected(cable);
        QCOMPARE(errors.last().at(0).toString(), cable);
    }

#ifdef Q_OS_UNIX
    // ── The async flow (fake adb) ───────────────────────────────────────
    void getDeviceStateNeverBlocksAndCoalesces()
    {
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        setDevices(USB + "\tunauthorized\n");
        gate(true);   // the first probe hangs, like a dead adb
        QStringList changes;
        connect(&m, &PhoneMirrorManager::deviceStateChanged, this, [&] { changes << m.deviceState(); });
        QElapsedTimer t;
        t.start();
        QCOMPARE(m.getDeviceState(), QStringLiteral("none"));   // cached, immediately
        for (int i = 0; i < 5; ++i)
            m.getDeviceState();   // asked again mid-probe: one re-run, not five
        QVERIFY(t.elapsed() < 500);
        gate(false);
        QTRY_COMPARE_WITH_TIMEOUT(m.deviceState(), QStringLiteral("unauthorized"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!m.m_probeRunning && adbCalls("devices") == 2, 5000);
        QTest::qWait(200);
        QCOMPARE(adbCalls("devices"), 2);
        QCOMPARE(changes, QStringList{"unauthorized"});
    }

    // An adb that overruns its time is ended and waited for where it was
    // started: one still running when its QProcess goes logs "QProcess:
    // Destroyed while process ... is still running" (0.9.6-rc5 / rc6, Windows)
    void anOverdueAdbIsEndedBeforeItsProcessObjectGoes()
    {
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        gate(true);   // `adb devices` hangs
        static QStringList warnings;
        warnings.clear();
        const QtMessageHandler previous = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext &, const QString &text) {
                if (type == QtWarningMsg)
                    warnings << text;
            });
        QElapsedTimer t;
        t.start();
        int code = 0;
        const QString full = m.runAdbFull({QStringLiteral("devices")}, 150, &code);
        const QString plain = m.runAdb({QStringLiteral("devices")}, 150);
        const qint64 took = t.elapsed();
        qInstallMessageHandler(previous);
        gate(false);
        QCOMPARE(code, -1);
        QVERIFY(full.isEmpty() && plain.isEmpty());
        QVERIFY2(took < 3000, qPrintable(QString::number(took)));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void startWithoutPhoneReportsOffTheGuiThread()
    {
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec: startScrcpy() reports the install text instead");
        setDevices(USB + "\tno-permissions\n");
        QSignalSpy errors(&m, &PhoneMirrorManager::scrcpyError);
        m.startScrcpy();
        QVERIFY(m.m_isStarting);   // returned at once, the probe is pending
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
        QVERIFY(errors.first().at(0).toString().contains("USB permissions"));
        QCOMPARE(m.deviceState(), QStringLiteral("no-permissions"));
        QVERIFY(!m.m_isStarting);
    }

    void stopWhileProbingDropsTheStart()
    {
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        setDevices(USB + "\tdevice\n");
        gate(true);
        m.startScrcpy();
        m.stopScrcpy();
        gate(false);
        QTRY_VERIFY_WITH_TIMEOUT(adbCalls("devices") >= 1, 5000);
        QTest::qWait(500);   // the probe's continuation has run
        QVERIFY(m.m_client == nullptr);
        QVERIFY(m.m_connectionType.isEmpty());
    }

    void cablePullReattachesOverWifiToTheSamePhone()
    {
        // A USB session dropped mid-stream leaves its server (and the virtual
        // display with the user's apps) alive on the phone; failing over to the
        // same phone's Wi-Fi serial must reattach to it, not start a second one
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        m.m_serial = USB;
        m.m_persistScid = "2cac176c";
        m.m_persistPhoneId = "R5CT1";
        m.m_persistUntilMs = QDateTime::currentMSecsSinceEpoch() + 30000;
        m.m_startGeneration = 7;
        PhoneMirrorManager::StartPrep prep;
        prep.probe = {{{TCP, "device"}}, "device", "", {}};
        prep.serial = TCP;
        prep.sdk = 36;
        prep.phoneId = "R5CT1";   // same phone, other transport
        m.continueStart(prep, 7);
        QVERIFY(m.m_attaching);
        QVERIFY(m.m_client);
        QTRY_VERIFY_WITH_TIMEOUT(adbCalls("logcat") == 1, 5000);   // attached: follows the running server's log
        QCOMPARE(adbCalls("app_process"), 0);                      // no second server

        // Picking another phone never reattaches to this one's session
        m.selectDevice(USB2);
        QCOMPARE(m.m_persistScid, QString());
        m.stopScrcpy();
    }

    void usbPhoneIsMadeReachableOverWifi()
    {
        // The cable path: a phone on USB gets its Wireless debugging turned
        // on and is connected over Wi-Fi with no pairing
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        const QString state = m_tmp.filePath("wifi-state");
        const QString mdns = m_tmp.filePath("mdns");
        QFile::remove(state);
        QFile f(mdns);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("adb-R5CT1234ABC-AbCdEf\t_adb-tls-connect._tcp\t" + TCP.toUtf8() + "\n");
        f.close();
        qputenv("FAKE_ADB_WIFI_STATE", state.toUtf8());
        qputenv("FAKE_ADB_MDNS", mdns.toUtf8());
        auto restore = qScopeGuard([] {
            for (const char *name : {"FAKE_ADB_WIFI_STATE", "FAKE_ADB_MDNS", "FAKE_ADB_WLAN_IP", "FAKE_ADB_WIFI_REFUSED"})
                qunsetenv(name);
        });

        // A phone with no Wi-Fi is left alone
        QCOMPARE(m.enableWirelessDebugging(USB), QString());
        QCOMPARE(adbCalls("put"), 0);

        // The setting is written, the port found over mDNS, the phone connected
        qputenv("FAKE_ADB_WLAN_IP", "192.168.1.5");
        QCOMPARE(m.enableWirelessDebugging(USB), TCP);
        QCOMPARE(adbCalls("put"), 1);
        QCOMPARE(adbCalls("connect"), 1);
        // Already on: not written again
        QCOMPARE(m.enableWirelessDebugging(USB), TCP);
        QCOMPARE(adbCalls("put"), 1);

        // A phone that does not keep the setting: no search, no connect
        QFile::remove(state);
        qputenv("FAKE_ADB_WIFI_REFUSED", "1");
        const int searches = adbCalls("mdns");
        QCOMPARE(m.enableWirelessDebugging(USB), QString());
        QCOMPARE(adbCalls("mdns"), searches);
        qunsetenv("FAKE_ADB_WIFI_REFUSED");

        // From a session start: in the background, the address remembered
        QSignalSpy remembered(&m, &PhoneMirrorManager::wirelessAddressChanged);
        int reads = adbCalls("ip");
        m.startWirelessDebugging(USB, 29);   // Android 10 has no Wireless debugging
        QTest::qWait(300);
        QCOMPARE(adbCalls("ip"), reads);
        m.startWirelessDebugging(USB, 37);
        QTRY_COMPARE_WITH_TIMEOUT(remembered.count(), 1, 5000);
        QCOMPARE(m.wirelessAddress(), TCP);
        // ... and not again for the same phone moments later (a restart for a new size)
        reads = adbCalls("ip");
        m.startWirelessDebugging(USB, 37);
        QTest::qWait(300);
        QCOMPARE(adbCalls("ip"), reads);
        QCOMPARE(remembered.count(), 1);
    }

    void carWifiWords()
    {
        using M = PhoneMirrorManager;
        QCOMPARE(M::shellQuote("OCTAVE-Jeep"), QString("'OCTAVE-Jeep'"));
        QCOMPARE(M::shellQuote("Rob's car; rm -rf /"), QString("'Rob'\\''s car; rm -rf /'"));
        QCOMPARE(M::carWifiCommand({"My Car", "pass word"}, true).join(' '),
                 QString("cmd wifi connect-network 'My Car' wpa2 'pass word' -r persistent"));
        QCOMPARE(M::carWifiCommand({"Open", ""}, false).join(' '), QString("cmd wifi connect-network 'Open' open"));

        const QList<M::Subnet> nets{{QHostAddress("192.168.0.14"), 24}, {QHostAddress("10.42.0.1"), 24}};
        QVERIFY(M::onSubnet("10.42.0.30", nets));
        QVERIFY(M::onSubnet("192.168.0.117", nets));
        QVERIFY(!M::onSubnet("192.168.1.5", nets));
        QVERIFY(!M::onSubnet("", nets));

        // Those on a network this machine is on, in order; the most recent one is always tried
        const QStringList known{"100.64.0.9:5555", "10.42.0.30:5555", "172.16.0.4:5555", "192.168.0.117:40001"};
        QCOMPARE(M::reconnectOrder(known, nets),
                 (QStringList{"10.42.0.30:5555", "192.168.0.117:40001", "100.64.0.9:5555"}));
        QCOMPARE(M::reconnectOrder({"10.42.0.30:5555"}, {}), QStringList{"10.42.0.30:5555"});
        QCOMPARE(M::reconnectOrder({}, nets), QStringList());

        // This machine's own access point for the car's network, from `iw dev`
        const QString iw = "phy#0\n\tInterface ap0\n\t\tifindex 5\n\t\tssid OCTAVE-Jeep\n\t\ttype AP\n"
                           "\t\tchannel 44 (5220 MHz)\n\tInterface wlan0\n\t\tssid Bigfoots Cave\n\t\ttype managed\n"
                           "\tInterface wlan1\n\t\tssid OCTAVE-Jeep\n\t\ttype managed\n";
        QCOMPARE(M::accessPointInterfaces(iw, "OCTAVE-Jeep"), QStringList{"ap0"});
        QCOMPARE(M::accessPointInterfaces(iw, "Bigfoots Cave"), QStringList());
        QCOMPARE(M::accessPointInterfaces(QString(), "OCTAVE-Jeep"), QStringList());
    }

    void aPhoneOnACableIsPutOnTheCarWifi()
    {
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        const QString joined = m_tmp.filePath("car-join");
        QFile::remove(joined);
        qputenv("FAKE_ADB_CAR_JOIN", joined.toUtf8());
        qputenv("FAKE_ADB_CAR_IP", "10.42.0.30");
        auto restore = qScopeGuard([] {
            for (const char *name : {"FAKE_ADB_CAR_JOIN", "FAKE_ADB_CAR_IP", "FAKE_ADB_WLAN_IP", "FAKE_ADB_WIFI_NO_R"})
                qunsetenv(name);
        });
        const QList<PhoneMirrorManager::Subnet> nets{{QHostAddress("192.168.1.2"), 24}, {QHostAddress("10.42.0.1"), 24}};
        const PhoneMirrorManager::CarWifi car{"OCTAVE-Jeep", "secret pass"};

        // No car Wi-Fi set: the phone is not even asked for its address
        QVERIFY(!m.joinCarWifi(USB, {}, nets, {}));
        QCOMPARE(adbCalls("ip"), 0);

        // A phone this machine already reaches stays on its network
        qputenv("FAKE_ADB_WLAN_IP", "192.168.1.5");
        QVERIFY(!m.joinCarWifi(USB, car, nets, {}));
        QCOMPARE(adbCalls("connect-network"), 0);

        // ... unless this machine hosts the car's Wi-Fi (the head unit in the
        // driveway): then the phone goes on it, off the Wi-Fi at home
        const QList<PhoneMirrorManager::Subnet> carNets{{QHostAddress("10.42.0.1"), 24}};
        QVERIFY(m.joinCarWifi(USB, car, nets, carNets));
        QCOMPARE(adbCalls("connect-network"), 1);
        // Already on it: left alone
        QVERIFY(!m.joinCarWifi(USB, car, nets, carNets));
        QCOMPARE(adbCalls("connect-network"), 1);
        QFile::remove(joined);
        QFile::remove(m_tmp.filePath("adb.log"));

        // A phone on a network that is not ours (or on none) is moved, with a fixed address
        qputenv("FAKE_ADB_WLAN_IP", "172.16.0.9");
        QVERIFY(m.joinCarWifi(USB, car, nets, {}));
        QCOMPARE(adbCalls("connect-network"), 1);
        QCOMPARE(adbCalls("persistent"), 1);
        QCOMPARE(adbCalls("'OCTAVE-Jeep'"), 1);
        QCOMPARE(m.phoneWifiAddress(USB), QString("10.42.0.30"));

        // An Android without -r: once more without it
        QFile::remove(joined);
        qunsetenv("FAKE_ADB_WLAN_IP");
        qputenv("FAKE_ADB_WIFI_NO_R", "1");
        QVERIFY(m.joinCarWifi(USB, car, nets, {}));
        QCOMPARE(adbCalls("connect-network"), 3);
        QCOMPARE(adbCalls("persistent"), 2);

        // From a session start: joined first, then made reachable there and remembered
        QFile::remove(joined);
        qunsetenv("FAKE_ADB_WIFI_NO_R");
        const QString state = m_tmp.filePath("wifi-state");
        const QString mdns = m_tmp.filePath("mdns");
        QFile::remove(state);
        QFile f(mdns);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("adb-R5CT1234ABC-AbCdEf\t_adb-tls-connect._tcp\t10.42.0.30:33659\n");
        f.close();
        qputenv("FAKE_ADB_WIFI_STATE", state.toUtf8());
        qputenv("FAKE_ADB_MDNS", mdns.toUtf8());
        auto restoreWifi = qScopeGuard([] { qunsetenv("FAKE_ADB_WIFI_STATE"); qunsetenv("FAKE_ADB_MDNS"); });
        m.m_testSubnets = nets;
        m.m_testSubnetsSet = true;
        m.setCarWifi(" OCTAVE-Jeep ", "secret pass");
        QSignalSpy remembered(&m, &PhoneMirrorManager::wirelessAddressChanged);
        m.startWirelessDebugging(USB, 37);
        QTRY_COMPARE_WITH_TIMEOUT(remembered.count(), 1, 8000);
        QCOMPARE(m.wirelessAddress(), QString("10.42.0.30:33659"));
        QCOMPARE(adbCalls("connect-network"), 4);
    }

    void pluggingInWhileMirroringOverWifiMovesThePhone()
    {
        // In the driveway: mirroring over the Wi-Fi at home, then the cable goes in
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        const QString joined = m_tmp.filePath("car-join");
        const QString state = m_tmp.filePath("wifi-state");
        const QString mdns = m_tmp.filePath("mdns");
        QFile::remove(joined);
        QFile::remove(state);
        QFile f(mdns);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("adb-R5CT1234ABC-AbCdEf\t_adb-tls-connect._tcp\t10.42.0.30:33659\n");
        f.close();
        qputenv("FAKE_ADB_CAR_JOIN", joined.toUtf8());
        qputenv("FAKE_ADB_CAR_IP", "10.42.0.30");
        qputenv("FAKE_ADB_WLAN_IP", "192.168.1.5");
        qputenv("FAKE_ADB_WIFI_STATE", state.toUtf8());
        qputenv("FAKE_ADB_MDNS", mdns.toUtf8());
        auto restore = qScopeGuard([] {
            for (const char *name : {"FAKE_ADB_CAR_JOIN", "FAKE_ADB_CAR_IP", "FAKE_ADB_WLAN_IP", "FAKE_ADB_WIFI_STATE",
                                     "FAKE_ADB_MDNS"})
                qunsetenv(name);
        });
        m.m_testSubnets = {{QHostAddress("192.168.1.2"), 24}, {QHostAddress("10.42.0.1"), 24}};
        m.m_testCarSubnets = {{QHostAddress("10.42.0.1"), 24}};
        m.m_testSubnetsSet = true;
        m.m_serial = TCP;
        m.m_ready = true;
        setDevices(TCP + "\tdevice\n" + USB + "\tdevice\n");

        // No Car Wi-Fi: a running session is left as it is
        m.cablePluggedInOnWifi();
        QTest::qWait(300);
        QCOMPARE(adbCalls("devices"), 0);

        m.setCarWifi("OCTAVE-Jeep", "secret pass");
        QSignalSpy remembered(&m, &PhoneMirrorManager::wirelessAddressChanged);
        m.cablePluggedInOnWifi();
        QTRY_COMPARE_WITH_TIMEOUT(remembered.count(), 1, 8000);
        QCOMPARE(adbCalls("connect-network"), 1);
        QCOMPARE(m.wirelessAddress(), QString("10.42.0.30:33659"));
        QCOMPARE(adbCalls("app_process"), 0);   // the session itself was not restarted
    }

    void theWatcherIsLeftOnThePhone()
    {
        using M = PhoneMirrorManager;
        // The script: the quoted name and password, plain sh, both forms of the join
        const QString script = M::watcherScript({"Rob's Jeep", "p%1ss w%2rd"});
        QVERIFY(script.startsWith("#!/system/bin/sh\n"));
        QVERIFY(script.contains("name='Rob'\\''s Jeep'\n"));
        QVERIFY(script.contains("cmd wifi connect-network 'Rob'\\''s Jeep' wpa2 'p%1ss w%2rd' -r persistent >/dev/null"));
        QVERIFY(script.contains("|| cmd wifi connect-network 'Rob'\\''s Jeep' wpa2 'p%1ss w%2rd' >/dev/null"));
        QVERIFY(script.contains("[ -n \"$age\" ] && [ -n \"$rssi\" ] || continue\n"));   // mksh: "" counts as 0
        QVERIFY(script.contains("-lt 120 ]"));
        QVERIFY(script.contains("-ge -70 ]"));
        QVERIFY(script.contains("sleep 10\n"));
        QVERIFY(!script.contains("awk"));
        // It never acts on a phone whose Wi-Fi is off or on another network
        QVERIFY(script.contains("*\"Wifi is disabled\"*|*\"Wifi is connected to\"*) ;;"));
#ifdef Q_OS_UNIX
        // ... and is a script the shell accepts
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write(script.toUtf8());
        file.close();
        QCOMPARE(QProcess::execute("sh", {"-n", file.fileName()}), 0);
        // Android's shell is mksh, which rejects what bash and dash accept: an
        // unquoted "(" inside ${...} kept the first version from ever running
        QVERIFY(!script.contains("%%("));
        const QString mksh = QStandardPaths::findExecutable("mksh");
        if (!mksh.isEmpty())
            QCOMPARE(QProcess::execute(mksh, {"-n", file.fileName()}), 0);
#endif

        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        const QString watcher = m_tmp.filePath("watcher");
        QFile::remove(watcher);
        qputenv("FAKE_ADB_WATCHER", watcher.toUtf8());
        auto restore = qScopeGuard([] { qunsetenv("FAKE_ADB_WATCHER"); });
        const M::CarWifi car{"OCTAVE-Jeep", "secret pass"};

        // Only the machine that hosts the network, and only with Car Wi-Fi set
        QCOMPARE(m.ensureWatcher(USB, car, false, false), QString());
        QCOMPARE(m.ensureWatcher(USB, {}, true, false), QString());
        QCOMPARE(adbCalls("push"), 0);

        // Copied and started once; found running after that
        QCOMPARE(m.ensureWatcher(USB, car, true, false), QString("started"));
        QCOMPARE(adbCalls("push"), 1);
        QCOMPARE(m.ensureWatcher(USB, car, true, false), QString("running"));
        QCOMPARE(adbCalls("push"), 1);
        // A new password: replaced
        QCOMPARE(m.ensureWatcher(USB, {"OCTAVE-Jeep", "other"}, true, false), QString("started"));
        QCOMPARE(adbCalls("push"), 2);

        // Car Wi-Fi cleared: taken off the phone at the next session start
        m.m_testSubnetsSet = true;
        m.m_testCarSubnets = {{QHostAddress("10.42.0.1"), 24}};
        m.setCarWifi("OCTAVE-Jeep", "other");
        m.setCarWifi("", "");
        QVERIFY(m.m_watcherRemovalPending);
        m.startWatcher(USB);
        QTRY_VERIFY_WITH_TIMEOUT(!m.m_watcherRemovalPending, 5000);
        QVERIFY(!QFile::exists(watcher));
        // ... and with it set, started from a session start
        m.setCarWifi("OCTAVE-Jeep", "other");
        m.startWatcher(USB);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(watcher), 5000);
    }

    void thePhoneIsRememberedOnEveryNetwork()
    {
        PhoneMirrorManager m;   // no adb: nothing is tried
        m.setKnownWireless({"192.168.0.117:5555", "10.42.0.30:33245"});
        m.setWirelessAddress("192.168.0.117:5555");
        QCOMPARE(m.knownWireless(), (QStringList{"192.168.0.117:5555", "10.42.0.30:33245"}));

        // Reached on the car's network at a new port: first, and the old port there is gone
        QSignalSpy changed(&m, &PhoneMirrorManager::wirelessAddressChanged);
        m.rememberWireless("10.42.0.30:33659");
        QCOMPARE(changed.count(), 1);
        QCOMPARE(m.wirelessAddress(), QString("10.42.0.30:33659"));
        QCOMPARE(m.knownWireless(), (QStringList{"10.42.0.30:33659", "192.168.0.117:5555"}));
        m.rememberWireless("10.42.0.30:33659");
        QCOMPARE(changed.count(), 1);

        // Never more than kKnownWirelessMax
        for (int i = 1; i <= 10; ++i)
            m.rememberWireless(QStringLiteral("10.0.%1.2:5555").arg(i));
        QCOMPARE(m.knownWireless().size(), PhoneMirrorManager::kKnownWirelessMax);
        QCOMPARE(m.knownWireless().first(), QString("10.0.10.2:5555"));

        // Forget drops them all
        m.forgetWireless();
        QCOMPARE(m.wirelessAddress(), QString());
        QCOMPARE(m.knownWireless(), QStringList());
    }

    void reconnectTriesEachNetworkThisMachineIsOn()
    {
        // Home address saved first, but the phone is on the car's Wi-Fi now
        PhoneMirrorManager m;
        m.m_testSubnets = {{QHostAddress("192.168.0.14"), 24}, {QHostAddress("10.42.0.1"), 24}};
        m.m_testSubnetsSet = true;
        m.setKnownWireless({"192.168.0.117:5555", "10.42.0.30:5555"});
        m.setWirelessAddress("192.168.0.117:5555");
        qputenv("FAKE_ADB_CONNECT_ONLY", "10.42.0.30:5555");
        auto restore = qScopeGuard([] { qunsetenv("FAKE_ADB_CONNECT_ONLY"); });
        m.m_adbPath = kFakeAdb;
        QSignalSpy changed(&m, &PhoneMirrorManager::wirelessAddressChanged);
        m.maybeReconnectWireless();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 8000);
        QCOMPARE(m.wirelessAddress(), QString("10.42.0.30:5555"));
        QCOMPARE(m.knownWireless(), (QStringList{"10.42.0.30:5555", "192.168.0.117:5555"}));
        QTRY_VERIFY(!m.m_autoConnectRunning);
    }

    void pluggingAPhoneInStartsMirroring()
    {
#ifndef Q_OS_LINUX
        QSKIP("the USB look is sysfs, Linux only");
#else
        // Found without adb: a USB interface of class ff / 42 / 01
        const QString usb = m_tmp.filePath("usb");
        auto interface = [&](const QString &name, const char *cls, const char *sub, const char *proto) {
            QVERIFY(QDir().mkpath(usb + "/" + name));
            const QPair<const char *, const char *> files[] = {
                {"bInterfaceClass", cls}, {"bInterfaceSubClass", sub}, {"bInterfaceProtocol", proto}};
            for (const auto &file : files) {
                QFile f(usb + "/" + name + "/" + file.first);
                QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
                f.write(QByteArray(file.second) + "\n");
            }
        };
        auto unplug = qScopeGuard([&] { QDir(usb).removeRecursively(); QDir().mkpath(usb); });
        interface("1-6:1.0", "06", "01", "01");   // MTP: a phone with USB debugging off
        QVERIFY(QDir().mkpath(usb + "/1-6"));     // the device itself
        QVERIFY(!PhoneMirrorManager::adbInterfaceOnUsb(usb));

        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        m.setViewActive(true);
        QVERIFY(m.m_usbWatchTimer.isActive());
        m.checkUsbPhone();
        QCOMPARE(m.sessionState(), QStringLiteral("idle"));
        QCOMPARE(adbCalls("devices"), 0);   // no adb command with no such phone

        interface("1-6:1.3", "ff", "42", "01");
        QVERIFY(PhoneMirrorManager::adbInterfaceOnUsb(usb));
        m.checkUsbPhone();
        QCOMPARE(m.sessionState(), QStringLiteral("starting"));
        QTRY_COMPARE_WITH_TIMEOUT(m.sessionState(), QStringLiteral("waitingForPhone"), 5000);   // the fake adb lists none
        // Stopped with the phone still plugged in: not started again
        m.stopScrcpy();
        QCOMPARE(m.sessionState(), QStringLiteral("idle"));
        m.checkUsbPhone();
        QCOMPARE(m.sessionState(), QStringLiteral("idle"));
        // Unplugged and plugged in again: started
        QDir(usb + "/1-6:1.3").removeRecursively();
        m.checkUsbPhone();
        interface("1-6:1.3", "ff", "42", "01");
        m.checkUsbPhone();
        QCOMPARE(m.sessionState(), QStringLiteral("starting"));
        m.stopScrcpy();
        // Hidden: no watching
        m.setViewActive(false);
        QVERIFY(!m.m_usbWatchTimer.isActive());
#endif
    }

    void aRunningAdbServerStartsMirroringAtLaunch()
    {
        // With an adb server already up, asking it for phones starts nothing
        // new: the first look after launch starts the session (a phone that
        // is connected over Wi-Fi), later looks do not
        QVERIFY(!PhoneMirrorManager::adbServerRunning());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        qputenv("ANDROID_ADB_SERVER_PORT", QByteArray::number(server.serverPort()));
        auto restore = qScopeGuard([] { qputenv("ANDROID_ADB_SERVER_PORT", "1"); });
        QVERIFY(PhoneMirrorManager::adbServerRunning());

        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        m.setViewActive(true);
        m.checkUsbPhone();
        QCOMPARE(m.sessionState(), QStringLiteral("starting"));
        m.stopScrcpy();
        m.checkUsbPhone();
        QCOMPARE(m.sessionState(), QStringLiteral("idle"));

        // No server at launch: nothing, and a server that comes up later
        // (our own, after Connect Phone) does not start it either
        qputenv("ANDROID_ADB_SERVER_PORT", "1");
        QTest::qWait(300);   // the first manager's probe, if it got that far
        const int asked = adbCalls("devices");
        PhoneMirrorManager fresh;
        fresh.m_adbPath = kFakeAdb;
        fresh.setViewActive(true);
        fresh.checkUsbPhone();
        QCOMPARE(fresh.sessionState(), QStringLiteral("idle"));
        qputenv("ANDROID_ADB_SERVER_PORT", QByteArray::number(server.serverPort()));
        fresh.checkUsbPhone();
        QCOMPARE(fresh.sessionState(), QStringLiteral("idle"));
        QTest::qWait(300);
        QCOMPARE(adbCalls("devices"), asked);   // adb was not asked
    }

    void anotherPhoneInThePersistWindowStartsFresh()
    {
        // Phone A dropped; phone B is picked automatically before A's session
        // expires. Reattaching would try A's scid on B and burn the 4 s attach
        // deadline, so B gets a fresh server straight away.
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        m.m_persistScid = "2cac176c";
        m.m_persistPhoneId = "PHONE_A";
        m.m_persistUntilMs = QDateTime::currentMSecsSinceEpoch() + 30000;
        m.m_startGeneration = 7;
        PhoneMirrorManager::StartPrep prep;
        prep.probe = {{{USB2, "device"}}, "device", "", {}};
        prep.serial = USB2;
        prep.sdk = 36;
        prep.phoneId = "PHONE_B";
        m.continueStart(prep, 7);
        QVERIFY(!m.m_attaching);
        QCOMPARE(m.m_persistScid, QString());
        QTRY_VERIFY_WITH_TIMEOUT(adbCalls("app_process") == 1, 5000);   // a new server
        QCOMPARE(adbCalls("logcat"), 0);
        m.stopScrcpy();
    }

    // ── Session state and recovery (the manager's, not the view's) ──────
    // The view used to poll adb and restart from two timers and a 10 s
    // grace window; the manager owns that now, gated by setViewActive.

    void noPhoneWaitsAndRestartsWhenItAppears()
    {
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        QCOMPARE(m.sessionState(), QStringLiteral("idle"));
        QStringList states;
        connect(&m, &PhoneMirrorManager::sessionStateChanged, this, [&] { states << m.sessionState(); });
        m.setViewActive(true);
        m.startScrcpy();
        QTRY_COMPARE_WITH_TIMEOUT(m.sessionState(), QStringLiteral("waitingForPhone"), 5000);
        QCOMPARE(m.lastErrorKind(), QStringLiteral("noPhone"));
        QCOMPARE(states, (QStringList{"starting", "waitingForPhone"}));

        setDevices(USB + "\tdevice\n");   // the phone is plugged in
        QTRY_VERIFY_WITH_TIMEOUT(adbCalls("app_process") == 1, 8000);   // restarted on its own
        QVERIFY(states.contains("starting"));
        m.stopScrcpy();
        QCOMPARE(m.sessionState(), QStringLiteral("idle"));
    }

    void noProbesWhileTheViewIsHidden()
    {
        // On the Pi every probe spawns adb: nothing while nobody is looking
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        PhoneMirrorManager::StartPrep prep;   // a start found no phone
        prep.probe.state = QStringLiteral("none");
        m.continueStart(prep, m.m_startGeneration);
        QCOMPARE(m.sessionState(), QStringLiteral("waitingForPhone"));
        QVERIFY(!m.m_retryTimer.isActive());
        QTest::qWait(2 * PhoneMirrorManager::kWaitPollMs + 200);
        QCOMPARE(adbCalls("devices"), 0);
        m.setViewActive(true);
        QVERIFY(m.m_retryTimer.isActive());
        QTRY_VERIFY_WITH_TIMEOUT(adbCalls("devices") >= 1, 2 * PhoneMirrorManager::kWaitPollMs + 1000);
        m.setViewActive(false);
        QVERIFY(!m.m_retryTimer.isActive());
    }

    void dropMidStreamReconnectsQuietlyThenGivesUp()
    {
        PhoneMirrorManager m;
        m.setViewActive(true);
        m.m_hadStream = true;   // a stream was up (onConnected needs a client)
        m.setSessionState(QStringLiteral("streaming"));
        QSignalSpy states(&m, &PhoneMirrorManager::sessionStateChanged);

        m.reportError(ScrcpyClient::disconnectedMessage(USB));      // cable pulled
        QCOMPARE(m.sessionState(), QStringLiteral("reconnecting"));   // hold the last frame
        QCOMPARE(m.m_retryTimer.interval(), PhoneMirrorManager::kReconnectPollMs);
        m.reportError(m.describeDeviceState("none"));              // a retry found no phone yet
        QCOMPARE(m.sessionState(), QStringLiteral("reconnecting"));   // still inside the grace

        m.m_reconnectSinceMs -= PhoneMirrorManager::kReconnectGraceMs + 1;   // ... time passes
        m.onRetryTick();
        QCOMPARE(m.sessionState(), QStringLiteral("waitingForPhone"));   // the error page
        QCOMPARE(m.m_retryTimer.interval(), PhoneMirrorManager::kWaitPollMs);
        QVERIFY(!m.m_hadStream);
        QCOMPARE(states.count(), 2);
    }

    void handingTheSinkOverKeepsTheStream()
    {
        // A hand-over (Home's widget to the Media Room's art corner) asks the
        // phone for nothing: a fresh-frame request restarts its encoder, and
        // on a locked phone a restart ended the stream (2026-11-26). The same
        // sink again changes nothing
        PhoneMirrorManager m;
        QVideoSink home, media;
        QSignalSpy changed(&m, &PhoneMirrorManager::videoSinkChanged);
        m.setVideoSink(&home);
        m.setVideoSink(&home);                  // the same view again
        QCOMPARE(changed.count(), 1);
        m.setVideoSink(nullptr);                // Home gives it up ...
        m.setVideoSink(&media);                 // ... the Media Room takes it
        QCOMPARE(m.videoSink(), &media);
        QCOMPARE(changed.count(), 3);
    }

    void aDestroyedSinkIsLetGo()
    {
        // The Media Room's art corner switched away from the phone destroyed
        // its VideoOutput while it still held the sink, and the next frame
        // went to freed memory (crash 2026-11-26): the manager lets go of a
        // destroyed sink and says so, so another view can take the picture
        PhoneMirrorManager m;
        auto sink = std::make_unique<QVideoSink>();
        m.setVideoSink(sink.get());
        QSignalSpy changed(&m, &PhoneMirrorManager::videoSinkChanged);
        sink.reset();
        QCOMPARE(m.videoSink(), nullptr);
        QCOMPARE(changed.count(), 1);
        QVideoSink next;
        m.setVideoSink(&next);                  // no stale connection in the way
        QCOMPARE(m.videoSink(), &next);
    }

    void dropReconnectsAtOnceWithNoViewOpen()
    {
        // The phone keeps its music playing through a link drop for
        // kReconnectGraceMs (phone_server HeldAudioCapture): the reconnect
        // must not wait for a poll, nor for a page that shows the phone
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        m.m_hadStream = true;
        m.setSessionState(QStringLiteral("streaming"));   // on the music page: no view

        m.reportError(ScrcpyClient::disconnectedMessage(USB));      // cable pulled
        QCOMPARE(m.sessionState(), QStringLiteral("reconnecting"));
        QVERIFY(m.m_retryTimer.isActive());                          // retries with no view
        QTRY_VERIFY_WITH_TIMEOUT(adbCalls("devices") >= 1, PhoneMirrorManager::kReconnectPollMs / 2);
        m.stopScrcpy();
    }

    void setupErrorsFailWithoutRetrying()
    {
        PhoneMirrorManager m;
        m.setViewActive(true);
        m.reportError(QStringLiteral("Bundled scrcpy server could not be extracted"));
        QCOMPARE(m.sessionState(), QStringLiteral("failed"));
        QVERIFY(!m.m_retryTimer.isActive());
        m.m_hadStream = true;
        m.setSessionState(QStringLiteral("streaming"));
        m.reportError(PhoneMirrorManager::kEncoderGaveUp);   // not a link problem: no quiet reconnect
        QCOMPARE(m.sessionState(), QStringLiteral("failed"));
    }

    void deviceStateHandlerCannotStartASecondSession()
    {
        // PhoneMirrorView restarts the mirror from onDeviceStateChanged; the
        // probe result applied inside continueStart emits that signal, which
        // must not start a second session over the one being started (seen on
        // the Pi: two "Reattaching" starts 85 ms apart after a cable pull)
        PhoneMirrorManager m;
        m.m_adbPath = kFakeAdb;
        if (!m.nativeAvailable())
            QSKIP("built without libavcodec");
        connect(&m, &PhoneMirrorManager::deviceStateChanged, &m, &PhoneMirrorManager::startScrcpy);
        gate(true);
        m.startScrcpy();
        const int gen = m.m_startGeneration;
        PhoneMirrorManager::StartPrep prep;
        prep.probe = {{{USB, "unauthorized"}}, "unauthorized", "", {}};
        m.continueStart(prep, gen);
        QCOMPARE(m.m_startGeneration, gen);   // no second start was queued
        // Drop the still-pending first probe, then check nothing else ran
        m.stopScrcpy();
        gate(false);
        QTest::qWait(500);
        QCOMPARE(adbCalls("devices"), 1);
        QVERIFY(m.m_client == nullptr);
    }
#endif

private:
    void setDevices(const QString &rows)
    {
        QFile f(m_tmp.filePath("devices"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(rows.toUtf8());
    }

    void gate(bool closed)
    {
        if (closed) {
            QFile f(m_tmp.filePath("gate"));
            QVERIFY(f.open(QIODevice::WriteOnly));
        } else {
            QFile::remove(m_tmp.filePath("gate"));
        }
    }

    // How many logged adb calls mention `word`
    int adbCalls(const QString &word) const
    {
        QFile f(m_tmp.filePath("adb.log"));
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        int n = 0;
        for (const QString &line : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts))
            if (line.split(' ').contains(word) || (word == "app_process" && line.contains("app_process")))
                ++n;
        return n;
    }

    QTemporaryDir m_tmp;
};

QTEST_MAIN(TstPhoneMirror)
#include "tst_phonemirror.moc"
