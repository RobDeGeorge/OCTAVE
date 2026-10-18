// Download helpers (src/managers/downloadutil.cpp): yt-dlp search output to
// the result objects the QML download page binds to, the library scan that
// marks songs as already downloaded, and power-cut-safe staging (a song only
// appears under its library name once it is complete). The C++ counterpart of
// tests/test_download_ytm_parse.py and tests/test_download_partial_file.py;
// the C++ backend searches through yt-dlp, not ytmusicapi.

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "managers/downloadutil.h"

namespace {
const QStringList kAudio{".mp3", ".flac", ".ogg", ".opus", ".m4a", ".mp4"};

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
} // namespace

class TstDownloadUtil : public QObject
{
    Q_OBJECT

private slots:
    void parseMapsFieldsAndSkipsJunk()
    {
        const QByteArray out =
            R"({"id": "abc123", "title": "Title", "channel": "Artist", "album": "Album", "duration": 3723.0,)"
            R"( "release_year": 2020, "thumbnails": [{"url": "https://x/small.jpg"},)"
            R"( {"url": "https://lh3.googleusercontent.com/x=w60-h60-l90-rj"}]})"
            "\n"
            "not json at all\n"
            R"({"title": "no id, skipped"})"
            "\n\n";
        const QJsonArray results = DownloadUtil::parseSearchOutput(out);
        QCOMPARE(results.size(), 1);
        const QJsonObject r = results.at(0).toObject();
        QCOMPARE(r.value("name").toString(), QStringLiteral("Title"));
        QCOMPARE(r.value("artist").toString(), QStringLiteral("Artist"));
        QCOMPARE(r.value("artists").toArray(), QJsonArray{"Artist"});
        QCOMPARE(r.value("album_name").toString(), QStringLiteral("Album"));
        QCOMPARE(r.value("duration").toInt(), 3723);
        QCOMPARE(r.value("year").toInt(), 2020);
        QCOMPARE(r.value("song_id").toString(), QStringLiteral("abc123"));
        QCOMPARE(r.value("url").toString(), QStringLiteral("https://music.youtube.com/watch?v=abc123"));
        // The last (largest) thumbnail, upgraded to the CDN's high-res variant
        QVERIFY(r.value("cover_url").toString().endsWith("=w800-h800-l90-rj"));
    }

    void parseFallsBackAndToleratesMissingFields()
    {
        const QByteArray out = R"({"id": "x1", "uploader": "Uploader", "upload_date": "19991231", "duration": null})";
        const QJsonObject r = DownloadUtil::parseSearchOutput(out).at(0).toObject();
        QCOMPARE(r.value("artist").toString(), QStringLiteral("Uploader"));
        QCOMPARE(r.value("year").toInt(), 1999);
        QCOMPARE(r.value("duration").toInt(), 0);
        QCOMPARE(r.value("cover_url").toString(), QString());

        const QJsonObject bare = DownloadUtil::parseSearchOutput(R"({"id": "y2"})").at(0).toObject();
        QCOMPARE(bare.value("year").toInt(), 0);
        QCOMPARE(bare.value("name").toString(), QString());
    }

    void scanSkipsStagedPartialsAndNamesFolders()
    {
        QTemporaryDir root;
        writeFile(root.filePath("Artist - Loose.mp3"), "x");
        writeFile(root.filePath("Road Trip/Artist - Song.flac"), "x");
        writeFile(root.filePath("Road Trip/cover.jpg"), "x");
        // A power cut left a truncated song in staging: not downloaded
        writeFile(root.filePath(".octave-partial/abc/Artist - Cut Off.mp3"), "trunc");

        const QMap<QString, QString> found = DownloadUtil::scanDownloadedFiles({root.path()}, kAudio);
        QCOMPARE(found.value("artist - loose"), QStringLiteral("Unsorted"));
        QCOMPARE(found.value("artist - song"), QStringLiteral("Road Trip"));
        QVERIFY(!found.contains("cover"));
        QVERIFY(!found.contains("artist - cut off"));
        QCOMPARE(found.size(), 2);
    }

#ifndef Q_OS_MOBILE
    void stagingDirIsHiddenAndSanitized()
    {
        const QString dir = DownloadUtil::stagingDirFor("/music", "a/b?c");
        QCOMPARE(dir, QStringLiteral("/music/.octave-partial/a_b_c"));
        QCOMPARE(DownloadUtil::stagingDirFor("/music", ""), QStringLiteral("/music/.octave-partial/song"));
        QVERIFY(QFileInfo(dir).fileName() != QLatin1String(".."));
    }

    void removeStagingDirRefusesRealFolders()
    {
        QTemporaryDir root;
        writeFile(root.filePath("Library/keep.mp3"), "x");
        DownloadUtil::removeStagingDir(root.filePath("Library"));
        DownloadUtil::removeStagingDir(QString());
        QVERIFY(QFile::exists(root.filePath("Library/keep.mp3")));

        const QString staged = DownloadUtil::stagingDirFor(root.path(), "id1");
        writeFile(staged + "/song.webm", "x");
        DownloadUtil::removeStagingDir(staged);
        QVERIFY(!QFileInfo::exists(staged));
    }

    void songAppearsOnlyWhenPublished()
    {
        QTemporaryDir root;
        const QString staging = DownloadUtil::stagingDirFor(root.path(), "id1");
        const QString staged = staging + "/Artist - Song.mp3";
        const QString final = root.filePath("Artist - Song.mp3");
        writeFile(staged, "mp3 audio");

        // Converting and tagging happen in staging: nothing at the final name yet
        QVERIFY(!QFile::exists(final));
        QVERIFY(DownloadUtil::scanDownloadedFiles({root.path()}, kAudio).isEmpty());

        QString error;
        QVERIFY2(DownloadUtil::publishStagedFile(staged, final, &error), qPrintable(error));
        DownloadUtil::removeStagingDir(staging);
        QFile f(final);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("mp3 audio"));
        QVERIFY(!QFileInfo::exists(staging));
        QCOMPARE(DownloadUtil::scanDownloadedFiles({root.path()}, kAudio).value("artist - song"),
                 QStringLiteral("Unsorted"));
    }

    void publishReplacesAnOldFile()
    {
        QTemporaryDir root;
        const QString staged = DownloadUtil::stagingDirFor(root.path(), "id2") + "/a.mp3";
        writeFile(staged, "new");
        writeFile(root.filePath("a.mp3"), "old truncated");
        QString error;
        QVERIFY(DownloadUtil::publishStagedFile(staged, root.filePath("a.mp3"), &error));
        QFile f(root.filePath("a.mp3"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("new"));
    }
#endif
};

QTEST_GUILESS_MAIN(TstDownloadUtil)
#include "tst_downloadutil.moc"
