#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QUrl>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariantAnimation>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QRegularExpression>

class SettingsManager;

// ─── Metadata cache entry ──────────────────────────────────────────
struct MediaMetadata {
    QString title;
    QString artist;
    QString album;
    int durationSeconds = 0;
};

// ─── Playlist entry ────────────────────────────────────────────────
struct PlaylistInfo {
    QString name;
    QString path;
    QStringList files;
    int songCount = 0;
    bool isCombined = false;
};

class MediaManager : public QObject
{
    Q_OBJECT

public:
    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager() override;

    // ──────────────────────────────────────────────────────────────
    // Settings manager wiring (replaces Python connect_settings_manager)
    // ──────────────────────────────────────────────────────────────
    void setSettingsManager(SettingsManager *sm);

signals:
    // ──────────────────────────────────────────────────────────────
    // Signals — names MUST match the Python originals for QML binding
    // ──────────────────────────────────────────────────────────────
    void playbackStateChanged(int state);
    void playStateChanged(bool playing);
    void currentMediaChanged(const QString &filename);
    void mediaListChanged(const QStringList &files);
    void muteChanged(bool muted);
    void durationChanged(qint64 durationMs);
    void positionChanged(qint64 positionMs);
    void metadataChanged(const QString &title, const QString &artist, const QString &album);
    void durationFormatChanged(const QString &formatted);
    void volumeChanged(float volume);
    void shuffleStateChanged(bool shuffled);
    void totalDurationChanged(const QString &formatted);
    void albumCountChanged(int count);
    void artistCountChanged(int count);
    void playlistsChanged();
    void currentPlaylistChanged(const QString &name);
    void scanProgress(const QString &message);
    void albumColorsExtracted(const QString &jsonColors);
    void songDeleted(const QString &filename);

public slots:
    // ──────────────────────────────────────────────────────────────
    // Slots — names MUST match the Python @Slot originals for QML
    // ──────────────────────────────────────────────────────────────

    // QML compatibility aliases (Python API names called from QML)
    Q_INVOKABLE void connect_settings_manager(QObject *) { /* already wired in main.cpp */ }
    Q_INVOKABLE void _save_playback_state() { _save_playback_state_now(); }
    Q_INVOKABLE void _clear_temp_files() { clearTempFilesInternal(); }

    // Playback
    void play_file(const QString &filename);
    void next_track();
    void previous_track();
    void pause();
    void toggle_play();
    void toggle_mute();
    void toggle_shuffle();
    void setVolume(float volume);
    // Temporarily attenuate the output (1.0 = none, 0.1 = -20 dB) while another
    // source, e.g. the mirrored phone's navigation prompt, needs to be heard.
    // Independent of the user's volume: setVolume/mute keep working underneath
    // and the ramp restores the full level when the factor returns to 1.0.
    void setDucking(float factor);
    void set_position(int positionMs);

    // Metadata / album art
    void extract_colors_from_album_art(const QString &filename);

    // Library scanning
    void scan_library(bool resetDisplayNames = true);
    void set_library_root(const QString &path);
    void strip_filenames();

    // Playlist management
    void select_playlist(const QString &name);
    bool create_playlist(const QString &name);
    bool delete_playlist(const QString &name);
    bool move_song_to_playlist(const QString &filename, const QString &targetPlaylist);
    bool delete_song(const QString &filename);

    // Playback state for download integration
    bool play_downloaded_song(const QString &artist, const QString &name, const QString &playlist);
    bool play_file_at_path(const QString &filePath);

    // Stats cache invalidation
    void invalidate_stats_cache();

    // ──────────────────────────────────────────────────────────────
    // Q_INVOKABLE getters — match Python @Slot(result=...) methods
    // ──────────────────────────────────────────────────────────────
    Q_INVOKABLE QStringList get_media_files(bool emitSignal = true);
    Q_INVOKABLE QString get_formatted_duration(const QString &filename);
    Q_INVOKABLE QString get_band(const QString &filename);
    Q_INVOKABLE QString get_album(const QString &filename);
    Q_INVOKABLE QString get_album_art(const QString &filename);
    Q_INVOKABLE QString get_current_file();
    Q_INVOKABLE QString get_next_track_name();
    Q_INVOKABLE QString get_previous_track_name();
    Q_INVOKABLE QString get_next_track_album_art();
    Q_INVOKABLE QString get_previous_track_album_art();
    Q_INVOKABLE QStringList get_neighbor_album_arts();
    Q_INVOKABLE bool is_playing();
    Q_INVOKABLE bool is_paused();
    Q_INVOKABLE float get_duration();
    Q_INVOKABLE float get_position();
    Q_INVOKABLE bool is_muted();
    Q_INVOKABLE float getVolume();
    Q_INVOKABLE bool is_shuffled();
    Q_INVOKABLE QStringList get_playlist_names();
    Q_INVOKABLE QString get_current_playlist_name();
    Q_INVOKABLE QStringList get_current_song_list();
    Q_INVOKABLE QStringList get_movable_playlist_names();
    Q_INVOKABLE QString get_song_file_path(const QString &filename);
    Q_INVOKABLE QString get_full_file_path(const QString &filename);
    Q_INVOKABLE QString get_display_name(const QString &filename);
    Q_INVOKABLE QString get_default_media_dir();
    Q_INVOKABLE QString get_media_folder_name();
    Q_INVOKABLE QString get_total_duration();
    Q_INVOKABLE int get_album_count();
    Q_INVOKABLE int get_artist_count();
    Q_INVOKABLE QStringList sort_media_files(const QString &sortColumn, bool ascending = true);

private slots:
    void _update_position();
    void _handle_media_status(QMediaPlayer::MediaStatus status);
    void _handle_player_error(QMediaPlayer::Error error, const QString &errorString);
    void _save_playback_state_now();
    void _precache_neighbors_start();
    void _unlock_mute_toggle();
    void _on_theme_changed(const QString &theme);
    void _on_media_changed_for_theme(const QString &filename);
    void _extract_colors_on_startup();

private:
    // ──────────────────────────────────────────────────────────────
    // Internal helpers
    // ──────────────────────────────────────────────────────────────
    void _ensure_directories();
    void clearTempFilesInternal();
    void _cache_metadata(const QString &filename);
    void _emit_metadata(const QString &filename);
    QString _get_album_id(const QString &filename);
    void _manage_cache(const QString &newAlbumId);
    void _attempt_playback_recovery();
    QStringList _shuffle_playlist();
    QStringList _get_current_playlist_files();
    QString _get_file_path(const QString &filename);
    QString _get_original_filename(const QString &filename);
    void _calculate_all_stats();
    QString _format_duration(qint64 ms);
    void _save_playback_state_debounced();
    void _restore_playback_state();
    void _set_playing_state(bool isPlaying);
    void _load_display_names();
    void _save_display_names();
    void _auto_strip_display_names();
    QString _strip_single_filename(const QString &filename);
    void _scan_library_inner(bool resetDisplayNames);
    void _precache_tracks(const QStringList &filenames);
    void update_media_directory(const QString &directory);

    // Album art extraction helpers (TagLib)
    QString _extract_album_art_mp3(const QString &filePath, const QString &albumId);
    QString _extract_album_art_mp4(const QString &filePath, const QString &albumId);
    QString _extract_album_art_flac(const QString &filePath, const QString &albumId);
    QString _extract_album_art_ogg(const QString &filePath, const QString &albumId);

    // Color extraction (k-means)
    QString _extract_album_colors(const QString &imagePath);
    QVector<QVector<int>> _kmeans_colors(const QVector<QVector<int>> &pixels, int k = 5, int maxIterations = 10);
    QString _generate_theme_from_colors(const QVector<QVector<int>> &colors, bool isDark);

    // Path safety
    static bool _is_safe_path(const QString &basePath, const QString &targetPath);
    static QString _sanitize_metadata(const QString &text, int maxLength = 200);

    // ──────────────────────────────────────────────────────────────
    // Member variables
    // ──────────────────────────────────────────────────────────────
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    SettingsManager *m_settingsManager = nullptr;

    // Directories
    QString m_backendDir;
    QString m_defaultMediaDir;
    QString m_mediaDir;
    QString m_tempDir;

    // Playback state
    int m_currentIndex = 0;
    bool m_isMuted = false;
    float m_volume = 0.5f;          // user's volume (linear), before ducking and mute
    float m_previousVolume = 0.5f;
    float m_duckTarget = 1.0f;      // requested ducking factor
    float m_duckCurrent = 1.0f;     // ramped factor actually applied
    QVariantAnimation m_duckAnim;
    void applyOutputVolume();       // output = muted ? 0 : m_volume * m_duckCurrent
    bool m_isPlaying = false;
    bool m_isPaused = true;
    bool m_shuffle = false;
    bool m_autoPlay = false;

    // Playlist management
    QStringList m_originalFiles;
    QStringList m_currentPlaylist;

    // Caching
    QHash<QString, QString> m_albumArtCache;     // albumId -> file:// URL
    QHash<QString, MediaMetadata> m_metadataCache;
    int m_maxCacheFiles = 500;
    int m_metadataCacheMax = 1000;
    QHash<QString, int> m_accessCount;

    // Album Art Capture theme active state
    bool m_albumArtCaptureActive = false;

    // Statistics cache
    struct StatsCache {
        qint64 totalDurationMs = 0;
        QString totalDurationFormatted = QStringLiteral("0:00:00");
        int albumCount = 0;
        int artistCount = 0;
        bool isValid = false;
    } m_statsCache;

    // Playlist management
    QString m_libraryRoot;
    QHash<QString, PlaylistInfo> m_playlists;
    QStringList m_playlistNames;
    QString m_currentPlaylistName;

    // All Music playlist support
    QHash<QString, QString> m_allMusicFilePaths;  // filename -> directory path
    bool m_isAllMusicActive = false;

    // Mute toggle guard
    bool m_muteToggleLocked = false;

    // Scan guard
    bool m_scanInProgress = false;

    // Display names
    QHash<QString, QString> m_displayNames;
    QString m_displayNamesPath;

    // Timers
    QTimer m_positionTimer;
    QTimer m_saveStateTimer;
    QTimer m_precacheTimer;

    // Junk patterns for display name stripping
    static QRegularExpression s_junkRe;
    static QRegularExpression s_trackNumRe;

    // Audio extensions
    static const QStringList s_audioExtensions;
};

#endif // MEDIAMANAGER_H
