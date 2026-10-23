// What a picker hands back, as something to open (src/util/pathurl): the
// Media page used to decode Android tree URIs and Windows drive letters by
// hand, kept percent escapes in desktop paths, and saved an SD card's tree
// id ("1234-5678:Music") as if it were a path.

#include <QTest>

#include "util/pathurl.h"

class TstPathUrl : public QObject
{
    Q_OBJECT

private slots:
    void folders_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("path");
        QTest::newRow("desktop") << "file:///home/rob/My%20Music" << "/home/rob/My Music";
        QTest::newRow("windows drive") << "file:///C:/Users/Rob/Music" << "C:/Users/Rob/Music";
        QTest::newRow("android primary")
            << "content://com.android.externalstorage.documents/tree/primary%3AMusic" << "/storage/emulated/0/Music";
        QTest::newRow("android subfolder")
            << "content://com.android.externalstorage.documents/tree/primary%3AMusic%2FRock"
            << "/storage/emulated/0/Music/Rock";
        QTest::newRow("android storage root")
            << "content://com.android.externalstorage.documents/tree/primary%3A" << "/storage/emulated/0";
        QTest::newRow("sd card")
            << "content://com.android.externalstorage.documents/tree/1234-5678%3AMusic" << "/storage/1234-5678/Music";
        QTest::newRow("tree with document")
            << "content://com.android.externalstorage.documents/tree/primary%3AMusic/document/primary%3AMusic"
            << "/storage/emulated/0/Music";
        QTest::newRow("other provider")
            << "content://com.google.android.apps.docs.storage/tree/abc" << "";
        QTest::newRow("web") << "https://example.com/music" << "";
        QTest::newRow("empty") << "" << "";
    }

    void folders()
    {
        QFETCH(QString, url);
        QFETCH(QString, path);
        QCOMPARE(PathUrl::folderPath(QUrl(url)), path);
    }

    void files()
    {
        QCOMPARE(PathUrl::openablePath("file:///tmp/dash%201.json"), QStringLiteral("/tmp/dash 1.json"));
        QCOMPARE(PathUrl::openablePath("content://x/document/7"), QStringLiteral("content://x/document/7"));
        QCOMPARE(PathUrl::openablePath("/tmp/plain.json"), QStringLiteral("/tmp/plain.json"));
    }
};

QTEST_GUILESS_MAIN(TstPathUrl)
#include "tst_pathurl.moc"
