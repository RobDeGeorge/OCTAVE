// NowPlaying: what is playing and the controls for it, whichever source has
// OCTAVE's media controls. Replaces the "phone or local?" checks, the
// per-page play-state syncs and the three 500 ms phone-position timers that
// lived in QML. Drives the real PhoneMirrorManager through its state
// reports and the real MediaManager through its signals.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/mediamanager.h"
#include "managers/nowplaying.h"
#include "managers/phonemirrormanager.h"

namespace {
QVariantMap phoneState(bool playing, const QString &title = "Song", qint64 positionMs = 1000)
{
    return {{"has_session", true}, {"playing", playing}, {"can_seek", true}, {"position_ms", positionMs},
            {"duration_ms", 200000}, {"package", "com.example.music"}, {"title", title}, {"artist", "A"},
            {"album", "B"}};
}
} // namespace

class TstNowPlaying : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp->path() + "/appdata").toUtf8());
    }

    void startsLocalWithNothingLoaded()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        QCOMPARE(np.source(), QStringLiteral("local"));
        QVERIFY(!np.hasTrack());
        QVERIFY(!np.playing());
        QVERIFY(np.canShuffle());
        QVERIFY(!np.canSeek());
    }

    void phoneTakesTheControls()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        QSignalSpy source(&np, &NowPlaying::sourceChanged);
        QSignalSpy track(&np, &NowPlaying::trackChanged);

        phone.onMediaState(phoneState(true));   // first report: the phone takes them
        QCOMPARE(np.source(), QStringLiteral("phone"));
        QCOMPARE(source.count(), 1);
        QVERIFY(track.count() >= 1);
        QVERIFY(np.hasTrack());
        QCOMPARE(np.title(), QStringLiteral("Song"));
        QCOMPARE(np.artist(), QStringLiteral("A"));
        QCOMPARE(np.album(), QStringLiteral("B"));
        QVERIFY(np.playing());
        QCOMPARE(np.durationMs(), 200000);
        QVERIFY(np.canSeek());
        QVERIFY(!np.canShuffle());   // the phone's shuffle belongs to its app

        phone.resetMedia();          // mirroring ended
        QCOMPARE(np.source(), QStringLiteral("local"));
        QCOMPARE(source.count(), 2);
        QVERIFY(np.canShuffle());
    }

    void onePhoneClockWhilePlaying()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        phone.onMediaState(phoneState(true, "Song", 1000));
        const int start = np.positionMs();
        QVERIFY(start >= 1000);
        // Extrapolated between reports, at kPhoneTickMs
        QTRY_VERIFY_WITH_TIMEOUT(np.positionMs() >= start + NowPlaying::kPhoneTickMs, 3000);

        phone.onMediaState(phoneState(false, "Song", 5000));   // paused at 5 s
        QCOMPARE(np.positionMs(), 5000);
        QSignalSpy pos(&np, &NowPlaying::positionChanged);
        QTest::qWait(3 * NowPlaying::kPhoneTickMs);
        QCOMPARE(pos.count(), 0);                               // no clock while paused
    }

    void trackChangedOncePerTrack()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        phone.onMediaState(phoneState(true, "Song"));
        QSignalSpy track(&np, &NowPlaying::trackChanged);
        QSignalSpy info(&np, &NowPlaying::trackInfoChanged);
        phone.onMediaState(phoneState(false, "Song"));    // pause
        phone.onMediaState(phoneState(true, "Song", 9000));   // play, seek
        QCOMPARE(track.count(), 0);
        QCOMPARE(info.count(), 0);
        phone.onMediaState(phoneState(true, "Next"));
        QCOMPARE(track.count(), 1);
        QCOMPARE(np.title(), QStringLiteral("Next"));
    }

    void localSignalsWhileLocal()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        QSignalSpy track(&np, &NowPlaying::trackChanged);
        emit media.currentMediaChanged(QStringLiteral("Road Song.mp3"));
        QCOMPARE(track.count(), 1);
        QVERIFY(np.hasTrack());
        QCOMPARE(np.title(), QStringLiteral("Road Song"));   // display name
        QVERIFY(np.canSeek());
        emit media.durationChanged(180000);
        emit media.positionChanged(42000);
        QCOMPARE(np.durationMs(), 180000);
        QCOMPARE(np.positionMs(), 42000);
        emit media.shuffleStateChanged(true);
        QVERIFY(np.shuffled());

        // While the phone has the controls, local reports do not leak in
        phone.onMediaState(phoneState(false, "Song", 1000));
        emit media.positionChanged(99000);
        QCOMPARE(np.positionMs(), 1000);
        // ...and shuffle stays local-only
        np.toggleShuffle();
        QVERIFY(np.shuffled());
    }

    void localTrackStartingHandsTheControlsBack()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        phone.onMediaState(phoneState(true));
        QCOMPARE(np.source(), QStringLiteral("phone"));
        emit media.playStateChanged(true);   // the user started a local track
        QCOMPARE(np.source(), QStringLiteral("local"));
        QVERIFY(!phone.mediaActive());
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TstNowPlaying)
#include "tst_nowplaying.moc"
