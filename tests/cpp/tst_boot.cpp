// The built app must start: `octave --smoke` loads Main.qml with every manager
// and exits 0 after five seconds. Unit-level checks can't catch a startup path
// that calls something missing (a removed slot once broke boot while every
// other test passed). With the dev tooling built, `--dev --profile` must boot
// too and answer on its command server, and `--service` must serve the
// service protocol in any build without the dev-only actions; `--headless`
// must bring the managers and the service up with no QML at all, and with
// the dev tooling `--headless --dev` must connect the real OBDManager to the
// simulated car and take the sim_* actions.

#include <QElapsedTimer>
#include <QProcess>
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
        const quint16 port = quint16(20000 + QCoreApplication::applicationPid() % 20000);
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
        const quint16 port = quint16(20001 + QCoreApplication::applicationPid() % 20000);
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
        const quint16 port = quint16(20003 + QCoreApplication::applicationPid() % 20000);
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--headless", "--service", QString::number(port), "--smoke"});
        QVERIFY(app.waitForStarted(10000));

        const QByteArray hello = request(app, port, R"({"op": "hello"})");
        const QByteArray get = request(app, port, R"({"op": "get", "object": "volumeController", "property": "percent"})");
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(hello.contains(R"("headless":true)"), qPrintable(hello + out.right(3000)));
        QVERIFY2(get.startsWith(R"({"ok":true)"), get.constData());
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
        const quint16 port = quint16(20004 + QCoreApplication::applicationPid() % 20000);
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
        const QByteArray status = request(app, port, R"({"action": "sim_status"})");
        QVERIFY2(status.startsWith(R"({"ok":true)") && status.contains("\"imu\""), status.constData());

        app.terminate();   // SIGTERM: the normal clean shutdown
        QVERIFY2(finished(app), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QVERIFY2(!out.contains("Main.qml"), qPrintable(out.right(3000)));
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
#endif
    }

    void noServiceUnlessAsked()
    {
        QTemporaryDir tmp;
        const quint16 port = quint16(20002 + QCoreApplication::applicationPid() % 20000);
        QProcess app;
        app.setProcessEnvironment(env(tmp));
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--command-port", QString::number(port), "--smoke"});
        QVERIFY(app.waitForStarted(10000));
        QVERIFY(request(app, port, R"({"op": "hello"})", 8000).isEmpty());
        QVERIFY2(finished(app), "did not exit within 60 s");
    }

private:
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
