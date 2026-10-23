// Installable frontends (src/managers/frontendmanager.h): the manifest's
// rules, installing from a folder and from a zip (nothing outside the
// frontend's folder, no links, size caps, nothing installed when a check
// fails), remove, the active choice and what the next start shows.

#include "managers/frontendmanager.h"
#include "service/protocol.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtCore/private/qzipwriter_p.h>

namespace {
QJsonObject webManifest(const QString &id = QStringLiteral("com.example.dash"))
{
    return {{"id", id}, {"name", "Example Dash"}, {"version", "1.0.0"},
            {"protocol", QJsonArray{1, OctaveService::kProtocolVersion}},
            {"scopes", QJsonArray{"read", "media"}}, {"web", QJsonObject{{"root", "dist"}}}};
}

QByteArray json(const QJsonObject &o)
{
    return QJsonDocument(o).toJson();
}

void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// QZipWriter never writes the executable bit: set the Unix mode of `name`
// in the zip's central directory, as zip(1) on Unix would have
void setZipMode(const QString &zipPath, const QByteArray &name, quint32 mode)
{
    QFile f(zipPath);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QByteArray b = f.readAll();
    int at = 0;
    while ((at = b.indexOf(QByteArray("PK\x01\x02", 4), at)) >= 0) {
        const int nameLen = quint8(b[at + 28]) | quint8(b[at + 29]) << 8;
        if (b.mid(at + 46, nameLen) == name) {
            const quint32 external = mode << 16;
            b[at + 5] = 3;   // made on Unix
            for (int i = 0; i < 4; ++i)
                b[at + 38 + i] = char((external >> (8 * i)) & 0xFF);
        }
        at += 46;
    }
    f.seek(0);
    f.write(b);
}

// A frontend folder with the manifest and dist/index.html
QString makeWebFolder(const QTemporaryDir &tmp, const QJsonObject &manifest, const QString &name = QStringLiteral("src"))
{
    const QString dir = tmp.filePath(name);
    write(dir + "/octave-frontend.json", json(manifest));
    write(dir + "/dist/index.html", "<!doctype html><title>dash</title>");
    return dir;
}
} // namespace

class TstFrontendManager : public QObject
{
    Q_OBJECT

private slots:
    // ── The manifest ──
    void manifestParsesAWebFrontend()
    {
        QStringList errors;
        const FrontendManifest m = FrontendManifest::parse(json(webManifest()), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join("; ")));
        QCOMPARE(m.id, QStringLiteral("com.example.dash"));
        QVERIFY(m.isWeb() && !m.isExec());
        QCOMPARE(m.webRoot, QStringLiteral("dist"));
        QCOMPARE(m.scopes, (QStringList{"read", "media"}));
        QVERIFY(m.speaks(OctaveService::kProtocolVersion));
    }

    void manifestParsesAnExecFrontend()
    {
        QJsonObject o = webManifest();
        o.remove("web");
        o.insert("exec", QJsonObject{{"linux", QJsonArray{"bin/dash", "--fs"}}, {"windows", QJsonArray{"dash.exe"}},
                                     {"amiga", QJsonArray{"x"}}});
        o.remove("scopes");
        QStringList errors;
        const FrontendManifest m = FrontendManifest::parse(json(o), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join("; ")));
        QVERIFY(m.isExec());
        QCOMPARE(m.exec.value("linux"), (QStringList{"bin/dash", "--fs"}));
        QVERIFY(!m.exec.contains("amiga"));               // a platform it doesn't know
        QCOMPARE(m.scopes, QStringList{"read"});          // the default
    }

    void manifestRefusesWhatItMust_data()
    {
        QTest::addColumn<QByteArray>("manifest");
        QTest::addColumn<QString>("error");
        auto with = [](const char *key, const QJsonValue &v) {
            QJsonObject o = webManifest();
            if (v.isUndefined())
                o.remove(QLatin1String(key));
            else
                o.insert(QLatin1String(key), v);
            return json(o);
        };
        QTest::newRow("not json") << QByteArray("{nope") << "not a JSON object";
        QTest::newRow("no id") << with("id", QJsonValue::Undefined) << "\"id\"";
        QTest::newRow("upper-case id") << with("id", "Com.Example") << "\"id\" must be";
        QTest::newRow("id with ..") << with("id", "a..b") << "\"id\" must be";
        QTest::newRow("id with /") << with("id", "a/b") << "\"id\" must be";
        QTest::newRow("no name") << with("name", "") << "\"name\"";
        QTest::newRow("no version") << with("version", QJsonValue::Undefined) << "\"version\"";
        QTest::newRow("bad protocol") << with("protocol", QJsonArray{3, 2}) << "\"protocol\"";
        QTest::newRow("protocol one number") << with("protocol", QJsonArray{2}) << "\"protocol\"";
        QTest::newRow("unknown scope") << with("scopes", QJsonArray{"read", "root"}) << "unknown scope \"root\"";
        QTest::newRow("web root escapes") << with("web", QJsonObject{{"root", "../x"}}) << "\"web.root\"";
        QTest::newRow("web root absolute") << with("web", QJsonObject{{"root", "/etc"}}) << "\"web.root\"";
        QTest::newRow("both web and exec") << with("exec", QJsonObject{{"linux", QJsonArray{"x"}}}) << "exactly one";
        QJsonObject neither = webManifest();
        neither.remove("web");
        QTest::newRow("neither") << json(neither) << "exactly one";
        QJsonObject escape = neither;
        escape.insert("exec", QJsonObject{{"linux", QJsonArray{"../../bin/sh"}}});
        QTest::newRow("exec escapes") << json(escape) << "\"exec.linux\"";
        QJsonObject empty = neither;
        empty.insert("exec", QJsonObject{{"linux", QJsonArray{}}});
        QTest::newRow("exec empty") << json(empty) << "\"exec.linux\"";
        QTest::newRow("too big") << QByteArray(FrontendManifest::kMaxBytes + 1, ' ') << "over 64 KB";
    }

    void manifestRefusesWhatItMust()
    {
        QFETCH(QByteArray, manifest);
        QFETCH(QString, error);
        QStringList errors;
        const FrontendManifest m = FrontendManifest::parse(manifest, &errors);
        QVERIFY(m.id.isEmpty());
        QVERIFY2(errors.join("; ").contains(error), qPrintable(errors.join("; ")));
    }

    void safeRelativePaths()
    {
        for (const char *ok : {"dist", "a/b/c.js", "bin/dash", "x.y"})
            QVERIFY2(FrontendManifest::safeRelativePath(ok), ok);
        for (const char *bad : {"", "/abs", "../x", "a/../b", "a//b", "./a", "a\\b", "C:/x", "a/", "."})
            QVERIFY2(!FrontendManifest::safeRelativePath(bad), bad);
    }

    // ── Installing ──
    void installsFromAFolderWithoutRunningAnything()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        const QString src = makeWebFolder(tmp, webManifest());
        // A program that would leave a mark if anything ran it
        write(src + "/install.sh", "#!/bin/sh\ntouch \"" + tmp.filePath("ran").toUtf8() + "\"\n");
        QFile(src + "/install.sh").setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QSignalSpy changed(&fm, &FrontendManager::frontendsChanged);

        QCOMPARE(fm.installFromFolder(src), QStringLiteral("com.example.dash"));
        QVERIFY(fm.lastError().isEmpty());
        QCOMPARE(changed.count(), 1);
        QVERIFY(QFile::exists(tmp.filePath("frontends/com.example.dash/dist/index.html")));
        QVERIFY(QFileInfo(tmp.filePath("frontends/com.example.dash/install.sh")).isExecutable());
        QVERIFY(!QFile::exists(tmp.filePath("ran")));
        const QVariantList list = fm.frontends();
        QCOMPARE(list.size(), 1);
        const QVariantMap row = list.first().toMap();
        QCOMPARE(row.value("kind").toString(), QStringLiteral("web"));
        QCOMPARE(row.value("scopes").toStringList(), (QStringList{"read", "media"}));
        QVERIFY(row.value("compatible").toBool());
        QVERIFY(!row.value("active").toBool());
        // No staging folder left behind
        QCOMPARE(QDir(tmp.filePath("frontends")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList{"com.example.dash"});
    }

    void reinstallingReplaces()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        QJsonObject m = webManifest();
        const QString src = makeWebFolder(tmp, m);
        write(src + "/old.txt", "old");
        QVERIFY(!fm.installFromFolder(src).isEmpty());
        m.insert("version", "2.0.0");
        const QString src2 = makeWebFolder(tmp, m, "src2");
        QVERIFY(!fm.installFromFolder(src2).isEmpty());
        QCOMPARE(fm.manifestOf("com.example.dash").value("version").toString(), QStringLiteral("2.0.0"));
        QVERIFY(!QFile::exists(tmp.filePath("frontends/com.example.dash/old.txt")));
    }

    void folderRefusals()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        auto refused = [&](const QString &src, const char *why) {
            const QString id = fm.installFromFolder(src);
            QVERIFY2(id.isEmpty(), why);
            QVERIFY2(fm.lastError().contains(QLatin1String(why)), qPrintable(fm.lastError()));
            QVERIFY(fm.frontends().isEmpty());
        };
        refused(tmp.filePath("missing"), "is not a folder");
        QDir().mkpath(tmp.filePath("empty"));
        refused(tmp.filePath("empty"), "has no octave-frontend.json");

        // A web root without index.html
        const QString noIndex = tmp.filePath("noindex");
        write(noIndex + "/octave-frontend.json", json(webManifest()));
        refused(noIndex, "has no index.html");

        // A protocol this OCTAVE doesn't speak
        QJsonObject future = webManifest();
        future.insert("protocol", QJsonArray{OctaveService::kProtocolVersion + 1, OctaveService::kProtocolVersion + 2});
        refused(makeWebFolder(tmp, future, "future"), "can't run here");

#ifdef Q_OS_UNIX
        // A link, which could point anywhere
        const QString linked = makeWebFolder(tmp, webManifest(), "linked");
        QVERIFY(QFile::link("/etc/passwd", linked + "/dist/passwd"));
        refused(linked, "symbolic link");
#endif
        // The frontends folder itself
        refused(fm.root(), "holds OCTAVE's own frontends");
        QVERIFY(!fm.lastError().isEmpty());
    }

    void installsFromAZip()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        // Zipped as a folder ("dash/..."), the usual way
        const QString zipPath = tmp.filePath("dash.zip");
        {
            QZipWriter z(zipPath);
            z.addFile("dash/octave-frontend.json", json(webManifest()));
            z.addFile("dash/bin/run", "#!/bin/sh\n");
            z.addFile("dash/dist/index.html", "<!doctype html>");
            z.close();
        }
        setZipMode(zipPath, "dash/bin/run", 0100755);
        QCOMPARE(fm.installFromZip(zipPath), QStringLiteral("com.example.dash"));
        QVERIFY(QFile::exists(tmp.filePath("frontends/com.example.dash/dist/index.html")));
#ifdef Q_OS_UNIX
        QVERIFY(QFileInfo(tmp.filePath("frontends/com.example.dash/bin/run")).isExecutable());
#endif
    }

    void zipRefusals_data()
    {
        QTest::addColumn<QStringList>("names");
        QTest::addColumn<QString>("error");
        QTest::newRow("parent") << QStringList{"octave-frontend.json", "dist/index.html", "../evil.txt"} << "points outside";
        QTest::newRow("nested parent") << QStringList{"octave-frontend.json", "dist/index.html", "dist/../../evil.txt"} << "points outside";
        QTest::newRow("absolute") << QStringList{"octave-frontend.json", "dist/index.html", "/tmp/evil.txt"} << "points outside";
        QTest::newRow("backslash") << QStringList{"octave-frontend.json", "dist/index.html", "..\\evil.txt"} << "points outside";
        QTest::newRow("drive") << QStringList{"octave-frontend.json", "dist/index.html", "C:/evil.txt"} << "points outside";
        QTest::newRow("no manifest") << QStringList{"dist/index.html", "readme.txt"} << "no octave-frontend.json";
    }

    void zipRefusals()
    {
        QFETCH(QStringList, names);
        QFETCH(QString, error);
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        const QString zipPath = tmp.filePath("bad.zip");
        {
            QZipWriter z(zipPath);
            for (const QString &n : names)
                z.addFile(n, n.endsWith(".json") ? json(webManifest()) : QByteArray("x"));
            z.close();
        }
        QVERIFY(fm.installFromZip(zipPath).isEmpty());
        QVERIFY2(fm.lastError().contains(error), qPrintable(fm.lastError()));
        QVERIFY(fm.frontends().isEmpty());
        QVERIFY(!QFile::exists(tmp.filePath("evil.txt")));
        QCOMPARE(QDir(tmp.filePath("frontends")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList());
    }

    void zipWithALinkIsRefused()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        const QString zipPath = tmp.filePath("link.zip");
        {
            QZipWriter z(zipPath);
            z.addFile("octave-frontend.json", json(webManifest()));
            z.addFile("dist/index.html", "x");
            z.addFile("dist/passwd", "/etc/passwd");
            z.close();
        }
        setZipMode(zipPath, "dist/passwd", 0120777);   // a link, as zip -y stores one
        QVERIFY(fm.installFromZip(zipPath).isEmpty());
        QVERIFY2(fm.lastError().contains("symbolic link"), qPrintable(fm.lastError()));
    }

    void notAZip()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        write(tmp.filePath("x.zip"), "this is not a zip file");
        QVERIFY(fm.installFromZip(tmp.filePath("x.zip")).isEmpty());
        QVERIFY(!fm.lastError().isEmpty());
    }

    // ── The active screen and the next start ──
    void activeChoiceAndBoot()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        const QString root = tmp.filePath("frontends");
        fm.setRoot(root);
        QCOMPARE(FrontendManager::bootChoice(root, false).kind, FrontendManager::Boot::Qml);
        QVERIFY(!fm.setActive("nope"));
        QVERIFY(!fm.setActive("../x"));
        QVERIFY(!fm.installFromFolder(makeWebFolder(tmp, webManifest())).isEmpty());
        QSignalSpy active(&fm, &FrontendManager::activeChanged);
        QVERIFY(fm.setActive("com.example.dash"));
        QCOMPARE(active.count(), 1);
        QCOMPARE(fm.activeId(), QStringLiteral("com.example.dash"));
        QVERIFY(fm.frontends().first().toMap().value("active").toBool());

        FrontendManager::Boot boot = FrontendManager::bootChoice(root, false);
        QCOMPARE(boot.kind, FrontendManager::Boot::Web);
        QCOMPARE(boot.dir, QDir(root).filePath("com.example.dash"));
        // --qml and USE_QML only stop exec frontends; a web one is served anyway
        QCOMPARE(FrontendManager::bootChoice(root, true).kind, FrontendManager::Boot::Web);

        QVERIFY(fm.setActive(""));
        QCOMPARE(fm.activeId(), QString());
        QCOMPARE(FrontendManager::bootChoice(root, false).kind, FrontendManager::Boot::Qml);
    }

    void execBootAndTheEscapeHatch()
    {
#if !QT_CONFIG(process) || defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        QSKIP("exec frontends need a desktop build");
#else
        QTemporaryDir tmp;
        FrontendManager fm;
        const QString root = tmp.filePath("frontends");
        fm.setRoot(root);
        QJsonObject m = webManifest("org.test.exec");
        m.remove("web");
        m.insert("exec", QJsonObject{{FrontendManifest::currentPlatform(), QJsonArray{"bin/run"}}});
        const QString src = tmp.filePath("exec");
        write(src + "/octave-frontend.json", json(m));
        write(src + "/bin/run", "#!/bin/sh\n");
        QCOMPARE(fm.installFromFolder(src), QStringLiteral("org.test.exec"));
        QVERIFY(fm.setActive("org.test.exec"));
        QCOMPARE(FrontendManager::bootChoice(root, false).kind, FrontendManager::Boot::Exec);

        // --qml: this run only
        FrontendManager::Boot boot = FrontendManager::bootChoice(root, true);
        QCOMPARE(boot.kind, FrontendManager::Boot::Qml);
        QVERIFY(boot.note.contains("--qml"));
        // USE_QML: until it is cleared
        FrontendManager::forceQml(root, "it crashed 5 times");
        boot = FrontendManager::bootChoice(root, false);
        QCOMPARE(boot.kind, FrontendManager::Boot::Qml);
        QVERIFY2(boot.note.contains("it crashed 5 times"), qPrintable(boot.note));
        QVERIFY(fm.forcedQml());
        QCOMPARE(fm.forcedQmlReason(), QStringLiteral("it crashed 5 times"));
        fm.clearForcedQml();
        QVERIFY(!fm.forcedQml());
        QCOMPARE(FrontendManager::bootChoice(root, false).kind, FrontendManager::Boot::Exec);

        // A program missing after install: the QML UI, with the reason
        QFile::remove(QDir(root).filePath("org.test.exec/bin/run"));
        boot = FrontendManager::bootChoice(root, false);
        QCOMPARE(boot.kind, FrontendManager::Boot::Qml);
        QVERIFY2(boot.note.contains("broken"), qPrintable(boot.note));
#endif
    }

    void removeAndTheRunningOne()
    {
        QTemporaryDir tmp;
        FrontendManager fm;
        fm.setRoot(tmp.filePath("frontends"));
        QVERIFY(!fm.installFromFolder(makeWebFolder(tmp, webManifest())).isEmpty());
        QVERIFY(fm.setActive("com.example.dash"));
        fm.setRunningId("com.example.dash");
        QVERIFY(!fm.remove("com.example.dash"));
        QVERIFY(fm.lastError().contains("is running"));
        QVERIFY(fm.installFromFolder(makeWebFolder(tmp, webManifest(), "again")).isEmpty());   // nor replace it
        fm.setRunningId({});
        QVERIFY(!fm.remove("../frontends"));
        QVERIFY(!fm.remove(".."));
        QVERIFY(fm.remove("com.example.dash"));
        QVERIFY(fm.frontends().isEmpty());
        QCOMPARE(fm.activeId(), QString());   // no longer active
        QVERIFY(QDir(tmp.path()).exists());
    }

    void leftoversAreCleanedAtStart()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("frontends");
        QDir().mkpath(root + "/.staging-1234/x");
        QDir().mkpath(root + "/.old-9876");
        FrontendManager fm;
        fm.setRoot(root);
        QCOMPARE(QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList());
    }
};

QTEST_GUILESS_MAIN(TstFrontendManager)
#include "tst_frontendmanager.moc"
