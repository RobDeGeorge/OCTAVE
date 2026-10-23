// The built app must start: `octave --smoke` loads Main.qml with every manager
// and exits 0 after five seconds. Unit-level checks can't catch a startup path
// that calls something missing (a removed slot once broke boot while every
// other test passed). With the dev tooling built, `--dev --profile` must boot
// too and answer on its command server, and `--service` must serve the
// service protocol in any build without the dev-only actions; `--headless`
// must bring the managers and the service up with no QML at all, and with
// the dev tooling `--headless --dev` must connect the real OBDManager to the
// simulated car and take the sim_* actions. `--headless --sim` must do the
// same through the `sim` object in every build, release included, on a
// throwaway config that leaves the caller's alone.

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString kApp = QStringLiteral(OCTAVE_APP_BINARY);
} // namespace

class TstBoot : public QObject
{
    Q_OBJECT

private slots:
    void appBootsAndExitsCleanly()
    {
        QTemporaryDir tmp;
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--smoke"});
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(out.contains("Smoke test: Main.qml loaded"), qPrintable(out.right(3000)));
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
    }

    void devProfileBootAnswersOnTheCommandServer()
    {
#ifndef OCTAVE_DEV_TOOLS
        QSKIP("built without OCTAVE_DEV_TOOLS");
#else
        QTemporaryDir tmp;
        const quint16 port = freePort();
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--dev", "--profile", "--command-port", QString::number(port), "--smoke"});
        QVERIFY(app.waitForStarted(10000));

        const QByteArray reply = request(app, port, R"({"action": "ping"})");
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QCOMPARE(reply.trimmed(), QByteArray(R"({"ok":true,"result":"pong"})"));
        QVERIFY2(out.contains("DevOverlay attached"), qPrintable(out.right(3000)));
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
#endif
    }

    void serviceBootServesTheProtocolOnly()
    {
        QTemporaryDir tmp;
        const quint16 port = freePort();
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--service", QString::number(port), "--smoke"});
        QVERIFY(app.waitForStarted(10000));

        const QByteArray hello = request(app, port, R"({"op": "hello", "id": 1})");
        const QByteArray actions = request(app, port, R"({"action": "list_actions"})");
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(hello.contains(R"("protocolVersion")"), qPrintable(hello + out.right(3000)));
        // No UI driving, screenshots or simulator without --profile
        QVERIFY2(actions.startsWith(R"({"ok":true)"), actions.constData());
        for (const char *dev : {"\"click\"", "\"screenshot\"", "\"navigate\"", "\"sim_throttle\""})
            QVERIFY2(!actions.contains(dev), actions.constData());
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
    }

    void headlessServesWithoutQml()
    {
        QTemporaryDir tmp;
        const quint16 port = freePort();
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--headless", "--service", QString::number(port), "--smoke"});
        QVERIFY(app.waitForStarted(10000));

        const QByteArray hello = request(app, port, R"({"op": "hello"})");
        const QByteArray get = request(app, port, R"({"op": "get", "object": "volumeController", "property": "percent"})");
        const QByteArray sim = request(app, port, R"({"op": "call", "object": "sim", "method": "setThrottle", "args": [50]})");
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(hello.contains(R"("headless":true)"), qPrintable(hello + out.right(3000)));
        QVERIFY2(get.startsWith(R"({"ok":true)"), get.constData());
        QVERIFY2(sim.contains(R"("result":false)"), sim.constData());   // no simulated car unless --sim
        QVERIFY2(out.contains("Smoke test: headless core up"), qPrintable(out.right(3000)));
        QVERIFY2(!out.contains("Main.qml"), qPrintable(out.right(3000)));   // no engine, no UI
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
    }

    void headlessDevRunsTheSimulatedCar()
    {
#ifndef OCTAVE_DEV_TOOLS
        QSKIP("built without OCTAVE_DEV_TOOLS");
#else
        QTemporaryDir tmp;
        const quint16 port = freePort();
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--headless", "--dev", "--service", QString::number(port)});
        QVERIFY(app.waitForStarted(10000));

        const QByteArray actions = request(app, port, R"({"action": "list_actions"})");
        QVERIFY2(actions.contains("\"sim_throttle\"") && actions.contains("\"sim_status\""), actions.constData());
        // The UI / perf actions need the engine: never under --headless
        for (const char *ui : {"\"click\"", "\"screenshot\"", "\"navigate\""})
            QVERIFY2(!actions.contains(ui), actions.constData());

        // The real OBDManager reaches the simulated adapter
        QByteArray connected;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 30000 && app.state() == QProcess::Running) {
            connected = request(app, port, R"({"op": "get", "object": "obdManager", "property": "connected"})");
            if (connected.contains(R"("result":true)"))
                break;
            QTest::qWait(500);
        }
        QVERIFY2(connected.contains(R"("result":true)"), qPrintable(connected + app.readAll().right(3000)));
        const QByteArray throttle = request(app, port, R"({"action": "sim_throttle", "args": {"value": 60}})");
        QVERIFY2(throttle.contains(R"("queued":"sim_throttle")"), throttle.constData());
        // A pairing code without the Settings page (frontend development)
        const QByteArray code = request(app, port, R"({"action": "sim_pairing_code"})");
        QVERIFY2(QRegularExpression(R"("code":"\d{6}")").match(QString::fromUtf8(code)).hasMatch(), code.constData());
        const QByteArray status = request(app, port, R"({"action": "sim_status"})");
        QVERIFY2(status.startsWith(R"({"ok":true)") && status.contains("\"imu\""), status.constData());

        app.terminate();   // SIGTERM: the normal clean shutdown
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(!out.contains("Main.qml"), qPrintable(out.right(3000)));
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
#endif
    }

    void headlessSimRunsInEveryBuild()
    {
        QTemporaryDir tmp;
        const quint16 port = freePort();
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--headless", "--sim", "--service", QString::number(port)});
        QVERIFY(app.waitForStarted(10000));

        const QByteArray active = request(app, port, R"({"op": "get", "object": "sim", "property": "active"})");
        QVERIFY2(active.contains(R"("result":true)"), active.constData());
        QByteArray connected;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 30000 && app.state() == QProcess::Running) {
            connected = request(app, port, R"({"op": "get", "object": "obdManager", "property": "connected"})");
            if (connected.contains(R"("result":true)"))
                break;
            QTest::qWait(500);
        }
        QVERIFY2(connected.contains(R"("result":true)"), connected.constData());
        const QByteArray throttle = request(app, port, R"({"op": "call", "object": "sim", "method": "setThrottle", "args": [60]})");
        QVERIFY2(throttle.contains(R"("result":true)"), throttle.constData());
        const QByteArray code = request(app, port, R"({"op": "call", "object": "sim", "method": "pairingCode", "args": []})");
        QVERIFY2(QRegularExpression(R"("code":"\d{6}")").match(QString::fromUtf8(code)).hasMatch(), code.constData());
        // The IMU moves from the start
        const QByteArray status = request(app, port, R"({"op": "call", "object": "sim", "method": "status", "args": []})");
        QVERIFY2(status.contains(R"("imu":{)") && status.contains(R"("on":true)"), status.constData());
        // Test tracks to play, with their tags
        const QByteArray play = request(app, port, R"({"op": "call", "object": "mediaManager", "method": "play_file", "args": ["01 - Idle.wav"]})");
        QVERIFY2(play.startsWith(R"({"ok":true)"), play.constData());
        QByteArray title;
        t.restart();
        while (t.elapsed() < 10000 && !title.contains(R"("result":"Idle")")) {
            title = request(app, port, R"({"op": "get", "object": "nowPlaying", "property": "title"})");
            QTest::qWait(200);
        }
        QVERIFY2(title.contains(R"("result":"Idle")"), title.constData());
        // The dev tooling's sim_* actions only come with --dev
        const QByteArray action = request(app, port, R"({"action": "sim_status"})");
        QVERIFY2(action.contains(R"("ok":false)"), action.constData());

        app.terminate();
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
        // Its own throwaway config, not the one the environment points at
        QVERIFY2(!QDir(tmp.filePath("OCTAVE")).exists(), qPrintable(out.right(3000)));
        QVERIFY2(out.contains("--sim: throwaway config in"), qPrintable(out.right(3000)));
    }

    void configDirKeepsEverythingThere()
    {
        QTemporaryDir tmp;
        const QString config = tmp.filePath("chosen");
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--headless", "--sim", "--config", config, "--service", QString::number(freePort()), "--smoke"});
        QVERIFY(app.waitForStarted(10000));
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(app.exitCode() == 0, qPrintable(out.right(3000)));
        QVERIFY2(QFile::exists(config + "/logs/octave-cpp.log"), qPrintable(out.right(3000)));
        QVERIFY2(!QDir(tmp.filePath("OCTAVE")).exists(), qPrintable(out.right(3000)));
        QVERIFY2(!out.contains("throwaway"), qPrintable(out.right(3000)));
    }

    // An installed exec frontend is the screen: a plain start runs headless,
    // starts it with a token that holds only its manifest's scopes, trusts
    // no other local connection, and quits when the frontend exits 0
    void anExecFrontendIsTheScreen()
    {
#if !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS)
        QSKIP("the fake frontend is a Unix program");
#else
        const QString python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            QSKIP("no python3 for the fake frontend");
        QTemporaryDir tmp;
        const QString config = tmp.filePath("config");
        const QString fe = config + "/frontends/org.test.screen";
        QDir().mkpath(fe);
        const QString platform =
#ifdef Q_OS_MACOS
            "macos";
#else
            "linux";
#endif
        QFile m(fe + "/octave-frontend.json");
        QVERIFY(m.open(QIODevice::WriteOnly));
        m.write(QStringLiteral(R"({"id": "org.test.screen", "name": "Test screen", "version": "1",
            "protocol": [1, 9], "scopes": ["read", "media"], "exec": {"%1": ["python3", "{dir}/main.py"]}})")
                    .arg(platform).toUtf8());
        m.close();
        QFile script(fe + "/main.py");
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"(import json, os, socket, time
def session(token=None):
    for _ in range(100):
        try:
            s = socket.create_connection((os.environ["OCTAVE_HOST"], int(os.environ["OCTAVE_PORT"])), timeout=5)
            break
        except OSError:
            time.sleep(0.1)
    f = s.makefile("rb")
    n = [0]
    def rpc(msg):
        n[0] += 1
        msg["id"] = n[0]
        s.sendall((json.dumps(msg) + "\n").encode())
        while True:
            r = json.loads(f.readline())
            if r.get("id") == n[0]:
                return r
    if token:
        rpc({"op": "hello", "token": token})
    return rpc
out = {}
rpc = session(os.environ["OCTAVE_TOKEN"])
out["read"] = rpc({"op": "get", "object": "volumeController", "property": "percent"})
out["media"] = rpc({"op": "call", "object": "volumeController", "method": "applyVolume", "args": [30]})
out["settings"] = rpc({"op": "call", "object": "settingsManager", "method": "save_setting", "args": ["uiScale", 1.0]})
out["system"] = rpc({"op": "call", "object": "frontendManager", "method": "setActive", "args": [""]})
out["notoken"] = session()({"op": "get", "object": "volumeController", "property": "percent"})
json.dump(out, open(os.path.join(os.environ["OCTAVE_FRONTEND_DIR"], "result.json"), "w"))
time.sleep(4)   # the simulated car records a trip meanwhile
)");
        script.close();
        QFile active(config + "/frontends/ACTIVE");
        QVERIFY(active.open(QIODevice::WriteOnly));
        active.write("org.test.screen\n");
        active.close();

        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--config", config, "--sim", "--service", QString::number(freePort())});
        QVERIFY(app.waitForStarted(10000));
        QVERIFY2(finished(app), "did not exit within 60 s");   // the frontend's exit 0 ends it
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
        QVERIFY2(out.contains("is the screen; loopback trust is off"), qPrintable(out.right(3000)));
        QVERIFY2(!out.contains("Main.qml"), qPrintable(out.right(3000)));
        // A normal quit: the black box closes the trip the simulated car made
        QVERIFY2(QRegularExpression("Trip \\d+ started").match(out).hasMatch(), qPrintable(out.right(3000)));
        QVERIFY2(out.contains("ending ("), qPrintable(out.right(3000)));
        QFile r(fe + "/result.json");
        QVERIFY2(r.open(QIODevice::ReadOnly), qPrintable(out.right(3000)));
        const QJsonObject res = QJsonDocument::fromJson(r.readAll()).object();
        auto ok = [&](const char *k) { return res.value(QLatin1String(k)).toObject().value("ok").toBool(); };
        auto code = [&](const char *k) { return res.value(QLatin1String(k)).toObject().value("code").toString(); };
        QVERIFY2(ok("read"), qPrintable(QJsonDocument(res).toJson()));
        QVERIFY2(ok("media"), qPrintable(QJsonDocument(res).toJson()));
        QCOMPARE(code("settings"), QStringLiteral("forbidden"));
        QCOMPARE(code("system"), QStringLiteral("forbidden"));
        QCOMPARE(code("notoken"), QStringLiteral("unauthorised"));
        // Still the active one: setActive was refused
        QFile again(config + "/frontends/ACTIVE");
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll().trimmed(), QByteArray("org.test.screen"));
#endif
    }

    // A frontend that can't start: USE_QML with the reason, and OCTAVE starts
    // again on the QML UI (with the same arguments: --smoke ends that one too)
    void aFailingExecFrontendFallsBackToQml()
    {
#if !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS)
        QSKIP("exec frontends on Unix only in this test");
#else
        QTemporaryDir tmp;
        const QString config = tmp.filePath("config");
        const QString fe = config + "/frontends/org.test.broken";
        QDir().mkpath(fe);
        QFile m(fe + "/octave-frontend.json");
        QVERIFY(m.open(QIODevice::WriteOnly));
        m.write(R"({"id": "org.test.broken", "name": "Broken", "version": "1", "protocol": [1, 9],
            "exec": {"linux": ["no-such-program-octave-test"], "macos": ["no-such-program-octave-test"]}})");
        m.close();
        QFile active(config + "/frontends/ACTIVE");
        QVERIFY(active.open(QIODevice::WriteOnly));
        active.write("org.test.broken\n");
        active.close();

        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--config", config, "--service", QString::number(freePort()), "--smoke"});
        QVERIFY(app.waitForStarted(10000));
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(app.exitCode() == 0, qPrintable(out.right(3000)));
        QVERIFY2(out.contains("starting again on the QML UI"), qPrintable(out.right(3000)));
        QFile useQml(config + "/frontends/USE_QML");
        QVERIFY(useQml.open(QIODevice::ReadOnly));
        QVERIFY(useQml.readAll().contains("neither in the frontend nor on PATH"));
        // The second start: the QML UI, which --smoke ends after 5 s
        QString log;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 60000 && !log.contains("Smoke test: Main.qml loaded")) {
            QTest::qWait(500);
            QFile f(config + "/logs/octave-cpp.log");
            if (f.open(QIODevice::ReadOnly))
                log = QString::fromUtf8(f.readAll());
        }
        QVERIFY2(log.contains("USE_QML: the QML UI instead of org.test.broken"), qPrintable(log.right(3000)));
        QVERIFY2(log.contains("Smoke test: Main.qml loaded"), qPrintable(log.right(3000)));
        QTest::qWait(7000);   // its --smoke exit, before the folder goes
#endif
    }

    // An installed web frontend chosen as the screen is served at GET /
    void anActiveWebFrontendIsServed()
    {
        QTemporaryDir tmp;
        const QString config = tmp.filePath("config");
        const QString fe = config + "/frontends/org.test.web";
        QDir().mkpath(fe + "/dist");
        QFile m(fe + "/octave-frontend.json");
        QVERIFY(m.open(QIODevice::WriteOnly));
        m.write(R"({"id": "org.test.web", "name": "Web", "version": "1", "protocol": [1, 9], "web": {"root": "dist"}})");
        m.close();
        QFile index(fe + "/dist/index.html");
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write("<!doctype html><title>installed-web-frontend</title>");
        index.close();
        QFile active(config + "/frontends/ACTIVE");
        QVERIFY(active.open(QIODevice::WriteOnly));
        active.write("org.test.web\n");
        active.close();

        const quint16 port = freePort();
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--headless", "--config", config, "--service", QString::number(port)});
        QVERIFY(app.waitForStarted(10000));
        QByteArray page;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 30000 && app.state() == QProcess::Running && !page.contains("installed-web-frontend")) {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, port);
            if (s.waitForConnected(500)) {
                s.write("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
                while (s.waitForReadyRead(2000))
                    page += s.readAll();
            } else {
                QTest::qWait(200);
            }
        }
        // Its page in a browser on this device holds the manifest's scopes
        // (read only here), not loopback's full trust; a program still has full
        const QByteArray browserHello = wsRequest(port, R"({"op": "hello", "id": 1})");
        QVERIFY2(browserHello.contains(R"("scopes":["read"])"), browserHello.constData());
        const QByteArray browserSet = wsRequest(port, R"({"op": "call", "object": "volumeController", "method": "applyVolume", "args": [20], "id": 2})");
        QVERIFY2(browserSet.contains(R"("code":"forbidden")"), browserSet.constData());
        const QByteArray browserGet = wsRequest(port, R"({"op": "get", "object": "volumeController", "property": "percent", "id": 3})");
        QVERIFY2(browserGet.contains(R"("ok":true)"), browserGet.constData());
        const QByteArray programSet = request(app, port, R"({"op": "call", "object": "volumeController", "method": "applyVolume", "args": [20]})");
        QVERIFY2(programSet.contains(R"("ok":true)"), programSet.constData());
        app.terminate();
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(page.contains("installed-web-frontend"), qPrintable(page.left(500) + out.right(2000)));
        QVERIFY2(out.contains("Frontend org.test.web 1 (web) is the active screen"), qPrintable(out.right(3000)));
    }

    // `octave --pair-code`: a code from the running instance of the same
    // config, from a second process; nothing running is exit 1, and another
    // config's instance is never the one asked
    void pairCodeFromTheRunningInstance()
    {
#ifdef Q_OS_ANDROID
        QSKIP("desktop only");
#else
        QTemporaryDir tmp;
        const QString a = tmp.filePath("a"), b = tmp.filePath("b");
        auto runPairCode = [&](const QString &config, QString *out) {
            QProcess p;
            p.setProcessEnvironment(env(tmp));
            p.start(kApp, {"--pair-code", "--config", config});
            if (!p.waitForFinished(30000))
                return -2;
            *out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
            return p.exitCode();
        };
        QString out;
        QCOMPARE(runPairCode(a, &out), 1);   // nothing running
        QVERIFY2(out.contains("no running OCTAVE"), qPrintable(out));

        QProcess appA, appB;
        for (QProcess *app : {&appA, &appB}) {
            app->setProcessEnvironment(env(tmp));
            app->setProcessChannelMode(QProcess::MergedChannels);
        }
        const quint16 portA = freePort();
        appA.start(kApp, {"--headless", "--sim", "--config", a, "--service", QString::number(portA)});
        QVERIFY(appA.waitForStarted(10000));
        QVERIFY(!request(appA, portA, R"({"op": "hello"})").isEmpty());   // up
        const quint16 portB = freePort();
        appB.start(kApp, {"--headless", "--config", b, "--service", QString::number(portB)});
        QVERIFY(appB.waitForStarted(10000));
        QVERIFY(!request(appB, portB, R"({"op": "hello"})").isEmpty());

        QCOMPARE(runPairCode(b, &out), 0);
        QVERIFY2(QRegularExpression(R"(^\{"code": "\d{6}", "secondsLeft": \d+\}\s*$)").match(out).hasMatch(), qPrintable(out));
        auto logOf = [](const QString &config) {
            QFile f(config + "/logs/octave-cpp.log");
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QTRY_VERIFY(logOf(b).contains("pairing code shown for octave --pair-code"));
        QVERIFY(!logOf(a).contains("pairing code shown"));   // A was not asked
        // The code is B's: it pairs a device there
        const QString code = QRegularExpression(R"re("code": "(\d{6})")re").match(out).captured(1);
        const QByteArray paired = request(appB, portB, QStringLiteral(R"({"op": "pair", "code": "%1", "name": "test"})").arg(code).toUtf8());
        QVERIFY2(paired.contains(R"("token")"), paired.constData());

        // The channel: the user's own socket, and one op only
        const QRegularExpressionMatch where = QRegularExpression("admin channel \\(octave --pair-code\\) on (\\S+)").match(logOf(a));
        QVERIFY2(where.hasMatch(), qPrintable(logOf(a).right(2000)));
#ifdef Q_OS_UNIX
        const QFileInfo sock(where.captured(1));
        QVERIFY(sock.exists());
        QCOMPARE(sock.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
                 QFile::Permissions());
#endif
        QLocalSocket s;
        s.connectToServer(where.captured(1));
        QVERIFY(s.waitForConnected(3000));
        s.write("{\"op\":\"shutdown\"}\n");
        QVERIFY(s.waitForReadyRead(3000));
        QVERIFY(s.readAll().contains("only pairCode"));

        for (QProcess *app : {&appA, &appB}) {
            app->terminate();
            QVERIFY(finished(*app));
        }
#ifdef Q_OS_UNIX
        QVERIFY(!QFileInfo::exists(where.captured(1)));   // removed at exit
#endif
        QCOMPARE(runPairCode(a, &out), 1);
#endif
    }

    void helpAndVersionExitBeforeAnything()
    {
        // No manager, no config read, no log file: --help on someone's
        // real config must not start the app
        for (const QStringList &args : {QStringList{"--help"}, QStringList{"-h"}, QStringList{"--version"},
                                        QStringList{"--headless", "--help"}}) {
            QTemporaryDir tmp;
            QProcess app;
            app.setProcessEnvironment(env(tmp));
            QElapsedTimer t;
            t.start();
            app.start(kApp, args);
            QVERIFY2(app.waitForFinished(5000), qPrintable(args.join(' ')));
            QVERIFY2(t.elapsed() < 3000, qPrintable(QString::number(t.elapsed())));
            QCOMPARE(app.exitStatus(), QProcess::NormalExit);
            QCOMPARE(app.exitCode(), 0);
            const QString out = QString::fromUtf8(app.readAllStandardOutput());
            QVERIFY(out.startsWith("OCTAVE "));
            if (args.contains("--version"))
                QCOMPARE(out.count('\n'), 1);
            else
                for (const char *flag : {"--service", "--service-bind", "--headless", "--sim", "--config", "--pair-code", "--web-root", "--dump-api", "--command-port"})
                    QVERIFY2(out.contains(flag), flag);
            QVERIFY2(QDir(tmp.path()).isEmpty(), qPrintable(QDir(tmp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).join(", ")));
        }
    }

    void noServiceUnlessAsked()
    {
        QTemporaryDir tmp;
        const quint16 port = freePort();
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--command-port", QString::number(port), "--smoke"});
        QVERIFY(app.waitForStarted(10000));
        QVERIFY(request(app, port, R"({"op": "hello"})", 8000).isEmpty());
        QVERIFY2(finished(app), "did not exit within 60 s");
        // Not only that nothing answered: the app says it never listened
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(out.contains("Service off"), qPrintable(out.right(3000)));
        QVERIFY2(!out.contains("command server listening"), qPrintable(out.right(3000)));
    }

private:
    // A port nothing is using right now: the OS picks it (a fixed range
    // would overlap the ephemeral ports other tests' sockets sit on)
    static quint16 freePort()
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0))
            return 0;
        const quint16 port = probe.serverPort();
        probe.close();
        return port;
    }

    // waitForFinished() is false for a process that already exited (one the
    // request loop saw go)
    static bool finished(QProcess &app)
    {
        return app.state() == QProcess::NotRunning || app.waitForFinished(60000);
    }

    // One request line to the app's port, retried until it listens (or
    // `timeoutMs` passes, or the app exits); the reply line, or empty
    static QByteArray request(QProcess &app, quint16 port, const QByteArray &line, int timeoutMs = 30000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs && app.state() == QProcess::Running) {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, port);
            if (s.waitForConnected(500)) {
                s.write(line + '\n');
                if (s.waitForReadyRead(5000))
                    return s.readLine().trimmed();
            } else {
                QTest::qWait(200);
            }
        }
        return {};
    }

    // One request over a WebSocket as a browser on this device opens it
    // (with the page's Origin); the first text frame back
    static QByteArray wsRequest(quint16 port, const QByteArray &json)
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        if (!s.waitForConnected(3000))
            return {};
        const QByteArray host = "127.0.0.1:" + QByteArray::number(port);
        s.write("GET / HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
                "Origin: http://" + host + "\r\n\r\n");
        QByteArray buf;
        while (!buf.contains("\r\n\r\n") && s.waitForReadyRead(3000))
            buf += s.readAll();
        if (!buf.startsWith("HTTP/1.1 101"))
            return buf;
        buf = buf.mid(buf.indexOf("\r\n\r\n") + 4);
        QByteArray frame("\x81", 1);
        frame += char(0x80 | 126);
        frame += char((json.size() >> 8) & 0xFF);
        frame += char(json.size() & 0xFF);
        const char mask[4] = {1, 2, 3, 4};
        frame += QByteArray(mask, 4);
        for (int i = 0; i < json.size(); ++i)
            frame += char(json[i] ^ mask[i % 4]);
        s.write(frame);
        // Text frames until the reply with our id (events may come first)
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 5000) {
            while (buf.size() >= 2) {
                int len = quint8(buf[1]) & 0x7F, off = 2;
                if (len == 126) {
                    if (buf.size() < 4)
                        break;
                    len = (quint8(buf[2]) << 8) | quint8(buf[3]);
                    off = 4;
                }
                if (buf.size() < off + len)
                    break;
                const QByteArray payload = buf.mid(off, len);
                buf = buf.mid(off + len);
                if (payload.contains("\"id\":"))
                    return payload;
            }
            if (!s.waitForReadyRead(1000))
                continue;
            buf += s.readAll();
        }
        return {};
    }

    static QProcessEnvironment env(const QTemporaryDir &tmp)
    {
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        e.insert("QT_QPA_PLATFORM", "offscreen");
        e.insert("XDG_CONFIG_HOME", tmp.path());   // never the real settings
        e.insert("HOME", tmp.path());
        return e;
    }
};

QTEST_GUILESS_MAIN(TstBoot)
#include "tst_boot.moc"
