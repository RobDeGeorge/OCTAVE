// MediaManager playback robustness: a missing track must not silently stop
// playback, shuffle must survive a playlist re-select (what a library rescan
// does), and a truncated cover in the cache is never served. Runs against a
// real MediaManager and a scanned temp library (the "songs" are placeholder bytes; nothing here
// waits for the player to decode them).

#include <QAudioDevice>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

#include "managers/mediamanager.h"
#include "managers/settingsmanager.h"

class TstMediaManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
        qputenv("APPDATA", (m_home.path() + "/appdata").toUtf8());
    }

    void pickedAudioOutputIsUsedWhilePresent()
    {
        SettingsManager sm;
        MediaManager mm;
        mm.setSettingsManager(&sm);
        QSignalSpy outputs(&mm, &MediaManager::audioOutputsChanged);

        // Not plugged in (CI has no outputs at all): the system default
        sm.save_audio_output_device(QStringLiteral("no-such-output"));
        QCOMPARE(outputs.count(), 1);
        QCOMPARE(mm.m_audioOutput->device(), QMediaDevices::defaultAudioOutput());

        // With a real second output, picking it moves playback there and back
        const QList<QAudioDevice> all = QMediaDevices::audioOutputs();
        for (const QAudioDevice &d : all) {
            if (d.isDefault())
                continue;
            sm.save_audio_output_device(QString::fromUtf8(d.id()));
            QCOMPARE(mm.m_audioOutput->device(), d);
            QCOMPARE(mm.currentAudioOutput(), d.description());
            sm.save_audio_output_device(QString());
            QCOMPARE(mm.m_audioOutput->device(), QMediaDevices::defaultAudioOutput());
            break;
        }
        QCOMPARE(mm.audioOutputs().size(), all.size());
    }

    void missingTrackSkipsToNextPlayable()
    {
        auto mm = library({"a.mp3", "b.mp3", "c.mp3"});
        removeSongs({"a.mp3", "c.mp3"});   // deleted since the scan
        QSignalSpy started(mm.get(), &MediaManager::currentMediaChanged);

        mm->play_file("a.mp3");

        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().at(0).toString(), QStringLiteral("b.mp3"));
        QVERIFY(mm->is_playing());
        QCOMPARE(mm->get_current_file(), QStringLiteral("b.mp3"));
    }

    void allTracksMissingStopsCleanly()
    {
        auto mm = library({"a.mp3", "b.mp3", "c.mp3"});
        removeSongs({"a.mp3", "b.mp3", "c.mp3"});
        QSignalSpy state(mm.get(), &MediaManager::playStateChanged);
        QSignalSpy started(mm.get(), &MediaManager::currentMediaChanged);

        mm->play_file("a.mp3");

        QVERIFY(!mm->is_playing());
        QVERIFY(!state.isEmpty());
        QCOMPARE(state.last().at(0).toBool(), false);
        QVERIFY(started.isEmpty());
        QCOMPARE(mm->m_consecutiveBadTracks, 0);   // reset for the next attempt
    }

    void shuffleSurvivesReselectWithCurrentSongFirst()
    {
        QStringList files;
        for (int i = 0; i < 40; ++i)
            files << QStringLiteral("%1.mp3").arg(i, 2, 10, QLatin1Char('0'));
        auto mm = library(files);
        mm->play_file("17.mp3");
        mm->toggle_shuffle();
        QVERIFY(mm->is_shuffled());

        mm->select_playlist(m_playlist);   // what a library rescan does

        QStringList list = mm->get_current_song_list();
        QCOMPARE(list.first(), QStringLiteral("17.mp3"));
        QStringList sorted = list;
        sorted.sort();
        QCOMPARE(sorted, files);
        // 39! orderings: an alphabetical result would mean shuffle was dropped
        QStringList rest = list.mid(1), restSorted = rest;
        restSorted.sort();
        QVERIFY(rest != restSorted);
    }

    void selectPlaylistWithoutShuffleIsAlphabetical()
    {
        auto mm = library({"c.mp3", "a.mp3", "b.mp3"});
        mm->select_playlist(m_playlist);
        QCOMPARE(mm->get_current_song_list(), (QStringList{"a.mp3", "b.mp3", "c.mp3"}));
    }

    void sanitizeMetadata_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("plain") << QStringLiteral("No One Knows") << QStringLiteral("No One Knows");
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("only spaces") << QStringLiteral("   ") << QString();
        QTest::newRow("control chars become spaces, collapsed")
            << (QStringLiteral("Track") + QChar(0x01) + QStringLiteral("Name") + QChar(0x1f) + QChar(0)
                + QStringLiteral("End"))
            << QStringLiteral("Track Name End");
        QTest::newRow("tabs and newlines")
            << QStringLiteral("  Song\tTitle\r\nPart 2  ") << QStringLiteral("Song Title Part 2");
        QTest::newRow("accents (NFC)") << QStringLiteral("Beyonc\u00e9") << QStringLiteral("Beyonc\u00e9");
        QTest::newRow("combining mark kept (NFD)")
            << QStringLiteral("Beyonce\u0301") << QStringLiteral("Beyonce\u0301");
        // UTF-8 read as Latin-1 can't be told apart from real Latin-1 text: kept as is
        QTest::newRow("mojibake kept") << QStringLiteral("Beyonc\u00c3\u00a9") << QStringLiteral("Beyonc\u00c3\u00a9");
        QTest::newRow("emoji kept") << QString::fromUtf8("\U0001F3B8 Song") << QString::fromUtf8("\U0001F3B8 Song");
        QTest::newRow("joined emoji kept")
            << QString::fromUtf8("\U0001F468\u200D\U0001F469\u200D\U0001F467 Family")
            << QString::fromUtf8("\U0001F468\u200D\U0001F469\u200D\U0001F467 Family");
        QTest::newRow("lone surrogate dropped")
            << (QStringLiteral("A") + QChar(0xD83C) + QStringLiteral("B")) << QStringLiteral("A B");
        QTest::newRow("CJK") << QString::fromUtf8("\u6771\u4eac") << QString::fromUtf8("\u6771\u4eac");
    }

    void sanitizeMetadata()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(MediaManager::_sanitize_metadata(in), out);
    }

    void sanitizeMetadataCapsLength()
    {
        const QString longText(300, QLatin1Char('a'));
        const QString capped = MediaManager::_sanitize_metadata(longText);
        QCOMPARE(capped.size(), 200);
        QVERIFY(capped.endsWith(QStringLiteral("...")));
        QCOMPARE(MediaManager::_sanitize_metadata(longText, 10), QStringLiteral("aaaaaaa..."));
        QCOMPARE(MediaManager::_sanitize_metadata(QString(200, QLatin1Char('a'))).size(), 200);   // at the cap: untouched

        // The cut never splits an emoji's surrogate pair
        const QString guitar = QString::fromUtf8("\U0001F3B8");
        const QString cut = MediaManager::_sanitize_metadata(QStringLiteral("abcdef") + guitar + QStringLiteral("xyz"), 10);
        QVERIFY2(!cut.isEmpty() && !cut.at(cut.size() - 4).isHighSurrogate(), qPrintable(cut));
        QCOMPARE(cut, QStringLiteral("abcdef..."));
    }

    void zeroByteCoverIsNotServed()
    {
        MediaManager mm;
        const QString albumId = QStringLiteral("Album_Artist");
        const QByteArray hash = QCryptographicHash::hash((albumId + "_0").toUtf8(), QCryptographicHash::Sha256)
                                    .toHex().left(16);
        QDir().mkpath(mm.m_tempDir);
        const QString path = mm.m_tempDir + "/cover_" + QString::fromLatin1(hash) + ".jpg";

        // Truncated by an older build's in-place write
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.close();
        QCOMPARE(mm._cached_art_url_for(albumId), QString());

        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("\xff\xd8jpeg");
        f.close();
        QCOMPARE(mm._cached_art_url_for(albumId), QUrl::fromLocalFile(path).toString());
        QFile::remove(path);
    }

    void browseRootsLeaveTheHomeFolderOut()
    {
        MediaManager mm;
        const QStringList roots = mm.musicBrowseRoots();
        QVERIFY(!roots.contains(QDir::homePath()));
        QVERIFY(!roots.contains(QDir::rootPath()));
        QVERIFY(mm.browse_music_folders(QDir::homePath()).contains("error"));
    }

    void libraryArtOnlyFromTheArtCache()
    {
        // GET /art?song=: a library song's cover through the manager's own
        // lookup, and only a file in its art cache
        auto mm = library({"a.mp3"});
        QVERIFY(mm->libraryArtFile("a.mp3").isEmpty());          // no cover
        QVERIFY(mm->libraryArtFile("not-in-library.mp3").isEmpty());
        QVERIFY(mm->libraryArtFile("../../etc/passwd").isEmpty());
        QVERIFY(mm->libraryArtFile("").isEmpty());

        QDir().mkpath(mm->m_tempDir);
        const QString cover = mm->m_tempDir + "/cover_test.jpg";
        QFile f(cover);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\xff\xd8\xff");
        f.close();
        const QString albumId = mm->_get_album_id("a.mp3");
        mm->m_albumArtCache.insert(albumId, QUrl::fromLocalFile(cover).toString());
        QCOMPARE(mm->libraryArtFile("a.mp3"), QFileInfo(cover).canonicalFilePath());

        // A cache entry naming a file elsewhere is never served
        QTemporaryDir elsewhere;
        const QString outside = elsewhere.filePath("x.jpg");
        QFile o(outside);
        QVERIFY(o.open(QIODevice::WriteOnly));
        o.close();
        mm->m_albumArtCache.insert(albumId, QUrl::fromLocalFile(outside).toString());
        QVERIFY(mm->libraryArtFile("a.mp3").isEmpty());
        QFile::remove(cover);
    }

private:
    // A scanned library with one folder ("Road") holding `files`
    std::unique_ptr<MediaManager> library(const QStringList &files)
    {
        m_lib = std::make_unique<QTemporaryDir>();
        const QDir road(m_lib->filePath("Road"));
        QDir().mkpath(road.path());
        for (const QString &name : files) {
            QFile f(road.filePath(name));
            if (!f.open(QIODevice::WriteOnly))
                qFatal("cannot create %s", qPrintable(f.fileName()));
            f.write(QByteArray(1, '\0'));
        }
        auto mm = std::make_unique<MediaManager>();
        mm->set_library_root(m_lib->path());
        const QStringList names = mm->get_playlist_names();
        m_playlist = names.contains("Road") ? QStringLiteral("Road") : QString();
        if (m_playlist.isEmpty())
            qWarning() << "no Road playlist after the scan:" << names;
        mm->select_playlist(m_playlist);
        return mm;
    }

    void removeSongs(const QStringList &files)
    {
        for (const QString &name : files)
            QFile::remove(m_lib->filePath("Road/" + name));
    }

    QTemporaryDir m_home;
    std::unique_ptr<QTemporaryDir> m_lib;
    QString m_playlist;
};

QTEST_MAIN(TstMediaManager)
#include "tst_mediamanager.moc"
