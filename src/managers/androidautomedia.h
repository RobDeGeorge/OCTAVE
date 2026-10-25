#ifndef ANDROIDAUTOMEDIA_H
#define ANDROIDAUTOMEDIA_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>

/**
 * @brief Android Auto's music, as NowPlaying sees it: what plays, the phone's
 *        requests to play, and the controls OCTAVE sends back.
 *
 * AndroidAutoManager feeds it from the native session (playback status and
 * metadata, focus requests, the mic, guidance audio) and carries out what it
 * emits (focus answers, media keys). NowPlaying decides: it answers every
 * mediaFocusRequested() with grantFocus() or denyFocus(). A plain QObject
 * built on every platform, so tst_nowplaying drives it without a phone.
 *
 * Not a service object of its own: nowPlaying is the published surface.
 */
class AndroidAutoMedia : public QObject
{
    Q_OBJECT
public:
    // A focus request this long after a user action counts as the user's
    static constexpr int kIntentWindowMs = 6000;
    // Volume factor for local music under Android Auto guidance / system audio
    static constexpr float kDuckFactor = 0.3f;

    explicit AndroidAutoMedia(QObject *parent = nullptr);

    // ── state ──
    bool connected() const { return m_connected; }
    bool hasFocus() const { return m_focus; }
    bool playing() const { return m_playing; }
    bool hasTrack() const { return !m_title.isEmpty(); }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString appName() const { return m_app; }
    QString artUrl() const { return m_artUrl; }
    int durationMs() const { return m_durationMs; }
    // Extrapolated from the last report while playing
    int positionMs() const;
    bool microphoneOpen() const { return m_micOpen; }
    bool interruptsLocal() const { return m_guidance || m_system; }

    // A user action aimed at Android Auto: a touch on its screen, an OCTAVE
    // control sent to it, a voice request
    void noteUserIntent();
    bool recentUserIntent() const;
    // Milliseconds since the session started running (-1 when not connected)
    qint64 msSinceConnect() const;

    // ── fed by AndroidAutoManager ──
    void setConnected(bool connected);
    void setPlaybackStatus(int state, int positionS, const QString &app);   // Aap::PlaybackStatus::State
    void setMetadata(const QString &title, const QString &artist, const QString &album,
                     const QByteArray &art, int durationS);
    void focusRequested(int requestType);                                     // Aap::Focus::Request*
    void setMicrophoneOpen(bool open);
    void setGuidancePlaying(bool on);
    void setSystemSoundPlaying(bool on);

    // ── decisions (NowPlaying) ──
    void grantFocus();      // answer the pending GAIN request with GAIN
    void denyFocus();       // answer it with LOSS: the phone doesn't play
    void revokeFocus();     // OCTAVE takes focus back: the phone pauses

    // ── controls ──
    void playPause();
    void next();
    void previous();

signals:
    void connectedChanged();
    void playingChanged();
    void trackChanged();        // title / artist / album / duration
    void artChanged();
    void positionReported();    // a status report: the position is authoritative
    // The phone wants to play media (GAIN); answer with grantFocus / denyFocus
    void mediaFocusRequested();
    // The phone gave media focus up (RELEASE)
    void mediaFocusReleased();
    void microphoneOpenChanged(bool open);
    void interruptsLocalChanged(bool on);
    // For AndroidAutoManager: carry these out on the session
    void focusAnswer(int state);        // Aap::Focus state, answering a request
    void focusChange(int state);        // unsolicited
    void keyPress(int keycode);         // Aap::Key

private:
    void setPlaying(bool v);

    bool m_connected = false;
    bool m_focus = false;
    bool m_pendingGain = false;
    bool m_playing = false;
    QString m_title, m_artist, m_album, m_app, m_artUrl;
    int m_artSeq = 0;
    int m_durationMs = 0;
    int m_reportedPositionMs = 0;
    QElapsedTimer m_sinceReport;
    QElapsedTimer m_sinceIntent;
    QElapsedTimer m_sinceConnect;
    bool m_micOpen = false;
    bool m_guidance = false;
    bool m_system = false;
};

#endif // ANDROIDAUTOMEDIA_H
