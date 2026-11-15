// docs/api/ is the published API (TODO/open-backend.md, contract step 3):
// the built app's `--dump-api` output must match the committed copy, so a
// manager's public surface never changes without its docs. On a mismatch,
// regenerate: ./build/octave --dump-api docs/api (docs/api/README.md).

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString kApp = QStringLiteral(OCTAVE_APP_BINARY);
const QString kDocs = QStringLiteral(OCTAVE_SOURCE_DIR "/docs/api");
// Hand-written files living next to the generated ones
const QStringList kHandWritten = {QStringLiteral("dashboard-spec.schema.json"), QStringLiteral("protocol.json")};
} // namespace

class TstApiDocs : public QObject
{
    Q_OBJECT

private slots:
    void committedDocsMatchTheBuild()
    {
#if !defined(Q_OS_LINUX)
        QSKIP("docs/api is the Linux desktop build");
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

    // Method names are camelCase. The snake_case ones (from the Python
    // backend) stay through protocol 3, each marked deprecated and, where
    // it says "use X", beside an X that takes and returns the same and needs
    // the same permission. settingsManager's save_* / get_* are replaced by
    // writable properties instead (TODO/fix-up-leftovers.md), so not here.
    // Over every object the app registers: a new snake_case method fails.
    void snakeCaseMethodsAreDeprecatedBesideACamelCaseOne()
    {
        QFile indexFile(kDocs + "/index.json");
        QVERIFY(indexFile.open(QIODevice::ReadOnly));
        const QStringList objects = QJsonDocument::fromJson(indexFile.readAll()).object().value("objects").toObject().keys();
        QVERIFY2(objects.size() > 20, qPrintable(QString::number(objects.size())));
        QStringList offenders;
        int deprecated = 0;
        for (const QString &object : objects) {
            if (object == "settingsManager")
                continue;
            QFile f(kDocs + '/' + object + ".json");
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(object));
            const QJsonObject d = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonArray methods = d.value("methods").toArray();
            QSet<QString> offered;
            for (const char *list : {"methods", "properties"})
                for (const QJsonValue &m : d.value(QLatin1String(list)).toArray())
                    offered.insert(m.toObject().value("name").toString());
            for (const QJsonValue &v : methods) {
                const QJsonObject m = v.toObject();
                const QString name = m.value("name").toString();
                if (!name.contains('_'))
                    continue;
                const QString note = m.value("deprecated").toString();
                if (note.isEmpty()) {
                    offenders << object + '.' + name + " is snake_case and not deprecated";
                    continue;
                }
                ++deprecated;
                if (!note.startsWith("use "))
                    continue;   // "internal", "does nothing": nothing replaces it
                const QString instead = note.mid(4).section(QRegularExpression("[ ;]"), 0, 0);
                if (!offered.contains(instead)) {
                    offenders << object + '.' + name + " points at " + instead + ", which is not offered";
                    continue;
                }
                // The same call under the new name: an overload with these
                // parameters, this result and this permission
                bool twin = false;
                for (const QJsonValue &o : methods) {
                    const QJsonObject other = o.toObject();
                    twin = twin || (other.value("name").toString() == instead && !other.contains("deprecated")
                                    && other.value("params") == m.value("params")
                                    && other.value("returns") == m.value("returns")
                                    && other.value("scope") == m.value("scope"));
                }
                if (!twin)
                    offenders << object + '.' + name + " and " + instead + " differ in parameters, result or scope";
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join("; ")));
        QVERIFY2(deprecated > 100, qPrintable(QString::number(deprecated)));   // the scan still finds them
    }

    // The note a client sees
    void aDeprecatedMethodSaysWhatToUse()
    {
        QFile f(kDocs + "/mediaManager.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QHash<QString, QString> notes;
        for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value("methods").toArray())
            notes.insert(v.toObject().value("name").toString(), v.toObject().value("deprecated").toString());
        QCOMPARE(notes.value("next_track"), QStringLiteral("use nextTrack; goes in protocol 5"));
        QVERIFY(notes.contains("nextTrack"));
        QCOMPARE(notes.value("nextTrack"), QString());
        QCOMPARE(notes.value("connect_settings_manager"), QStringLiteral("does nothing; goes in protocol 5"));
    }
};

QTEST_GUILESS_MAIN(TstApiDocs)
#include "tst_apidocs.moc"
