#ifndef NOWPLAYING_H
#define NOWPLAYING_H

#include <QObject>
#include <QString>
#include <QTimer>

class MediaManager;
class PhoneMirrorManager;
class SettingsManager;

/**
 * @brief What is playing right now and the controls for it, whichever source
 *        has OCTAVE's media controls: the phone (while mirroring, once it has
 *        taken them) or OCTAVE's own player.
 *
 * Every frontend binds to this instead of asking phoneMirrorManager.mediaActive
 * and then reading one of two APIs; `source` says which one is live. The phone
 * reports its position only now and then, so this owns the one clock that
 * extrapolates it (kPhoneTickMs) while the phone plays. Values are raw: no
 * placeholder text ("Nothing playing" is a frontend's choice, see hasTrack).
 *
 * It also holds the handoff rules: when the phone takes the controls local
 * playback pauses, and a local track starting hands them back (pausing the
 * phone). PhoneMirrorManager decides when the phone takes them.
 *
 * And the Album Art Capture rule: while that theme is selected, the colours
 * follow the live source's art (extracted on an art change, a source switch,
 * or the theme being chosen), whichever source it is.
 *
 * Registered in main.cpp as QML context property "nowPlaying".
 */
class NowPlaying : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)          // "local" | "phone"
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackInfoChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackInfoChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackInfoChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackInfoChanged)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY artUrlChanged)          // "" when none
    // +1 on every art change: a service client's cache key for GET /art
    Q_PROPERTY(int artRevision READ artRevision NOTIFY artUrlChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY durationChanged)    // 0 = unknown
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY capabilitiesChanged)
    // The phone's shuffle belongs to its app: OCTAVE's toggle is local only
    Q_PROPERTY(bool canShuffle READ canShuffle NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool shuffled READ shuffled NOTIFY shuffledChanged)

public:
    static constexpr int kPhoneTickMs = 500;

    explicit NowPlaying(MediaManager *media, PhoneMirrorManager *phone,
                        SettingsManager *settings = nullptr, QObject *parent = nullptr);

    QString source() const { return m_phone ? QStringLiteral("phone") : QStringLiteral("local"); }
    bool isPhone() const { return m_phone; }
    bool playing() const { return m_playing; }
    bool hasTrack() const { return m_hasTrack; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString artUrl() const { return m_artUrl; }
    int artRevision() const { return m_artRevision; }
    int durationMs() const { return m_durationMs; }
    int positionMs() const { return m_positionMs; }
    bool canSeek() const { return m_canSeek; }
    bool canShuffle() const { return !m_phone; }
    bool shuffled() const { return m_shuffled; }

signals:
    void sourceChanged();
    void playingChanged();
    void trackInfoChanged();
    void artUrlChanged();
    void durationChanged();
    void positionChanged();
    void capabilitiesChanged();
    void shuffledChanged();
    // Another track (or another source): for track-change animations
    void trackChanged();

public slots:
    void togglePlay();
    void next();
    void previous();
    void seek(int positionMs);
    void toggleShuffle();   // no-op while the phone is the source

    // Theme colours from the live source's art now, whatever the theme
    // (Media Room's "save as theme" popup; the Album Art Capture rule)
    void extractArtColors();

private:
    void onSourceChanged();
    void readLocalTrack();
    void readPhoneState();
    void readPhoneTrack();
    void tickPhone();
    void updateTick();
    bool albumArtCapture() const;

    void setPlaying(bool v);
    void setTrack(bool has, const QString &title, const QString &artist, const QString &album);
    void setArtUrl(const QString &url);
    void setDuration(int ms);
    void setPosition(int ms);
    void setCanSeek(bool v);
    void setShuffled(bool v);

    MediaManager *m_media = nullptr;
    PhoneMirrorManager *m_phoneMirror = nullptr;
    SettingsManager *m_settings = nullptr;
    bool m_phone = false;
    QString m_localFile;

    bool m_playing = false;
    bool m_hasTrack = false;
    QString m_title, m_artist, m_album, m_artUrl;
    int m_artRevision = 0;
    int m_durationMs = 0;
    int m_positionMs = 0;
    bool m_canSeek = false;
    bool m_shuffled = false;

    QTimer m_phoneTick;
};

#endif // NOWPLAYING_H
