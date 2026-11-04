// NowPlaying: what is playing and the controls for it, whichever source has
// OCTAVE's media controls. Replaces the "phone or local?" checks, the
// per-page play-state syncs and the three 500 ms phone-position timers that
// lived in QML. Drives the real PhoneMirrorManager through its state
// reports and the real MediaManager through its signals.

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <memory>

#include "managers/androidautomedia.h"
#include "managers/mediamanager.h"
#include "managers/nowplaying.h"
#include "managers/phonemirrormanager.h"
#include "managers/settingsmanager.h"
#include "managers/spotifymanager.h"

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

    // ── the arbiter: Android Auto asks, a user action decides ──────────
    // (TODO/media-source-arbiter.md; focus states 1 GAIN / 3 LOSS, keys 85
    // play-pause / 87 next / 88 previous are the protocol's)

    void androidAutoAutoPlayRefusedWhileLocalOwns()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy answer(&aa, &AndroidAutoMedia::focusAnswer);
        QSignalSpy change(&aa, &AndroidAutoMedia::focusChange);
        aa.setConnected(true);
        aa.focusRequested(1);               // the phone starting music by itself
        // Refused the way a car radio does it: granted, then taken back at
        // once (an answered LOSS makes Android Auto ask again in a loop)
        QCOMPARE(answer.count(), 1);
        QCOMPARE(answer.at(0).at(0).toInt(), 1);
        QCOMPARE(change.count(), 1);
        QCOMPARE(change.at(0).at(0).toInt(), 3);
        QCOMPARE(np.source(), QStringLiteral("local"));
        QVERIFY(!aa.hasFocus());
    }

    void androidAutoGrantedAfterATouch()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy answer(&aa, &AndroidAutoMedia::focusAnswer);
        QSignalSpy source(&np, &NowPlaying::sourceChanged);
        aa.setConnected(true);
        aa.noteUserIntent();                // a tap on Spotify in Android Auto
        aa.focusRequested(1);
        QCOMPARE(answer.at(0).at(0).toInt(), 1);
        QCOMPARE(np.source(), QStringLiteral("androidauto"));
        QCOMPARE(source.count(), 1);
        QVERIFY(!np.canShuffle());
        QVERIFY(!np.canSeek());
        // it owns now: later requests need no touch
        aa.focusRequested(4);               // paused, gave focus up
        aa.focusRequested(1);
        QCOMPARE(answer.count(), 2);
        QCOMPARE(answer.at(1).at(0).toInt(), 1);
    }

    void voiceRequestCountsAsTheUsers()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy answer(&aa, &AndroidAutoMedia::focusAnswer);
        aa.setConnected(true);
        aa.setMicrophoneOpen(true);         // "Hey Google, play some music"
        aa.setMicrophoneOpen(false);
        aa.focusRequested(1);
        QCOMPARE(answer.at(0).at(0).toInt(), 1);
        QCOMPARE(np.source(), QStringLiteral("androidauto"));
    }

    void localPlayTakesTheMusicBackFromAndroidAuto()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy change(&aa, &AndroidAutoMedia::focusChange);
        aa.setConnected(true);
        aa.noteUserIntent();
        aa.focusRequested(1);
        QCOMPARE(np.source(), QStringLiteral("androidauto"));
        emit media.playStateChanged(true);  // the user started a local track
        QCOMPARE(np.source(), QStringLiteral("local"));
        QCOMPARE(change.count(), 1);
        QCOMPARE(change.at(0).at(0).toInt(), 3);   // unsolicited LOSS: the phone pauses
        QVERIFY(!aa.hasFocus());
    }

    void phoneMirrorTakingTheControlsStopsAndroidAuto()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy change(&aa, &AndroidAutoMedia::focusChange);
        aa.setConnected(true);
        aa.noteUserIntent();
        aa.focusRequested(1);
        phone.onMediaState(phoneState(true));
        QCOMPARE(np.source(), QStringLiteral("phone"));
        QCOMPARE(change.count(), 1);
    }

    void controlsGoToAndroidAuto()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy keys(&aa, &AndroidAutoMedia::keyPress);
        aa.setConnected(true);
        aa.noteUserIntent();
        aa.focusRequested(1);
        np.togglePlay();
        np.next();
        np.previous();
        QCOMPARE(keys.count(), 3);
        QCOMPARE(keys.at(0).at(0).toInt(), 85);
        QCOMPARE(keys.at(1).at(0).toInt(), 87);
        QCOMPARE(keys.at(2).at(0).toInt(), 88);
        // after pausing from OCTAVE, the phone's next request is the user's
        aa.focusRequested(4);
        QSignalSpy answer(&aa, &AndroidAutoMedia::focusAnswer);
        aa.focusRequested(1);
        QCOMPARE(answer.at(0).at(0).toInt(), 1);
    }

    void androidAutoTrackShows()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        aa.setConnected(true);
        aa.noteUserIntent();
        aa.focusRequested(1);
        QSignalSpy track(&np, &NowPlaying::trackChanged);
        aa.setMetadata(QStringLiteral("Song"), QStringLiteral("Artist"), QStringLiteral("Album"), redJpeg(), 200);
        aa.setPlaybackStatus(2, 10, QStringLiteral("Spotify"));
        QCOMPARE(track.count(), 1);
        QVERIFY(np.hasTrack());
        QCOMPARE(np.title(), QStringLiteral("Song"));
        QCOMPARE(np.artist(), QStringLiteral("Artist"));
        QCOMPARE(np.album(), QStringLiteral("Album"));
        QVERIFY(!np.artUrl().isEmpty());
        QCOMPARE(np.durationMs(), 200000);
        QVERIFY(np.playing());
        QVERIFY(np.positionMs() >= 10000);
        const int start = np.positionMs();
        QTRY_VERIFY_WITH_TIMEOUT(np.positionMs() >= start + NowPlaying::kPhoneTickMs, 3000);

        // unplugged: the track stays on show, paused, Android Auto still the source
        aa.setConnected(false);
        QCOMPARE(np.source(), QStringLiteral("androidauto"));
        QVERIFY(!np.playing());
        QVERIFY(np.hasTrack());
        // play then goes to OCTAVE's own player, not into the void
        QSignalSpy keys(&aa, &AndroidAutoMedia::keyPress);
        np.togglePlay();
        QCOMPARE(keys.count(), 0);
    }

    void androidAutoResumesAfterARestart()
    {
        SettingsManager settings;
        AndroidAutoMedia aa;
        {
            MediaManager media;
            PhoneMirrorManager phone;
            NowPlaying np(&media, &phone, &settings);
            np.setAndroidAuto(&aa);
            aa.setConnected(true);
            aa.noteUserIntent();
            aa.focusRequested(1);
            aa.setPlaybackStatus(2, 0, QStringLiteral("Spotify"));
            QCOMPARE(settings.get_setting(QStringLiteral("mediaLastSource")).toString(), QStringLiteral("androidauto"));
            QVERIFY(settings.get_setting(QStringLiteral("mediaLastPlaying")).toBool());
            np.stopSaving();
            aa.setConnected(false);         // the car turned off
        }
        QVERIFY(settings.get_setting(QStringLiteral("mediaLastPlaying")).toBool());

        AndroidAutoMedia aa2;               // next drive: no touch yet
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone, &settings);
        np.setAndroidAuto(&aa2);
        QSignalSpy answer(&aa2, &AndroidAutoMedia::focusAnswer);
        QSignalSpy keys(&aa2, &AndroidAutoMedia::keyPress);
        QSignalSpy change(&aa2, &AndroidAutoMedia::focusChange);
        aa2.setConnected(true);
        aa2.focusRequested(1);              // Android Auto's own request on connect
        QCOMPARE(answer.at(0).at(0).toInt(), 1);
        QCOMPARE(change.count(), 0);
        QCOMPARE(np.source(), QStringLiteral("androidauto"));
        // not playing yet: the saved "it was playing" must survive that
        QVERIFY(settings.get_setting(QStringLiteral("mediaLastPlaying")).toBool());
        // the phone stays quiet: OCTAVE presses play
        QTRY_COMPARE_WITH_TIMEOUT(keys.count(), 1, NowPlaying::kResumeKeyDelayMs + 2000);
        QCOMPARE(keys.at(0).at(0).toInt(), 85);
    }

    void resumePressesPlayIfThePhoneStaysQuiet()
    {
        SettingsManager settings;
        settings.save_setting(QStringLiteral("mediaLastSource"), QStringLiteral("androidauto"));
        settings.save_setting(QStringLiteral("mediaLastPlaying"), true);
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone, &settings);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy keys(&aa, &AndroidAutoMedia::keyPress);
        aa.setConnected(true);
        QTRY_COMPARE_WITH_TIMEOUT(keys.count(), 1, NowPlaying::kResumeKeyDelayMs + 2000);
        QCOMPARE(keys.at(0).at(0).toInt(), 85);
    }

    void localPlayCancelsTheResume()
    {
        SettingsManager settings;
        settings.save_setting(QStringLiteral("mediaLastSource"), QStringLiteral("androidauto"));
        settings.save_setting(QStringLiteral("mediaLastPlaying"), true);
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone, &settings);
        AndroidAutoMedia aa;
        np.setAndroidAuto(&aa);
        QSignalSpy change(&aa, &AndroidAutoMedia::focusChange);
        emit media.playStateChanged(true);  // the user picked a local song first
        aa.setConnected(true);
        aa.focusRequested(1);
        QCOMPARE(change.count(), 1);        // refused
        QCOMPARE(np.source(), QStringLiteral("local"));
    }

    void androidAutoAloneGrantsEverything()
    {
        AndroidAutoMedia aa;                // no NowPlaying deciding
        QSignalSpy answer(&aa, &AndroidAutoMedia::focusAnswer);
        aa.setConnected(true);
        aa.focusRequested(1);
        QCOMPARE(answer.at(0).at(0).toInt(), 1);
    }

    // ── Album Art Capture: the theme follows the live source's art ─────
    // (was MediaManager for local tracks plus two copies in Main.qml for
    // the phone, which extracted twice on a theme switch)

    void themeFollowsThePhonesArt()
    {
        SettingsManager settings;
        settings.save_theme_setting(QStringLiteral("Album Art Capture"));
        MediaManager media;   // no settings manager: it reports by signal
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone, &settings);
        QSignalSpy colors(&media, &MediaManager::albumColorsExtracted);

        phone.onMediaState(phoneState(true));
        phone.onMediaArt(redJpeg());
        QVERIFY(!np.artUrl().isEmpty());
        QTRY_COMPARE(colors.count(), 1);   // once, from the art change
        // GET /art's cache key moves with every new cover
        const int revision = np.artRevision();
        QVERIFY(revision > 0);
        phone.onMediaArt(redJpeg());
        QCOMPARE(np.artRevision(), revision + 1);
        QTRY_COMPARE(colors.count(), 2);
        QTest::qWait(100);
        QCOMPARE(colors.count(), 2);   // once per art change
    }

    void noExtractionForOtherThemes()
    {
        SettingsManager settings;
        settings.save_theme_setting(QStringLiteral("Midnight"));
        MediaManager media;
        PhoneMirrorManager phone;
        NowPlaying np(&media, &phone, &settings);
        QSignalSpy colors(&media, &MediaManager::albumColorsExtracted);
        phone.onMediaState(phoneState(true));
        phone.onMediaArt(redJpeg());
        QTest::qWait(200);
        QCOMPARE(colors.count(), 0);

        // Switching the theme on picks up the art already showing
        settings.save_theme_setting(QStringLiteral("Album Art Capture"));
        QTRY_COMPARE(colors.count(), 1);
    }

    // Spotify: a remote over the Web API. Signed in without a token, so no
    // request leaves the test; player reports go in through applyPlayer.
    void spotifyStartingElsewhereTakesTheMusicWhenNothingPlays()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        NowPlaying np(&media, &phone);
        np.setSpotify(&spotify);
        spotify.m_state = QStringLiteral("signedIn");

        spotify.applyPlayer(spotifyState(true, QStringLiteral("Drive")));
        QCOMPARE(np.source(), QStringLiteral("spotify"));
        QVERIFY(np.playing());
        QCOMPARE(np.title(), QStringLiteral("Drive"));
        QCOMPARE(np.artist(), QStringLiteral("Band"));
        QCOMPARE(np.durationMs(), 240000);
        QVERIFY(np.canSeek());
        QVERIFY(np.canShuffle());
        QVERIFY(np.shuffled());

        // A new track from the same source
        QSignalSpy track(&np, &NowPlaying::trackChanged);
        spotify.applyPlayer(spotifyState(true, QStringLiteral("Next One")));
        QCOMPARE(np.title(), QStringLiteral("Next One"));
        QCOMPARE(track.count(), 1);

        // The user starts a local track: Spotify is paused, local owns
        emit media.playStateChanged(true);
        QCOMPARE(np.source(), QStringLiteral("local"));
        QVERIFY(!spotify.playing());
        QVERIFY(!np.shuffled());
    }

    void spotifyNeverTakesOverMusicThatPlays()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        NowPlaying np(&media, &phone);
        np.setSpotify(&spotify);
        spotify.m_state = QStringLiteral("signedIn");

        emit media.currentMediaChanged(QStringLiteral("Road Song.mp3"));
        emit media.playStateChanged(true);
        spotify.applyPlayer(spotifyState(true, QStringLiteral("Drive")));
        QCOMPARE(np.source(), QStringLiteral("local"));
        QVERIFY(np.playing());

        // Played from OCTAVE's Spotify page: the user's choice wins
        emit spotify.playbackRequested();
        QCOMPARE(np.source(), QStringLiteral("spotify"));
        QCOMPARE(np.title(), QStringLiteral("Drive"));

        // Signing out while it owns hands back to local, paused
        spotify.signOut();
        QCOMPARE(np.source(), QStringLiteral("local"));
    }

    // Mirroring with Spotify on that phone: playing a song from OCTAVE starts
    // it there, the phone reports it and takes the controls. Pausing "the old
    // source" then would pause the very music the user just asked for.
    void spotifyOnTheMirroredPhoneIsOnePlayer()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        NowPlaying np(&media, &phone);
        np.setSpotify(&spotify);
        spotify.m_state = QStringLiteral("signedIn");
        spotify.applyPlayer(spotifyState(true, QStringLiteral("Drive")));
        QCOMPARE(np.source(), QStringLiteral("spotify"));

        QVariantMap state = phoneState(true, "Drive");
        state[QStringLiteral("package")] = QStringLiteral("com.spotify.music");
        phone.onMediaState(state);
        QCOMPARE(np.source(), QStringLiteral("phone"));
        QVERIFY(spotify.playing());   // not paused by the hand-off

        // And the phone keeps the controls: its audio is on OCTAVE's volume
        emit spotify.playbackRequested();
        QCOMPARE(np.source(), QStringLiteral("phone"));
    }

    void anotherAppOnThePhoneStillPausesSpotify()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        NowPlaying np(&media, &phone);
        np.setSpotify(&spotify);
        spotify.m_state = QStringLiteral("signedIn");
        spotify.applyPlayer(spotifyState(true, QStringLiteral("Drive")));

        QVariantMap state = phoneState(true, "A Podcast");
        state[QStringLiteral("package")] = QStringLiteral("org.example.podcasts");
        phone.onMediaState(state);
        QCOMPARE(np.source(), QStringLiteral("phone"));
        QVERIFY(!spotify.playing());
    }

    // No network yet (the car just started): the token request fails and
    // everything waiting for it must finish, or polling and the library stay
    // wedged until a restart. m_refreshing holds the real request back.
    void aFailedTokenRequestLeavesNothingHanging()
    {
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        spotify.m_state = QStringLiteral("signedIn");
        spotify.m_refreshToken = QStringLiteral("token");
        spotify.m_refreshing = true;

        spotify.poll();
        spotify.refreshLibrary();
        QVERIFY(spotify.m_pollInFlight);
        QVERIFY(spotify.libraryLoading());

        spotify.failWaiting();
        QVERIFY(!spotify.m_pollInFlight);
        QVERIFY(spotify.m_poll.isActive());   // it tries again
        QVERIFY(!spotify.libraryLoading());
        QVERIFY(!spotify.m_libraryLoaded);    // so the next good poll loads it
    }

    void aPlayThatCannotBeSentIsExplainedAndTakenBack()
    {
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        spotify.m_state = QStringLiteral("signedIn");
        spotify.m_refreshToken = QStringLiteral("token");
        spotify.m_refreshing = true;
        QSignalSpy requested(&spotify, &SpotifyManager::playbackRequested);
        QSignalSpy failed(&spotify, &SpotifyManager::commandFailed);

        spotify.play();
        QVERIFY(spotify.playing());
        spotify.failWaiting();
        QVERIFY(!spotify.playing());
        QCOMPARE(requested.count(), 0);   // the music that was on stays on
        QCOMPARE(failed.count(), 1);
    }

    // The whole way: a real request during a real aboutToQuit, when the main
    // loop has already ended
    void closingPausesSpotifyDuringAboutToQuit()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray request;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *sock = server.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, sock, [&request, sock] {
                request += sock->readAll();
                sock->write("HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
                sock->disconnectFromHost();
            });
        });

        MediaManager media;
        PhoneMirrorManager phone;
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        NowPlaying np(&media, &phone);
        np.setSpotify(&spotify);
        signInForTest(spotify);
        spotify.m_apiBase = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        spotify.applyPlayer(spotifyState(true, QStringLiteral("Drive")));
        QCOMPARE(np.source(), QStringLiteral("spotify"));

        QElapsedTimer took;
        const auto hook = connect(qApp, &QCoreApplication::aboutToQuit, &np, [&] {
            took.start();
            np.pauseSpotifyBeforeExit();
            QVERIFY(took.elapsed() < NowPlaying::kExitPauseMs);   // answered, not timed out
        });
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        QCoreApplication::exec();
        disconnect(hook);
        QVERIFY2(request.startsWith("PUT /me/player/pause "), request.constData());
    }

    void closingLeavesSpotifyAloneWhenItIsNotOctavesMusic()
    {
        MediaManager media;
        PhoneMirrorManager phone;
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        NowPlaying np(&media, &phone);
        np.setSpotify(&spotify);
        spotify.m_state = QStringLiteral("signedIn");

        // Nothing plays
        QVERIFY(!np.spotifyPlaysForOctave());

        // The phone owns the music with another app; Spotify plays elsewhere
        phone.onMediaState(phoneState(true, "A Podcast"));
        QCOMPARE(np.source(), QStringLiteral("phone"));
        spotify.applyPlayer(spotifyState(true, QStringLiteral("At Home")));
        QCOMPARE(np.source(), QStringLiteral("phone"));
        QVERIFY(!np.spotifyPlaysForOctave());

        // That phone's app is Spotify: it is OCTAVE's after all
        QVariantMap state = phoneState(true, "Drive");
        state[QStringLiteral("package")] = QStringLiteral("com.spotify.music");
        phone.onMediaState(state);
        QVERIFY(np.spotifyPlaysForOctave());
    }

    // No network: closing waits a moment, not forever
    void closingWithNoAnswerGivesUpInTime()
    {
        SpotifyManager spotify;
        spotify.setStoreFile(m_tmp->path() + "/spotify.json");
        spotify.m_state = QStringLiteral("signedIn");
        spotify.m_refreshToken = QStringLiteral("token");
        spotify.m_refreshing = true;   // a token that never comes
        QElapsedTimer took;
        took.start();
        spotify.pauseBeforeExit(100);
        QVERIFY(took.elapsed() >= 90 && took.elapsed() < 2000);
    }

    void savedSessionIsPrivate()
    {
        SpotifyManager spotify;
        const QString file = m_tmp->path() + "/spotify-session.json";
        spotify.setStoreFile(file);
        spotify.m_refreshToken = QStringLiteral("token");
        spotify.save();
        QVERIFY(QFile::exists(file));
        QCOMPARE(QFile::permissions(file) & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());
    }

private:
    static void signInForTest(SpotifyManager &spotify)
    {
        spotify.m_state = QStringLiteral("signedIn");
        spotify.m_refreshToken = QStringLiteral("token");
        spotify.m_accessToken = QStringLiteral("access");
        spotify.m_tokenLifeMs = 3600 * 1000;
        spotify.m_tokenAge.start();
    }

    static SpotifyApi::PlayerState spotifyState(bool playing, const QString &title)
    {
        SpotifyApi::PlayerState s;
        s.valid = true;
        s.playing = playing;
        s.title = title;
        s.artist = QStringLiteral("Band");
        s.album = QStringLiteral("LP");
        s.trackUri = QStringLiteral("spotify:track:") + title;
        s.durationMs = 240000;
        s.positionMs = 1000;
        s.shuffle = true;
        return s;
    }

    static QByteArray redJpeg()
    {
        QImage img(32, 32, QImage::Format_RGB32);
        img.fill(QColor(200, 30, 30));
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "JPEG");
        return bytes;
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TstNowPlaying)
#include "tst_nowplaying.moc"
