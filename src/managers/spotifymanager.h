#ifndef SPOTIFYMANAGER_H
#define SPOTIFYMANAGER_H

// Spotify is desktop-only — excluded from Android builds (no OAuth URI-scheme
// redirect handler wired yet, and per scope decision Spotify lives on desktop).
#ifndef Q_OS_MOBILE

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QImage>
#include <QElapsedTimer>

class SettingsManager;

class SpotifyManager : public QObject
{
    Q_OBJECT

public:
    explicit SpotifyManager(QObject *parent = nullptr);
    ~SpotifyManager() override;

    // ──────────────────────────────────────────────────────────────
    // Settings manager wiring
    // ──────────────────────────────────────────────────────────────
    void setSettingsManager(SettingsManager *sm);

signals:
    // ──────────────────────────────────────────────────────────────
    // Signals — names MUST match the Python originals for QML binding
    // ──────────────────────────────────────────────────────────────

    // Connection state
    void connectionStateChanged(bool connected);
    void errorOccurred(const QString &message);

    // Playback state (mirrors MediaManager signals)
    void playStateChanged(bool playing);
    void currentTrackChanged(const QString &title, const QString &artist,
                             const QString &album, const QString &artUrl);
    void durationChanged(int durationMs);
    void positionChanged(int positionMs);
    void shuffleStateChanged(bool shuffled);
    void volumeChanged(int volume);

    // Device
    void devicesChanged(const QVariantList &devices);
    void activeDeviceChanged(const QString &deviceName);

    // Playlist / library
    void playlistsChanged(const QVariantList &playlists);
    void spotifyTracksChanged(const QVariantList &tracks);
    void currentSpotifyPlaylistChanged(const QString &playlistName);

    // Auth flow
    void authUrlReady(const QString &url);

    // Status progress (terminal-style feedback)
    void statusProgress(const QString &message);

    // Album art capture (matches MediaManager)
    void albumColorsExtracted(const QString &jsonColors);

    // Queue neighbor
    void queueUpdated();

public slots:
    // ──────────────────────────────────────────────────────────────
    // Slots — names MUST match the Python @Slot originals for QML
    // ──────────────────────────────────────────────────────────────

    // QML compatibility alias (Python API name called from QML)
    Q_INVOKABLE void connect_settings_manager(QObject *) { /* already wired in main.cpp */ }

    // Auth
    Q_INVOKABLE void authenticate();
    Q_INVOKABLE void logout();
    // Alias matching the Python backend's @Slot name — QML's MediaSettingsPage
    // calls `spotifyManager.disconnect()`, which both backends must honor.
    Q_INVOKABLE void disconnect() { logout(); }

    // Playback control
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle_play();
    Q_INVOKABLE void next_track();
    Q_INVOKABLE void previous_track();
    Q_INVOKABLE void set_position(int positionMs);
    Q_INVOKABLE void set_volume(int volumePercent);
    Q_INVOKABLE void toggle_shuffle();
    Q_INVOKABLE void play_uri(const QString &uri);

    // Device management
    Q_INVOKABLE void refresh_devices();
    Q_INVOKABLE void set_active_device(const QString &deviceId);

    // Playlist management
    Q_INVOKABLE void select_spotify_playlist(const QString &playlistId);

    // Getters (match Python @Slot(result=...) signatures)
    Q_INVOKABLE bool is_available();
    Q_INVOKABLE bool is_connected();
    Q_INVOKABLE bool is_playing();
    Q_INVOKABLE bool is_shuffled();
    Q_INVOKABLE bool has_credentials();
    Q_INVOKABLE bool has_spotify_playlist_loaded();

    Q_INVOKABLE QString get_current_track_name();
    Q_INVOKABLE QString get_current_artist();
    Q_INVOKABLE QString get_current_album();
    Q_INVOKABLE QString get_current_album_art();
    Q_INVOKABLE QString get_current_file();   // compat with MediaManager

    Q_INVOKABLE int get_duration();
    Q_INVOKABLE int get_position();
    Q_INVOKABLE int get_volume();

    Q_INVOKABLE QVariantList get_devices();
    Q_INVOKABLE QVariantList get_playlists();

    Q_INVOKABLE QStringList get_spotify_playlist_names();
    Q_INVOKABLE QVariantList get_spotify_tracks();
    Q_INVOKABLE QString get_current_spotify_playlist_name();
    Q_INVOKABLE QString get_current_spotify_playlist_id();
    Q_INVOKABLE QString get_spotify_playlist_id(const QString &playlistName);

    Q_INVOKABLE QString get_spotify_track_image(const QVariant &trackName);
    Q_INVOKABLE QString get_spotify_track_duration_formatted(const QVariant &trackName);
    Q_INVOKABLE QString get_spotify_track_artist(const QVariant &trackName);
    Q_INVOKABLE QString get_spotify_track_album(const QVariant &trackName);
    Q_INVOKABLE QString get_spotify_track_uri(const QVariant &trackName);

    Q_INVOKABLE QString get_next_track_info();
    Q_INVOKABLE QString get_previous_track_info();

    Q_INVOKABLE void cleanup();

private slots:
    // ──────────────────────────────────────────────────────────────
    // Internal slots for async HTTP and timer callbacks
    // ──────────────────────────────────────────────────────────────
    void onPollTimeout();
    void onInterpolationTimeout();
    void onNewOAuthConnection();

private:
    // ──────────────────────────────────────────────────────────────
    // OAuth2 helpers
    // ──────────────────────────────────────────────────────────────
    void loadCredentials();
    void tryConnectWithCachedToken();
    void startAuthServer();
    void handleOAuthCallback(const QByteArray &requestData);
    void exchangeCodeForToken(const QString &code);
    void refreshAccessToken();
    bool isTokenExpired() const;
    void saveTokenToCache(const QJsonObject &tokenInfo);
    QJsonObject loadTokenFromCache();
    void deleteTokenCache();
    QString tokenCachePath() const;

    // ──────────────────────────────────────────────────────────────
    // Spotify REST API helpers
    // ──────────────────────────────────────────────────────────────
    QNetworkRequest buildApiRequest(const QString &endpoint) const;
    void fetchPlaylistItemsPage(const QString &playlistId, const QString &playlistName, int offset);
    void apiGet(const QString &endpoint,
                std::function<void(const QJsonDocument &)> onSuccess,
                std::function<void(const QString &)> onError = nullptr);
    void apiPut(const QString &endpoint,
                const QByteArray &body = QByteArray(),
                std::function<void()> onSuccess = nullptr,
                std::function<void(const QString &)> onError = nullptr);
    void apiPost(const QString &endpoint,
                 const QByteArray &body = QByteArray(),
                 std::function<void()> onSuccess = nullptr,
                 std::function<void(const QString &)> onError = nullptr);
    void handleApiReply(QNetworkReply *reply,
                        std::function<void(const QJsonDocument &)> onSuccess,
                        std::function<void(const QString &)> onError);
    void handleApiReplyNoBody(QNetworkReply *reply,
                              std::function<void()> onSuccess,
                              std::function<void(const QString &)> onError);

    // ──────────────────────────────────────────────────────────────
    // Playback state
    // ──────────────────────────────────────────────────────────────
    void pollPlaybackState();
    void handlePlaybackState(const QJsonObject &playback);
    void refreshDevices();
    void handleDevicesResult(const QJsonArray &devices);
    void refreshPlaylists();
    void handlePlaylistsResult(const QJsonArray &items);

    // ──────────────────────────────────────────────────────────────
    // Queue / neighbor art
    // ──────────────────────────────────────────────────────────────
    void fetchQueue();
    void handleQueueResult(const QJsonArray &queueTracks);

    // ──────────────────────────────────────────────────────────────
    // Album art color extraction
    // ──────────────────────────────────────────────────────────────
    void onThemeChanged(const QString &theme);
    void onTrackChangedForTheme(const QString &imageUrl);
    void extractColorsFromUrl(const QString &imageUrl);
    void handleImageDownloaded(QNetworkReply *reply);
    QVector<QVector<int>> kmeansColors(const QImage &img, int k = 5, int maxIterations = 10);
    QString generateThemeFromColors(const QVector<QVector<int>> &colors, bool isDark);

    // ──────────────────────────────────────────────────────────────
    // Static color-math helpers (pure functions)
    // ──────────────────────────────────────────────────────────────
    static QVector<int> adjustBrightness(const QVector<int> &rgb, double factor);
    static QVector<int> adjustSaturation(const QVector<int> &rgb, double factor);
    static double getLuminance(const QVector<int> &rgb);
    static double getContrastRatio(const QVector<int> &rgb1, const QVector<int> &rgb2);
    static QVector<int> getReadableTextColor(const QVector<int> &bgRgb);
    static QVector<int> getSecondaryTextColor(const QVector<int> &bgRgb);
    static QVector<int> capBrightness(const QVector<int> &rgb, double maxBrightness = 0.85);
    static QVector<int> ensureIconContrast(const QVector<int> &iconRgb,
                                           const QVector<int> &bgRgb,
                                           double minContrast = 3.0);
    static QString rgbToHex(const QVector<int> &rgb);

    // ──────────────────────────────────────────────────────────────
    // Member variables
    // ──────────────────────────────────────────────────────────────

    SettingsManager *m_settingsManager = nullptr;
    QNetworkAccessManager *m_nam = nullptr;

    // OAuth2 state
    QString m_clientId;
    QString m_clientSecret;
    QString m_redirectUri;
    QString m_accessToken;
    QString m_refreshToken;
    qint64  m_tokenExpiresAt = 0;   // seconds since epoch
    QString m_oauthState;           // CSRF protection
    QTcpServer *m_authServer = nullptr;

    // Connection
    bool m_isConnected = false;

    // Polling
    QTimer m_pollTimer;
    QTimer m_interpolationTimer;
    bool   m_pollInProgress = false;
    int    m_consecutivePlaybackErrors = 0;
    qint64 m_lastPlaybackErrorTs = 0;
    static constexpr int POLL_ERROR_THRESHOLD = 5;
    static constexpr int POLL_ERROR_EMIT_INTERVAL_S = 30;

    // Playback state
    bool m_isPlaying = false;
    bool m_isShuffled = false;
    int  m_currentVolume = 100;
    int  m_currentPositionMs = 0;
    QVariantMap m_currentTrack;
    QVariantList m_devices;
    QString m_activeDeviceId;
    QVariantList m_playlists;

    // Spotify playlist tracks state
    QVariantList m_spotifyTracks;
    QString m_currentSpotifyPlaylistId;
    QString m_currentSpotifyPlaylistName;

    // Queue / neighbor art
    QVariantList m_cachedQueue;
    QVariantMap  m_previousTrack;

    // Position interpolation
    qint64 m_lastKnownPositionMs = 0;
    qint64 m_lastPollTimestamp = 0;  // milliseconds since epoch

    // Album art capture
    bool    m_albumArtCaptureActive = false;
    QString m_lastExtractedImageUrl;
};

#else // Q_OS_MOBILE — mobile stub keeps QML bindings valid

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class SpotifyManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ is_connected NOTIFY connectionStateChanged)
public:
    explicit SpotifyManager(QObject *parent = nullptr) : QObject(parent) {}
    void setSettingsManager(QObject *) {}
    void cleanup() {}

    Q_INVOKABLE bool is_connected() const { return false; }
    Q_INVOKABLE bool is_playing() const { return false; }
    Q_INVOKABLE bool is_shuffled() const { return false; }
    Q_INVOKABLE bool has_credentials() const { return false; }
    Q_INVOKABLE bool has_spotify_playlist_loaded() const { return false; }
    Q_INVOKABLE QString get_current_track_name() const { return {}; }
    Q_INVOKABLE QString get_current_artist() const { return {}; }
    Q_INVOKABLE QString get_current_album() const { return {}; }
    Q_INVOKABLE QString get_current_album_art() const { return {}; }
    Q_INVOKABLE QString get_current_spotify_playlist_name() const { return {}; }
    Q_INVOKABLE QString get_current_spotify_playlist_id() const { return {}; }
    Q_INVOKABLE int get_duration() const { return 0; }
    Q_INVOKABLE int get_position() const { return 0; }
    Q_INVOKABLE QVariantList get_spotify_playlist_names() const { return {}; }
    Q_INVOKABLE QVariantList get_devices() const { return {}; }
    Q_INVOKABLE QVariantMap get_previous_track_info() const { return {}; }
    Q_INVOKABLE QVariantMap get_next_track_info() const { return {}; }
    Q_INVOKABLE QString get_spotify_playlist_id(const QString &) const { return {}; }
    Q_INVOKABLE QString get_spotify_track_image(const QString &) const { return {}; }
    Q_INVOKABLE QString get_spotify_track_duration_formatted(const QString &) const { return {}; }
    Q_INVOKABLE QString get_spotify_track_artist(const QString &) const { return {}; }
    Q_INVOKABLE QString get_spotify_track_album(const QString &) const { return {}; }
    Q_INVOKABLE QString get_spotify_track_uri(const QString &) const { return {}; }

public slots:
    void previous_track() {}
    void next_track() {}
    void toggle_play() {}
    void pause() {}
    void toggle_shuffle() {}
    void set_position(int) {}
    void set_volume(int) {}
    void authenticate() {}
    void disconnect() {}   // intentionally shadows QObject::disconnect; QML calls this
    void refresh_devices() {}
    void set_active_device(const QString &) {}
    void select_spotify_playlist(const QString &) {}
    void play_uri(const QString &) {}

signals:
    void connectionStateChanged(bool);
    void errorOccurred(const QString &);
    void playStateChanged(bool);
    void currentTrackChanged(const QString &, const QString &, const QString &, const QString &);
    void durationChanged(int);
    void positionChanged(int);
    void shuffleStateChanged(bool);
    void volumeChanged(int);
    void devicesChanged(const QVariantList &);
    void activeDeviceChanged(const QString &);
    void playlistsChanged(const QVariantList &);
    void spotifyTracksChanged(const QVariantList &);
    void currentSpotifyPlaylistChanged(const QString &);
    void authUrlReady(const QString &);
    void statusProgress(const QString &);
    void albumColorsExtracted(const QString &);
    void queueUpdated();
};

#endif // Q_OS_MOBILE
#endif // SPOTIFYMANAGER_H
