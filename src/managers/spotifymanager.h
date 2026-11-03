#ifndef SPOTIFYMANAGER_H
#define SPOTIFYMANAGER_H

#include "spotifyapi.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

#include <functional>

class QNetworkReply;
class QTcpServer;

/**
 * @brief Spotify through its Web API: sign in, the user's library in OCTAVE's
 *        own pages, and remote control of whatever Spotify device plays.
 *
 * OCTAVE never plays Spotify audio itself: the Spotify app (on the phone, a
 * computer, a speaker) is the player and this is its remote (Spotify Connect).
 * Playback control needs Spotify Premium.
 *
 * Sign-in is OAuth 2 with PKCE (no client secret). The desktop app waits for
 * Spotify's redirect on http://127.0.0.1:43821/callback; the browser build
 * sends the page to Spotify and reads the code when it comes back
 * (finishBrowserSignIn() at startup). The refresh token is kept in
 * <config>/spotify.json (localStorage in the browser).
 *
 * The client ID is the Spotify developer app's: OCTAVE_SPOTIFY_CLIENT_ID at
 * build time, or one set here (setClientId). A Development Mode app only
 * signs in the users added to it in Spotify's dashboard.
 *
 * NowPlaying makes Spotify a music source (`source` "spotify"): it reads the
 * player state from here and routes its controls here.
 *
 * Registered as context property / service object "spotifyManager".
 */
class SpotifyManager : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "media")
    Q_CLASSINFO("octave.scope.full", "setClientId signIn signOut")
    Q_CLASSINFO("octave.experimental", "*")
    friend class TstNowPlaying;

    // "signedOut" | "signingIn" | "signedIn"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY stateChanged)
    Q_PROPERTY(QString clientId READ clientId NOTIFY clientIdChanged)
    // The client ID is the build's (no need to enter one)
    Q_PROPERTY(bool builtInClientId READ builtInClientId CONSTANT)
    // The redirect URI to register in the Spotify developer app
    Q_PROPERTY(QString redirectUri READ redirectUri CONSTANT)
    Q_PROPERTY(QString userName READ userName NOTIFY userChanged)
    // Last thing worth telling the user ("" when all is well)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    // Library: Liked Songs first, then playlists; saved albums apart
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryLoading READ libraryLoading NOTIFY libraryLoadingChanged)

    // The collection opened with openCollection()
    Q_PROPERTY(QString collectionUri READ collectionUri NOTIFY collectionChanged)
    Q_PROPERTY(QVariantMap collection READ collection NOTIFY collectionChanged)
    Q_PROPERTY(QVariantList collectionTracks READ collectionTracks NOTIFY collectionTracksChanged)
    Q_PROPERTY(bool collectionLoading READ collectionLoading NOTIFY collectionTracksChanged)

    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

    // The player (GET /me/player)
    Q_PROPERTY(bool hasPlayback READ hasPlayback NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString trackUri READ trackUri NOTIFY trackChanged)
    Q_PROPERTY(QString contextUri READ contextUri NOTIFY trackChanged)
    // A local file: URL once the cover is downloaded ("" before)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY artChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY trackChanged)
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionReported)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY modesChanged)
    Q_PROPERTY(QString repeat READ repeat NOTIFY modesChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    // The playing device's volume, 0-100 (-1: not reported); false when that
    // device won't let Spotify change it (some phones)
    Q_PROPERTY(int volumePercent READ volumePercent NOTIFY deviceChanged)
    Q_PROPERTY(bool supportsVolume READ supportsVolume NOTIFY deviceChanged)

public:
    static constexpr quint16 kLoopbackPort = 43821;
    static constexpr int kPollPlayingMs = 2000;
    static constexpr int kPollIdleMs = 6000;
    // Liked Songs can be thousands: list this many (newest first)
    static constexpr int kMaxCollectionTracks = 1000;
    // A dragged slider sends its latest value at most this often
    static constexpr int kVolumeSendMs = 300;

    explicit SpotifyManager(QObject *parent = nullptr);
    ~SpotifyManager() override;

    // Where the tokens live (tests); the browser build ignores it
    void setStoreFile(const QString &path);

    QString state() const { return m_state; }
    bool signedIn() const { return m_state == QLatin1String("signedIn"); }
    QString clientId() const { return m_clientId; }
    bool builtInClientId() const;
    QString redirectUri() const;
    QString userName() const { return m_userName; }
    QString status() const { return m_status; }
    QVariantList playlists() const { return m_playlists; }
    QVariantList albums() const { return m_albums; }
    bool libraryLoading() const { return m_libraryLoading; }
    QString collectionUri() const { return m_collection.value(QStringLiteral("uri")).toString(); }
    QVariantMap collection() const { return m_collection; }
    QVariantList collectionTracks() const { return m_collectionTracks; }
    bool collectionLoading() const { return m_collectionLoading; }
    QVariantList devices() const { return m_devices; }

    bool hasPlayback() const { return m_player.valid; }
    bool playing() const { return m_player.playing; }
    QString title() const { return m_player.title; }
    QString artist() const { return m_player.artist; }
    QString album() const { return m_player.album; }
    QString trackUri() const { return m_player.trackUri; }
    QString contextUri() const { return m_player.contextUri; }
    QString artUrl() const { return m_artUrl; }
    int durationMs() const { return m_player.durationMs; }
    // Extrapolated from the last report while playing
    int positionMs() const;
    bool shuffle() const { return m_player.shuffle; }
    QString repeat() const { return m_player.repeat; }
    QString deviceName() const { return m_player.deviceName; }
    int volumePercent() const { return m_player.volumePercent; }
    bool supportsVolume() const { return m_player.supportsVolume; }

signals:
    void stateChanged();
    void clientIdChanged();
    void userChanged();
    void statusChanged();
    void libraryChanged();
    void libraryLoadingChanged();
    void collectionChanged();
    void collectionTracksChanged();
    void devicesChanged();
    void playingChanged();
    void trackChanged();
    void artChanged();
    void positionReported();
    void modesChanged();
    void deviceChanged();
    // The user started Spotify from OCTAVE: NowPlaying makes it the source
    void playbackRequested();
    // Spotify started playing by itself (from the phone, another app)
    void startedElsewhere();

public slots:
    void setClientId(const QString &clientId);
    // Opens Spotify's sign-in (the system browser, or this page in the browser build)
    void signIn();
    void signOut();
    void refreshLibrary();
    void refreshDevices();
    // Load a collection's tracks: a playlist, an album or Liked Songs (by uri)
    void openCollection(const QString &uri);
    // Play a collection from track `index` (-1: from the top, or shuffled)
    void playCollection(const QString &uri, int index = -1);
    void togglePlay();
    void play();
    void pause();
    void next();
    void previous();
    void seek(int positionMs);
    void setShuffle(bool on);
    void toggleShuffle();
    // off -> context -> track -> off
    void cycleRepeat();
    // The playing device's volume, 0-100 (OCTAVE's volume while Spotify is
    // the source: AppCore routes it here). Coalesced to one call per kVolumeSendMs
    void setVolume(int percent);
    // Move playback to a device ({id} from `devices`) and play there
    void transferTo(const QString &deviceId);

private:
    using Done = std::function<void(int status, const QJsonDocument &doc)>;

    struct Stored
    {
        QString clientId, refreshToken, userId, userName;
        // Browser sign-in in flight
        QString verifier, oauthState;
    };
    Stored load() const;
    void save() const;
    // At startup: the saved session back, or (browser) a sign-in finished
    void restore();
    void finishBrowserSignIn(const Stored &stored);

    void setState(const QString &s);
    void setStatus(const QString &s);
    void startLoopback();
    void stopLoopback();
    void exchangeCode(const QString &code, const QString &verifier);
    void tokenRequest(const QList<QPair<QString, QString>> &form, bool isRefresh);
    void refreshToken();
    void onSignedIn();
    // Runs `call` with a fresh access token (refreshing first when needed)
    void withToken(std::function<void()> call);
    void api(const QByteArray &verb, const QString &path, const QJsonDocument &body, Done done, bool retried = false);
    void get(const QString &path, Done done) { api("GET", path, {}, std::move(done)); }
    void command(const QByteArray &verb, const QString &path, const QJsonDocument &body = {});
    void loadPlaylistPage(const QString &path, QVariantList acc);
    void loadAlbumPage(const QString &path, QVariantList acc);
    void loadTracksPage(const QString &uri, const QString &path, const QString &albumImage);
    void poll();
    void pollSoon(int ms = 500);
    void applyPlayer(const SpotifyApi::PlayerState &s);
    void fetchArt(const QString &url);
    void schedulePoll();
    void clearSession();

    QNetworkAccessManager m_net;
    QString m_storeFile;
    QString m_state = QStringLiteral("signedOut");
    QString m_clientId;
    QString m_status;

    QString m_userId, m_userName;
    QString m_refreshToken, m_accessToken;
    QElapsedTimer m_tokenAge;
    int m_tokenLifeMs = 0;
    bool m_refreshing = false;
    QList<std::function<void()>> m_waiting;

    // Desktop sign-in
    QTcpServer *m_loopback = nullptr;
    QString m_verifier, m_oauthState;
    QTimer m_signInTimeout;

    QVariantList m_playlists, m_albums;
    QVariantMap m_liked;
    bool m_libraryLoading = false;
    int m_libraryPending = 0;
    QVariantMap m_collection;
    QVariantList m_collectionTracks;
    bool m_collectionLoading = false;
    int m_collectionGen = 0;
    QVariantList m_devices;

    SpotifyApi::PlayerState m_player;
    QElapsedTimer m_positionAge;
    QString m_artUrl, m_artSource;
    QTimer m_poll;
    QTimer m_volumeSend;
    int m_volumeWanted = -1;
    QElapsedTimer m_volumeAge;   // since the last volume sent: polls keep it meanwhile
    bool m_pollInFlight = false;
    // Optimistic state from a command holds until Spotify has caught up
    QElapsedTimer m_commandAge;
    // Rate limited: no calls until then
    QElapsedTimer m_backoff;
    int m_backoffMs = 0;
};

#endif // SPOTIFYMANAGER_H
