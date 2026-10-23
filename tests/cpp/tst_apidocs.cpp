// docs/api/ is the published API (TODO/open-backend.md, contract step 3):
// the built app's `--dump-api` output must match the committed copy, so a
// manager's public surface never changes without its docs. On a mismatch,
// regenerate: ./build/octave --dump-api docs/api (docs/api/README.md).

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString kApp = QStringLiteral(OCTAVE_APP_BINARY);
const QString kDocs = QStringLiteral(OCTAVE_SOURCE_DIR "/docs/api");
// Hand-written files living next to the generated ones
const QStringList kHandWritten = {QStringLiteral("dashboard-spec.schema.json")};
} // namespace

class TstApiDocs : public QObject
{
    Q_OBJECT

private slots:
    void committedDocsMatchTheBuild()
    {
#if !defined(OCTAVE_ENABLE_DOWNLOADS) || !defined(Q_OS_LINUX)
        QSKIP("docs/api is the Linux desktop build with downloads on");
#endif
        QTemporaryDir out;
        QVERIFY(out.isValid());
        QProcess app;
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--dump-api", out.path()});
        QVERIFY2(app.waitForFinished(60000), "did not exit within 60 s");
        const QString log = QString::fromUtf8(app.readAll());
        QVERIFY2(app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0, qPrintable(log.right(3000)));

        const QStringList generated = QDir(out.path()).entryList({"*.json"}, QDir::Files, QDir::Name);
        QVERIFY(generated.contains("index.json"));
        QVERIFY(generated.contains("parameters.json"));
        QStringList stale;
        for (const QString &name : generated) {
            QFile mine(out.filePath(name)), committed(kDocs + '/' + name);
            if (!committed.open(QIODevice::ReadOnly) || !mine.open(QIODevice::ReadOnly)
                || committed.readAll() != mine.readAll())
                stale << name;
        }
        // A committed file the build no longer writes (a removed manager)
        QStringList orphaned = QDir(kDocs).entryList({"*.json"}, QDir::Files, QDir::Name);
        for (const QString &name : generated + kHandWritten)
            orphaned.removeAll(name);
        QVERIFY2(stale.isEmpty() && orphaned.isEmpty(),
                 qPrintable(QStringLiteral("docs/api is out of date (stale: %1; orphaned: %2). "
                                           "Run ./build/octave --dump-api docs/api and "
                                           "venv/bin/python wiki/build_api_reference.py")
                                .arg(stale.join(", "), orphaned.join(", "))));
    }

    // Only for docs: the real config must not be where the dump builds
    // its managers
    void dumpLeavesTheConfigAlone()
    {
        QTemporaryDir home, out;
        QProcess app;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("HOME", home.path());
        env.insert("XDG_CONFIG_HOME", home.path() + "/config");
        app.setProcessEnvironment(env);
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(kApp, {"--dump-api", out.path()});
        QVERIFY(app.waitForFinished(60000));
        QCOMPARE(app.exitCode(), 0);
        QVERIFY2(QDir(home.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty(),
                 qPrintable(QDir(home.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
                                .join(", ")));
    }
};

QTEST_GUILESS_MAIN(TstApiDocs)
#include "tst_apidocs.moc"
