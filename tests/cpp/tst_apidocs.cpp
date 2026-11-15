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

    // settingsManager is the exception above because its plain settings are
    // writable properties: a save_* / get_* method that only repeats one is
    // deprecated and names it. What is left in snake_case does more than
    // read or write one setting, and is listed here, so a new one is a
    // decision and not an accident.
    void settingsMethodsPointAtWritableProperties()
    {
        QFile f(kDocs + "/settingsManager.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject d = QJsonDocument::fromJson(f.readAll()).object();
        QHash<QString, bool> writable;
        for (const QJsonValue &p : d.value("properties").toArray())
            writable.insert(p.toObject().value("name").toString(), p.toObject().value("writable").toBool());
        const QSet<QString> kept = {
            "add_home_obd_parameter", "add_obd_saved_adapter", "apply_settings", "delete_custom_theme",
            "get_gesture_mapping", "get_gesture_mapping_dict", "get_git_commit_date", "get_git_commit_hash",
            "get_git_commit_message", "get_obd_parameter_enabled", "get_pinned_settings_metadata", "get_setting",
            "get_setting_with_default", "get_settings_registry", "get_supported_obd_parameters", "is_setting_pinned",
            "is_settings_section_visible", "move_home_obd_parameter", "remove_from_directory_history",
            "remove_home_obd_parameter", "remove_obd_saved_adapter", "reset_gesture_mapping", "reset_to_defaults",
            "save_custom_theme", "save_gesture_action", "save_gesture_mapping", "save_media_folder_url",
            "save_obd_parameter_enabled", "save_playback_state", "save_sensor_card_visible", "save_setting",
            "save_settings_section_visibility", "save_supported_obd_parameters", "save_to_directory_history",
            "set_album_art_colors", "toggle_pin_setting"};
        static const QRegularExpression note("^use the (\\w+) property; goes in protocol 5$");
        QStringList offenders;
        QSet<QString> seen;   // a method with a default argument is listed once per overload
        int deprecated = 0;
        for (const QJsonValue &v : d.value("methods").toArray()) {
            const QJsonObject m = v.toObject();
            const QString name = m.value("name").toString();
            if (!name.contains('_'))
                continue;
            if (!m.contains("deprecated")) {
                if (!kept.contains(name))
                    offenders << name + " is snake_case, not deprecated and not in the list of those that stay";
                seen.insert(name);
                continue;
            }
            ++deprecated;
            const QRegularExpressionMatch match = note.match(m.value("deprecated").toString());
            if (!match.hasMatch()) {
                offenders << name + ": \"" + m.value("deprecated").toString() + "\" names no property";
                continue;
            }
            const QString property = match.captured(1);
            if (!writable.contains(property))
                offenders << name + " points at " + property + ", which is not a property";
            else if (!name.startsWith("get_") && !writable.value(property))
                offenders << name + " points at " + property + ", which cannot be written";
        }
        for (const QString &name : std::as_const(kept))
            if (!seen.contains(name))
                offenders << name + " is in the list and is not a method that stays";
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join("; ")));
        QVERIFY2(deprecated > 100, qPrintable(QString::number(deprecated)));
        // A client can write a plain setting with `set`, with the settings permission
        int settable = 0;
        for (const QJsonValue &p : d.value("properties").toArray())
            settable += p.toObject().value("writable").toBool() && p.toObject().value("scope").toString() == "settings";
        QVERIFY2(settable > 80, qPrintable(QString::number(settable)));
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
