// FrontendSupervisor (src/managers/frontendsupervisor.h) with fake exec
// frontends (shell scripts, so Unix only): the program resolved inside its
// folder or on PATH, the environment and a session token holding only the
// manifest's scopes, exit 0 ends it, crashes restart it with back-off until
// it gives up, a long run forgives earlier crashes, stop() ends a program
// that runs on, and every token is revoked when its run ends.

#include "managers/frontendsupervisor.h"
#include "managers/settingsmanager.h"
#include "service/servicepairing.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

class TstFrontendSupervisor : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
#ifndef Q_OS_UNIX
        QSKIP("the fake frontends are shell scripts");
#endif
        m_tmp = std::make_unique<QTemporaryDir>();
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        m_settings = std::make_unique<SettingsManager>();
        m_pairing = std::make_unique<ServicePairing>(m_settings.get());
    }

    void cleanup()
    {
        m_pairing.reset();
        m_settings.reset();
        m_tmp.reset();
    }

    void resolvesTheProgram()
    {
        const QString dir = frontend("#!/bin/sh\nexit 0\n");
        QString error;
        FrontendManifest m = manifest({"bin/run", "--x", "{dir}/data"});
        QStringList argv = FrontendSupervisor::resolveCommand(m, dir, &error);
        QCOMPARE(argv.value(0), QDir(dir).absoluteFilePath("bin/run"));
        QCOMPARE(argv.value(2), QDir(dir).absolutePath() + "/data");
        m = manifest({"sh", "-c", "true"});   // a bare name: PATH
        argv = FrontendSupervisor::resolveCommand(m, dir, &error);
        QVERIFY2(QFileInfo(argv.value(0)).isExecutable(), qPrintable(error));
        m = manifest({"no-such-program-anywhere"});
        QVERIFY(FrontendSupervisor::resolveCommand(m, dir, &error).isEmpty());
        QVERIFY(error.contains("neither in the frontend nor on PATH"));
        m = manifest({"bin/missing"});
        QVERIFY(FrontendSupervisor::resolveCommand(m, dir, &error).isEmpty());
    }

    void environmentTokenAndCleanExit()
    {
        const QString out = m_tmp->filePath("env.txt");
        const QString dir = frontend(QStringLiteral("#!/bin/sh\n"
                                                    "{ echo \"$OCTAVE_HOST\"; echo \"$OCTAVE_PORT\"; echo \"$OCTAVE_TOKEN\";\n"
                                                    "  echo \"$OCTAVE_SCOPES\"; echo \"$OCTAVE_PROTOCOL\"; pwd; } > '%1'\n"
                                                    "echo hello from the frontend\n"
                                                    "exit 0\n").arg(out));
        FrontendSupervisor s(manifest({"bin/run"}, {"read", "media"}), dir, m_pairing.get(), 19841);
        FrontendSupervisor::Options o;
        o.quickQuitMs = 0;   // it quits at once; that alone is tested below
        s.setOptions(o);
        QSignalSpy clean(&s, &FrontendSupervisor::exitedCleanly);
        QSignalSpy gaveUp(&s, &FrontendSupervisor::gaveUp);
        s.start();
        QVERIFY(clean.wait(10000));
        QCOMPARE(gaveUp.count(), 0);
        QCOMPARE(s.launches(), 1);
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
        QCOMPARE(lines.value(0), QStringLiteral("127.0.0.1"));
        QCOMPARE(lines.value(1), QStringLiteral("19841"));
        const QString token = lines.value(2);
        QCOMPARE(token.size(), 64);
        QCOMPARE(lines.value(3), QStringLiteral("read,media"));
        QCOMPARE(lines.value(4), QStringLiteral("2"));
        QCOMPARE(QDir(lines.value(5)).canonicalPath(), QDir(dir).canonicalPath());
        // The run is over: its token is gone, and it was never saved
        QVERIFY(m_pairing->checkToken(token).isEmpty());
        QVERIFY(m_settings->fileOnlySetting("serviceTokens").toList().isEmpty());
    }

    void theTokenHoldsOnlyTheManifestScopes()
    {
        const QString out = m_tmp->filePath("token.txt");
        const QString dir = frontend(QStringLiteral("#!/bin/sh\necho \"$OCTAVE_TOKEN\" > '%1'\nexec sleep 30\n").arg(out));
        FrontendSupervisor s(manifest({"bin/run"}, {"read", "media"}), dir, m_pairing.get(), 19841);
        QSignalSpy started(&s, &FrontendSupervisor::started);
        s.start();
        QVERIFY(started.wait(10000));
        QTRY_VERIFY(QFileInfo(out).size() > 0);
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString token = QString::fromUtf8(f.readAll()).trimmed();
        const QString id = m_pairing->checkToken(token);
        QVERIFY(!id.isEmpty());
        QVERIFY(m_pairing->allows(id, "media"));
        QVERIFY(!m_pairing->allows(id, "settings"));
        QVERIFY(!m_pairing->allows(id, "system"));
        QVERIFY(!m_pairing->allows(id, "full"));
        // stop() ends a program that runs on, and revokes its token
        QElapsedTimer t;
        t.start();
        s.stop();
        QVERIFY(!s.running());
        QVERIFY(t.elapsed() < 5000);
        QVERIFY(m_pairing->checkToken(token).isEmpty());
    }

    void crashesRestartThenItGivesUp()
    {
        const QString dir = frontend("#!/bin/sh\necho boom\nexit 3\n");
        FrontendSupervisor s(manifest({"bin/run"}), dir, m_pairing.get(), 19841);
        FrontendSupervisor::Options o;
        o.maxQuickCrashes = 3;
        o.firstBackoffMs = 20;
        o.maxBackoffMs = 50;
        s.setOptions(o);
        QSignalSpy gaveUp(&s, &FrontendSupervisor::gaveUp);
        QSignalSpy clean(&s, &FrontendSupervisor::exitedCleanly);
        s.start();
        QVERIFY(gaveUp.wait(10000));
        QCOMPARE(s.launches(), 3);
        QCOMPARE(clean.count(), 0);
        const QString why = gaveUp.first().at(0).toString();
        QVERIFY2(why.contains("exited with 3") && why.contains("3 times"), qPrintable(why));
        QTest::qWait(200);
        QCOMPARE(s.launches(), 3);   // and stays down
        // No session device left behind
        for (const QVariant &d : m_pairing->devices())
            QVERIFY(!d.toMap().value("session").toBool());
    }

    void aLongRunForgivesEarlierCrashes()
    {
        const QString dir = frontend("#!/bin/sh\nsleep 0.3\nexit 1\n");
        FrontendSupervisor s(manifest({"bin/run"}), dir, m_pairing.get(), 19841);
        FrontendSupervisor::Options o;
        o.maxQuickCrashes = 2;
        o.quickRunMs = 200;   // every 0.3 s run counts as long
        o.firstBackoffMs = 10;
        s.setOptions(o);
        QSignalSpy gaveUp(&s, &FrontendSupervisor::gaveUp);
        s.start();
        QTRY_VERIFY_WITH_TIMEOUT(s.launches() >= 4, 10000);
        QCOMPARE(gaveUp.count(), 0);
        s.stop();
    }

    // A frontend that quits (exit 0) at once on every start would quit
    // OCTAVE at every boot: that counts as a crash, and falls back to QML
    void quittingAtOnceCountsAsACrash()
    {
        const QString dir = frontend("#!/bin/sh\nexit 0\n");
        FrontendSupervisor s(manifest({"bin/run"}), dir, m_pairing.get(), 19841);
        FrontendSupervisor::Options o;
        o.maxQuickCrashes = 3;
        o.firstBackoffMs = 10;
        o.maxBackoffMs = 30;
        s.setOptions(o);
        QSignalSpy gaveUp(&s, &FrontendSupervisor::gaveUp);
        QSignalSpy clean(&s, &FrontendSupervisor::exitedCleanly);
        s.start();
        QVERIFY(gaveUp.wait(10000));
        QCOMPARE(clean.count(), 0);
        QCOMPARE(s.launches(), 3);
        const QString why = gaveUp.first().at(0).toString();
        QVERIFY2(why.contains("quit (exit code 0) within 3 s of starting"), qPrintable(why));
    }

    // ... but past quickQuitMs an exit 0 is the user quitting
    void quittingLaterQuitsOctave()
    {
        const QString dir = frontend("#!/bin/sh\nsleep 0.3\nexit 0\n");
        FrontendSupervisor s(manifest({"bin/run"}), dir, m_pairing.get(), 19841);
        FrontendSupervisor::Options o;
        o.quickQuitMs = 100;
        s.setOptions(o);
        QSignalSpy clean(&s, &FrontendSupervisor::exitedCleanly);
        s.start();
        QVERIFY(clean.wait(10000));
        QCOMPARE(s.launches(), 1);
    }

    // A frontend that prints without end: at most the budget a second reaches
    // the log, long lines are cut, and the rest is counted as dropped
    void theLogRelayIsBounded()
    {
        static QStringList relayed;
        relayed.clear();
        const QtMessageHandler previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &c, const QString &m) {
            if (QByteArray(c.category ? c.category : "") == "octave.frontends.supervisor")
                relayed << m;
        });
        // 20000 short lines and a 200 KB line, as fast as it can
        const QString dir = frontend("#!/bin/sh\n"
                                     "i=0; while [ $i -lt 20000 ]; do echo \"line $i\"; i=$((i+1)); done\n"
                                     "head -c 200000 /dev/zero | tr '\\0' x; echo\n"
                                     "sleep 1.2\necho after\nexit 0\n");
        FrontendSupervisor s(manifest({"bin/run"}), dir, m_pairing.get(), 19841);
        FrontendSupervisor::Options o;
        o.quickQuitMs = 0;
        o.logLinesPerSecond = 50;
        o.logLineMax = 100;
        s.setOptions(o);
        QSignalSpy clean(&s, &FrontendSupervisor::exitedCleanly);
        s.start();
        QVERIFY(clean.wait(30000));
        qInstallMessageHandler(previous);
        // Two one-second windows at most (the burst, then "after")
        QVERIFY2(s.relayedLines() <= 2 * 50, qPrintable(QString::number(s.relayedLines())));
        QVERIFY(s.droppedLines() > 10000);
        int longest = 0;
        bool note = false, after = false;
        for (const QString &m : std::as_const(relayed)) {
            longest = qMax(longest, int(m.size()));
            note = note || m.contains("lines of output dropped");
            after = after || m.endsWith("after ");
        }
        QVERIFY2(longest < 100 + 40, qPrintable(QString::number(longest)));   // the line, cut, and its "[id]" prefix
        QVERIFY(note);
        QVERIFY2(after, qPrintable(relayed.mid(relayed.size() - 3).join(" | ")));   // a new second has a new budget
    }

    void aProgramThatCantStartGivesUpAtOnce()
    {
        const QString dir = frontend("#!/bin/sh\nexit 0\n");
        QFile::remove(QDir(dir).filePath("bin/run"));
        FrontendSupervisor s(manifest({"bin/run"}), dir, m_pairing.get(), 19841);
        QSignalSpy gaveUp(&s, &FrontendSupervisor::gaveUp);
        s.start();
        QCOMPARE(gaveUp.count(), 1);   // nothing a restart would fix
        QCOMPARE(s.launches(), 0);
    }

private:
    FrontendManifest manifest(const QStringList &argv, const QStringList &scopes = {"read"})
    {
        FrontendManifest m;
        m.id = "org.test.fake";
        m.name = "Fake";
        m.version = "1";
        m.protocolMin = 1;
        m.protocolMax = 9;
        m.scopes = scopes;
        m.exec.insert(FrontendManifest::currentPlatform(), argv);
        return m;
    }

    QString frontend(const QString &script)
    {
        const QString dir = m_tmp->filePath(QStringLiteral("fe-%1").arg(++m_count));
        QDir().mkpath(dir + "/bin");
        QFile f(dir + "/bin/run");
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(script.toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return dir;
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<ServicePairing> m_pairing;
    int m_count = 0;
};

QTEST_GUILESS_MAIN(TstFrontendSupervisor)
#include "tst_frontendsupervisor.moc"
