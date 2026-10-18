// QML fixtures in tests/fixtures/, each run by qmlfixturehost in its own
// process against the C++ managers: the 3D music device scenes and their
// shared preference (ported from tests/test_music_devices.py) and the offline
// wiki viewer (tests/test_wiki_viewer.py).

#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString kRepo = QStringLiteral(OCTAVE_SOURCE_DIR);
const QString kHost = QStringLiteral(OCTAVE_QML_FIXTURE_HOST);
} // namespace

class TstQmlFixtures : public QObject
{
    Q_OBJECT

private slots:
    void musicDevices_data()
    {
        QTest::addColumn<QString>("fixture");
        for (const char *f : {"MusicDevicesSmoke.qml", "MusicDeviceLoadingSmoke.qml", "MusicDeviceArtworkSmoke.qml",
                              "MusicDeviceDetailsSmoke.qml"})
            QTest::newRow(f) << QString::fromLatin1(f);
    }

    void musicDevices()
    {
        QFETCH(QString, fixture);
        QTemporaryDir tmp;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_CONFIG_HOME", tmp.path());
        env.insert("QT_QUICK_CONTROLS_STYLE", "Basic");
        env.insert("QSG_RHI_BACKEND", "opengl");
        const auto [code, output] = run({kRepo + "/tests/fixtures/" + fixture, tmp.path()}, env, 35000);
        QVERIFY2(code == 0 && output.contains("MUSIC_DEVICES_SMOKE_PASS"), qPrintable(output));
        QVERIFY2(!output.contains("ReferenceError") && !output.contains("Unable to assign"), qPrintable(output));
    }

    void offlineWiki_data()
    {
        QTest::addColumn<bool>("available");
        QTest::newRow("viewer available") << true;
        QTest::newRow("viewer unavailable") << false;
    }

    void offlineWiki()
    {
        QFETCH(bool, available);
#ifndef OCTAVE_HAVE_WEBENGINE
        if (available)
            QSKIP("built without Qt WebEngine: the in-app wiki viewer is unavailable");
#endif
        QTemporaryDir tmp;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_CONFIG_HOME", tmp.path());
        env.insert("QT_QUICK_CONTROLS_STYLE", "Basic");
        // The same browser rendering policy as the app's entry point
        env.insert("QTWEBENGINE_CHROMIUM_FLAGS", (env.value("QTWEBENGINE_CHROMIUM_FLAGS") + " --disable-gpu").trimmed());
        const auto [code, output] = run({kRepo + "/tests/fixtures/WikiSmoke.qml", tmp.path(),
                                         available ? "true" : "false"}, env, 45000);
        QVERIFY2(code == 0 && output.contains("WIKI_SMOKE_PASS"), qPrintable(output));
    }

private:
    static std::pair<int, QString> run(const QStringList &args, const QProcessEnvironment &env, int timeoutMs)
    {
        QProcess p;
        p.setProcessEnvironment(env);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.setWorkingDirectory(kRepo);
        p.start(kHost, args);
        if (!p.waitForFinished(timeoutMs)) {
            p.kill();
            p.waitForFinished(2000);
            return {-1, QStringLiteral("timed out\n") + QString::fromUtf8(p.readAll())};
        }
        return {p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2, QString::fromUtf8(p.readAll())};
    }
};

QTEST_GUILESS_MAIN(TstQmlFixtures)
#include "tst_qmlfixtures.moc"
