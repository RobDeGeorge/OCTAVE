// PhoneMirrorManager without a phone: the adb parsing / state / serial-choice
// / server-log tables, Wi-Fi serials and messages, QR pairing, the phone media
// follow / hand-back rules, notices, the encoder fallback, and the async start
// flow with tests/cpp/fake-adb.sh standing in for adb.

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <memory>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <unistd.h>
#endif

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

    void resetClearsMedia()
    {
        PhoneMirrorManager m;
        m.onMediaState(mediaState(true));
        m.resetMedia();
        QVERIFY(!m.mediaActive() && !m.m_mediaAvailable && m.m_mediaTitle.isEmpty() && m.mediaArtUrl().isEmpty());
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
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(rows.toUtf8());
    }

    void gate(bool closed)
    {
        if (closed) {
            QFile f(m_tmp.filePath("gate"));
            f.open(QIODevice::WriteOnly);
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
