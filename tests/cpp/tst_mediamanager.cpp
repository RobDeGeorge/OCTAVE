// MediaManager playback robustness: a missing track must not silently stop
// playback, shuffle must survive a playlist re-select (what a library rescan
// does), and a truncated cover in the cache is never served. Ported from
// tests/test_media_playback_robustness.py, against a real MediaManager and a
// scanned temp library (the "songs" are placeholder bytes; nothing here
// waits for the player to decode them).

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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

private:
    // A scanned library with one folder ("Road") holding `files`
    std::unique_ptr<MediaManager> library(const QStringList &files)
    {
        m_lib = std::make_unique<QTemporaryDir>();
        const QDir road(m_lib->filePath("Road"));
        QDir().mkpath(road.path());
        for (const QString &name : files) {
            QFile f(road.filePath(name));
            f.open(QIODevice::WriteOnly);
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
