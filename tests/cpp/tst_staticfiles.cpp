// --web-root (src/service/staticfiles.{h,cpp}): a frontend directory served
// at GET /. Content types by extension, the single-page-app fallback, and
// every way out of the root a hostile page or client could ask for: "..",
// percent-encoded "..", backslashes, NUL, dotfiles, and symlinks (a file
// and a directory) that point outside.

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "service/staticfiles.h"

using namespace OctaveService;

class TstStaticFiles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QDir t(m_tmp.path());
        QVERIFY(t.mkpath("root/assets"));
        QVERIFY(t.mkpath("root/docs"));
        QVERIFY(t.mkpath("root/empty"));
        QVERIFY(t.mkpath("outside"));
        m_root = t.filePath("root");
        write("root/index.html", "<html>app</html>");
        write("root/assets/app.js", "console.log(1)");
        write("root/assets/app.css", "body{}");
        write("root/assets/logo.svg", "<svg/>");
        write("root/assets/font.woff2", "wOF2");
        write("root/assets/core.wasm", "\0asm");
        write("root/assets/data.json", "{}");
        write("root/assets/blob.xyz", "?");
        write("root/docs/index.html", "<html>docs</html>");
        write("root/.env", "SECRET=1");
        write("outside/secret.txt", "secret");
        // Links: one inside (allowed), a file and a directory outside (refused)
        QVERIFY(QFile::link(t.filePath("root/assets/app.js"), t.filePath("root/alias.js")));
        QVERIFY(QFile::link(t.filePath("outside/secret.txt"), t.filePath("root/leak.txt")));
        QVERIFY(QFile::link(t.filePath("outside"), t.filePath("root/out")));
        QVERIFY(QFile::link(t.filePath("nowhere"), t.filePath("root/dangling.txt")));
    }

    void contentTypes_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("type");
        QTest::newRow("html") << "a.html" << QByteArray("text/html; charset=utf-8");
        QTest::newRow("js") << "a.js" << QByteArray("text/javascript; charset=utf-8");
        QTest::newRow("mjs") << "a.mjs" << QByteArray("text/javascript; charset=utf-8");
        QTest::newRow("css") << "a.css" << QByteArray("text/css; charset=utf-8");
        QTest::newRow("json") << "a.json" << QByteArray("application/json");
        QTest::newRow("svg") << "a.svg" << QByteArray("image/svg+xml");
        QTest::newRow("png") << "a.png" << QByteArray("image/png");
        QTest::newRow("jpg") << "a.JPG" << QByteArray("image/jpeg");
        QTest::newRow("webp") << "a.webp" << QByteArray("image/webp");
        QTest::newRow("woff2") << "a.woff2" << QByteArray("font/woff2");
        QTest::newRow("wasm") << "a.wasm" << QByteArray("application/wasm");
        QTest::newRow("ico") << "favicon.ico" << QByteArray("image/x-icon");
        QTest::newRow("unknown") << "a.xyz" << QByteArray("application/octet-stream");
        QTest::newRow("none") << "Makefile" << QByteArray("application/octet-stream");
    }
    void contentTypes()
    {
        QFETCH(QString, name);
        QFETCH(QByteArray, type);
        QCOMPARE(StaticFiles::contentTypeFor(name), type);
    }

    void servesFiles_data()
    {
        QTest::addColumn<QByteArray>("target");
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("root") << QByteArray("/") << 200 << QByteArray("<html>app</html>");
        QTest::newRow("index") << QByteArray("/index.html") << 200 << QByteArray("<html>app</html>");
        QTest::newRow("query") << QByteArray("/assets/app.js?v=3") << 200 << QByteArray("console.log(1)");
        QTest::newRow("fragment") << QByteArray("/assets/app.css#x") << 200 << QByteArray("body{}");
        QTest::newRow("encoded") << QByteArray("/assets/%61pp.js") << 200 << QByteArray("console.log(1)");
        QTest::newRow("double slash") << QByteArray("//assets//app.js") << 200 << QByteArray("console.log(1)");
        QTest::newRow("dot segment") << QByteArray("/./assets/./app.js") << 200 << QByteArray("console.log(1)");
        QTest::newRow("subdir index") << QByteArray("/docs/") << 200 << QByteArray("<html>docs</html>");
        QTest::newRow("link inside") << QByteArray("/alias.js") << 200 << QByteArray("console.log(1)");
        // Client routes of a single-page app get its page
        QTest::newRow("spa route") << QByteArray("/settings/display") << 200 << QByteArray("<html>app</html>");
        QTest::newRow("missing file") << QByteArray("/assets/nope.js") << 404 << QByteArray();
        QTest::newRow("dir without index") << QByteArray("/empty") << 404 << QByteArray();
    }
    void servesFiles()
    {
        QFETCH(QByteArray, target);
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        const StaticFiles::Reply r = StaticFiles::serve(m_root, target);
        QCOMPARE(r.status, status);
        if (status == 200)
            QCOMPARE(r.body, body);
    }

    void refusesWaysOut_data()
    {
        QTest::addColumn<QByteArray>("target");
        QTest::addColumn<int>("status");
        QTest::newRow("parent") << QByteArray("/../outside/secret.txt") << 403;
        QTest::newRow("parent deep") << QByteArray("/assets/../../outside/secret.txt") << 403;
        QTest::newRow("parent inside") << QByteArray("/assets/../index.html") << 403;
        QTest::newRow("encoded parent") << QByteArray("/%2e%2e/outside/secret.txt") << 403;
        QTest::newRow("encoded slash") << QByteArray("/..%2foutside%2fsecret.txt") << 403;
        QTest::newRow("backslash") << QByteArray("/..\\outside\\secret.txt") << 400;
        QTest::newRow("encoded backslash") << QByteArray("/%5c..%5coutside") << 400;
        QTest::newRow("nul") << QByteArray("/index.html%00.js") << 400;
        QTest::newRow("absolute url") << QByteArray("http://evil/index.html") << 400;
        QTest::newRow("not a path") << QByteArray("*") << 400;
        QTest::newRow("dotfile") << QByteArray("/.env") << 404;
        QTest::newRow("dot dir") << QByteArray("/.git/config") << 404;
        QTest::newRow("file link out") << QByteArray("/leak.txt") << 403;
        QTest::newRow("dir link out") << QByteArray("/out/secret.txt") << 403;
        QTest::newRow("dangling link") << QByteArray("/dangling.txt") << 403;
    }
    void refusesWaysOut()
    {
        QFETCH(QByteArray, target);
        QFETCH(int, status);
        const StaticFiles::Reply r = StaticFiles::serve(m_root, target);
        QCOMPARE(r.status, status);
        QVERIFY(!r.body.contains("secret"));
    }

    void imagesAreTypedByTheirBytes()
    {
        QCOMPARE(StaticFiles::imageTypeOf(QByteArray::fromHex("ffd8ffe000104a464946")), QByteArray("image/jpeg"));
        QCOMPARE(StaticFiles::imageTypeOf(QByteArray::fromHex("89504e470d0a1a0a0000")), QByteArray("image/png"));
        QCOMPARE(StaticFiles::imageTypeOf("GIF89a...."), QByteArray("image/gif"));
        QCOMPARE(StaticFiles::imageTypeOf(QByteArray("RIFF\x10\0\0\0WEBPVP8 ", 16)), QByteArray("image/webp"));
        QVERIFY(StaticFiles::imageTypeOf("<html>").isEmpty());
        QVERIFY(StaticFiles::imageTypeOf("").isEmpty());
    }

    void serveImageOnlyServesImages()
    {
        const QString jpg = QDir(m_tmp.path()).filePath("cover.img");   // the art cache's own extension
        write("cover.img", QByteArray::fromHex("ffd8ffe000104a464946") + QByteArray(100, 'x'));
        const StaticFiles::Reply r = StaticFiles::serveImage(jpg);
        QCOMPARE(r.status, 200);
        QCOMPARE(r.contentType, QByteArray("image/jpeg"));
        QCOMPARE(r.body.size(), 110);
        // Not an image (were the art path ever wrong): nothing leaks
        QCOMPARE(StaticFiles::serveImage(QDir(m_tmp.path()).filePath("outside/secret.txt")).status, 404);
        QVERIFY(!StaticFiles::serveImage(QDir(m_tmp.path()).filePath("outside/secret.txt")).body.contains("secret"));
        QCOMPARE(StaticFiles::serveImage(QString()).status, 404);
        QCOMPARE(StaticFiles::serveImage(m_tmp.filePath("missing.jpg")).status, 404);
        QCOMPARE(StaticFiles::serveImage(m_tmp.path()).status, 404);   // a directory
    }

    void pathOfDropsQueryAndFragment()
    {
        QCOMPARE(StaticFiles::pathOf("/art?rev=3"), QByteArray("/art"));
        QCOMPARE(StaticFiles::pathOf("/art#x"), QByteArray("/art"));
        QCOMPARE(StaticFiles::pathOf("/art"), QByteArray("/art"));
    }

    void queryAndConfinement()
    {
        QCOMPARE(StaticFiles::queryValue("/art?song=My%20Song%20%26%20Co.mp3&rev=3", "song"), QStringLiteral("My Song & Co.mp3"));
        QCOMPARE(StaticFiles::queryValue("/art?song=a+b.mp3", "song"), QStringLiteral("a b.mp3"));
        QCOMPARE(StaticFiles::queryValue("/art?queue=12#x", "queue"), QStringLiteral("12"));
        QVERIFY(StaticFiles::queryValue("/art?rev=3", "song").isNull());
        QVERIFY(StaticFiles::queryValue("/art", "song").isNull());
        QVERIFY(!StaticFiles::queryValue("/art?song=", "song").isNull());   // present, empty

        QCOMPARE(StaticFiles::insideDir(m_root + "/assets/app.js", m_root), QFileInfo(m_root + "/assets/app.js").canonicalFilePath());
        QVERIFY(StaticFiles::insideDir(m_root + "/leak.txt", m_root).isEmpty());          // link out
        QVERIFY(StaticFiles::insideDir(m_root + "/../outside/secret.txt", m_root).isEmpty());
        QVERIFY(StaticFiles::insideDir(m_root, m_root).isEmpty());                         // the folder itself
        QVERIFY(StaticFiles::insideDir(m_root + "/nope", m_root).isEmpty());
    }

    void downloadsAreAttachments()
    {
        QDir t(m_tmp.path());
        QVERIFY(t.mkpath("export"));
        write("export/OCTAVE-trip-1.csv", "t,rpm\n");
        write("export/odd name\";x.log", "line\n");
        StaticFiles::Reply r = StaticFiles::serveDownload(t.filePath("export/OCTAVE-trip-1.csv"));
        QCOMPARE(r.status, 200);
        QCOMPARE(r.contentType, QByteArray("text/csv; charset=utf-8"));
        QCOMPARE(r.headers, QByteArray("Content-Disposition: attachment; filename=\"OCTAVE-trip-1.csv\"\r\n"));
        QCOMPARE(r.body, QByteArray("t,rpm\n"));
        // A name that could break the header is sanitised
        r = StaticFiles::serveDownload(t.filePath("export/odd name\";x.log"));
        QCOMPARE(r.headers, QByteArray("Content-Disposition: attachment; filename=\"odd_name__x.log\"\r\n"));
        QCOMPARE(StaticFiles::serveDownload(QString()).status, 404);   // nothing exported yet
        QCOMPARE(StaticFiles::serveDownload(t.filePath("export")).status, 404);
        QCOMPARE(StaticFiles::serveDownload(t.filePath("export/OCTAVE-trip-1.csv"), 3).status, 413);
    }

    void exportFoldersListAndServeOnlyTheirOwnFiles()
    {
        QDir t(m_tmp.path());
        QVERIFY(t.mkpath("logs/sub"));
        write("logs/octave-cpp.log", "hello\n");
        write("logs/device-info.txt", "pi\n");
        write("logs/.secret", "x");
        const QString dir = t.filePath("logs");
        StaticFiles::Reply r = StaticFiles::serveFromFolder(dir, QString());
        QCOMPARE(r.status, 200);
        QCOMPARE(r.body, QByteArray(R"(["device-info.txt","octave-cpp.log"])"));   // no dotfile, no folder
        r = StaticFiles::serveFromFolder(dir, "octave-cpp.log");
        QCOMPARE(r.status, 200);
        QCOMPARE(r.body, QByteArray("hello\n"));
        QVERIFY(r.headers.startsWith("Content-Disposition: attachment"));
        for (const char *bad : {"../outside/secret.txt", ".secret", "sub", "sub/x", "/etc/passwd", "nope.log"})
            QCOMPARE(StaticFiles::serveFromFolder(dir, QString::fromLatin1(bad)).status, 404);
        QCOMPARE(StaticFiles::serveFromFolder(QString(), QString()).status, 404);
    }

    void rootMustExist()
    {
        QCOMPARE(StaticFiles::serve(m_tmp.filePath("missing"), "/").status, 404);
        QCOMPARE(StaticFiles::serve(QString(), "/").status, 404);
    }

    void rootThroughALinkStillWorks()
    {
        const QString link = m_tmp.filePath("rootlink");
        QVERIFY(QFile::link(m_root, link));
        QCOMPARE(StaticFiles::serve(link, "/assets/app.js").status, 200);
        QCOMPARE(StaticFiles::serve(link, "/leak.txt").status, 403);
    }

private:
    void write(const QString &rel, const QByteArray &data)
    {
        QFile f(QDir(m_tmp.path()).filePath(rel));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    QTemporaryDir m_tmp;
    QString m_root;
};

QTEST_GUILESS_MAIN(TstStaticFiles)
#include "tst_staticfiles.moc"
