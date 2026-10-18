// The built app must start: `octave --smoke` loads Main.qml with every manager
// and exits 0 after five seconds. Unit-level checks can't catch a startup path
// that calls something missing (a removed slot once broke boot while every
// other test passed). With the dev tooling built, `--dev --profile` must boot
// too and answer on its command server. Replaces tests/test_boot.py.

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
        QVERIFY2(app.waitForFinished(60000), "did not exit within 60 s");
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

        QByteArray reply;
        QElapsedTimer t;
        t.start();
        while (reply.isEmpty() && t.elapsed() < 30000 && app.state() == QProcess::Running) {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, port);
            if (s.waitForConnected(500)) {
                s.write("{\"action\": \"ping\"}\n");
                if (s.waitForReadyRead(5000))
                    reply = s.readLine();
            } else {
                QTest::qWait(200);
            }
        }
        QVERIFY2(app.waitForFinished(60000), "did not exit within 60 s");
        const QString out = QString::fromUtf8(app.readAll());
        QCOMPARE(reply.trimmed(), QByteArray(R"({"ok":true,"result":"pong"})"));
        QVERIFY2(out.contains("DevOverlay attached"), qPrintable(out.right(3000)));
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(out.right(3000)));
#endif
    }

private:
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
