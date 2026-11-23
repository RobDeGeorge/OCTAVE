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
#include <QJsonObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QFuture>
#include <functional>

#include <atomic>
#include <memory>

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

// TagLib reads tags and covers on desktop and in the browser demo; Android
// reads them through the platform (JNI), other mobile targets use file names
#if !defined(Q_OS_MOBILE) || defined(OCTAVE_WASM)
#define OCTAVE_TAGLIB 1
#endif

class MediaManager : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "media")
    Q_CLASSINFO("octave.scope.read",
                "browseMusicFolders browse_music_folders getAlbum getAlbumArt getAlbumCount getArtistCount "
                "getBand getCurrentFile getCurrentPlaylistName getCurrentSongList getDefaultMediaDir "
                "getDisplayName getDuration getFormattedDuration getFullFilePath getMediaFiles "
                "getMediaFolderName getMovablePlaylistNames getNeighborAlbumArts getNextTrackAlbumArt "
                "getNextTrackName getPlaylistNames getPosition getPreviousTrackAlbumArt getPreviousTrackName "
                "getSongFilePath getSongPlaylistName getTotalDuration getVolume get_album get_album_art "
                "get_album_count get_artist_count get_band get_current_file get_current_playlist_name "
                "get_current_song_list get_default_media_dir get_display_name get_duration "
                "get_formatted_duration get_full_file_path get_media_files get_media_folder_name "
                "get_movable_playlist_names get_neighbor_album_arts get_next_track_album_art get_next_track_name "
                "get_playlist_names get_position get_previous_track_album_art get_previous_track_name "
                "get_song_file_path get_song_playlist_name get_total_duration isMuted isPaused isPlaying "
                "isShuffled is_muted is_paused is_playing is_shuffled")
    Q_CLASSINFO("octave.scope.files",
                "createPlaylist create_playlist deletePlaylist deleteSong delete_playlist delete_song "
                "flushMetadataStore flush_metadata_store moveSongToPlaylist move_song_to_playlist "
                "pickBrowserLibrary pick_browser_library setLibraryRoot set_library_root stripFilenames "
                "strip_filenames")
    Q_CLASSINFO("octave.scope.full",
                "_clear_temp_files _save_playback_state connect_settings_manager extractColorsFromAlbumArt "
                "extractColorsFromImage extract_colors_from_album_art extract_colors_from_image "
                "invalidateStatsCache invalidate_stats_cache setDucking setMuted setVolume")
    // Deprecated names (src/service/metadescribe.h): the camelCase one beside each stays
    Q_CLASSINFO("octave.deprecated.connect_settings_manager", "does nothing; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated._save_playback_state", "internal; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated._clear_temp_files", "internal; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.flush_metadata_store", "use flushMetadataStore; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.play_file", "use playFile; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.next_track", "use nextTrack; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.previous_track", "use previousTrack; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.toggle_play", "use togglePlay; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.toggle_shuffle", "use toggleShuffle; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.set_position", "use setPosition; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.extract_colors_from_album_art", "use extractColorsFromAlbumArt; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.extract_colors_from_image", "use extractColorsFromImage; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.scan_library", "use scanLibrary; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.set_library_root", "use setLibraryRoot; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.strip_filenames", "use stripFilenames; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.select_playlist", "use selectPlaylist; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.create_playlist", "use createPlaylist; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.delete_playlist", "use deletePlaylist; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.move_song_to_playlist", "use moveSongToPlaylist; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.delete_song", "use deleteSong; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.play_file_at_path", "use playFileAtPath; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.invalidate_stats_cache", "use invalidateStatsCache; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_media_files", "use getMediaFiles; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_formatted_duration", "use getFormattedDuration; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_band", "use getBand; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_album", "use getAlbum; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_album_art", "use getAlbumArt; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_current_file", "use getCurrentFile; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_next_track_name", "use getNextTrackName; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_previous_track_name", "use getPreviousTrackName; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_next_track_album_art", "use getNextTrackAlbumArt; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_previous_track_album_art", "use getPreviousTrackAlbumArt; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_neighbor_album_arts", "use getNeighborAlbumArts; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.is_playing", "use isPlaying; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.is_paused", "use isPaused; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_duration", "use getDuration; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_position", "use getPosition; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.is_muted", "use isMuted; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.is_shuffled", "use isShuffled; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_playlist_names", "use getPlaylistNames; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_current_playlist_name", "use getCurrentPlaylistName; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_current_song_list", "use getCurrentSongList; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_movable_playlist_names", "use getMovablePlaylistNames; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_song_playlist_name", "use getSongPlaylistName; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_song_file_path", "use getSongFilePath; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_full_file_path", "use getFullFilePath; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_display_name", "use getDisplayName; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_default_media_dir", "use getDefaultMediaDir; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_media_folder_name", "use getMediaFolderName; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.browse_music_folders", "use browseMusicFolders; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.pick_browser_library", "use pickBrowserLibrary; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_total_duration", "use getTotalDuration; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_album_count", "use getAlbumCount; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_artist_count", "use getArtistCount; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.sort_media_files", "use sortMediaFiles; goes in protocol 5")
    // Every audio output as {id, name, isDefault}, and the name of the one
    // playing now: the one in settingsManager.audioOutputDevice while it is
    // plugged in, else the system default (src/util/audiooutput.h)
    Q_PROPERTY(QVariantList audioOutputs READ audioOutputs NOTIFY audioOutputsChanged)
    Q_PROPERTY(QString currentAudioOutput READ currentAudioOutput NOTIFY audioOutputsChanged)

public:
    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager() override;

    // ──────────────────────────────────────────────────────────────
    // Settings manager wiring
    // ──────────────────────────────────────────────────────────────
    void setSettingsManager(SettingsManager *sm);

    QVariantList audioOutputs() const;
    QString currentAudioOutput() const;

signals:
    // ──────────────────────────────────────────────────────────────
    // Signals — the names are the QML binding API; don't rename them
    // ──────────────────────────────────────────────────────────────
    void playbackStateChanged(int state);
    void playStateChanged(bool playing);
    void currentMediaChanged(const QString &filename);
    void mediaListChanged(const QStringList &files);
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
    // The output list, or the output in use, changed
    void audioOutputsChanged();

public slots:
    // ──────────────────────────────────────────────────────────────
    // Slots — names are called from QML; don't rename them
    // ──────────────────────────────────────────────────────────────

    // QML compatibility aliases (snake_case names called from QML)
    // Does nothing: AppCore wires the settings manager (setSettingsManager).
    // Kept because it is published API and Main.qml still calls it.
    Q_INVOKABLE void connect_settings_manager(QObject *) {}
    Q_INVOKABLE void _save_playback_state() { _save_playback_state_now(); }
    Q_INVOKABLE void _clear_temp_files() { clearTempFilesInternal(); }
    // Write the persistent tag store now if it changed (called at shutdown)
    Q_INVOKABLE void flush_metadata_store() { _save_meta_store(); }

    // Playback
    void play_file(const QString &filename);
    void next_track();
    void previous_track();
    void pause();
    void toggle_play();
    // Follows VolumeController::mutedChanged (wired in main.cpp); the mute
    // rules live there
    void setMuted(bool muted);
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
    // Same, from an image file or file:// URL (phone album art)
    void extract_colors_from_image(const QString &imagePath);

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

    // Play a file by absolute path (selects the playlist holding it)
    bool play_file_at_path(const QString &filePath);

    // Stats cache invalidation
    void invalidate_stats_cache();

    // ──────────────────────────────────────────────────────────────
    // Q_INVOKABLE getters
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
    Q_INVOKABLE QString get_song_playlist_name(const QString &filename);
    Q_INVOKABLE QString get_song_file_path(const QString &filename);
    Q_INVOKABLE QString get_full_file_path(const QString &filename);
    Q_INVOKABLE QString get_display_name(const QString &filename);
    Q_INVOKABLE QString get_default_media_dir();
    Q_INVOKABLE QString get_media_folder_name();
    // A folder picker for frontends without one (util/folderbrowser.h):
    // "" lists the browse roots ({roots: [{name, path}]}: ~/Music,
    // /media, /mnt, /run/media, /storage, the current music folder unless
    // it is "/"), a path inside them its {path, parent, dirs, audioFiles,
    // truncated}, anything else {error}. Choose one with
    // settingsManager.save_media_folder(path).
    Q_INVOKABLE QVariantMap browse_music_folders(const QString &path);
    // The browser demo: opens the browser's folder picker (call from a tap)
    // and makes the folder picked the library for this visit
    // (platform/wasm/weblibrary.h); true. Elsewhere does nothing and returns
    // false: pick a folder with a folder dialog or browse_music_folders.
    Q_INVOKABLE bool pick_browser_library();
    Q_INVOKABLE QString get_total_duration();
    Q_INVOKABLE int get_album_count();
    Q_INVOKABLE int get_artist_count();
    Q_INVOKABLE QStringList sort_media_files(const QString &sortColumn, bool ascending = true);

    // camelCase names for the snake_case methods above (protocol 3 keeps both;
    // the old names are marked octave.deprecated and go in protocol 5)
    Q_INVOKABLE void flushMetadataStore() { flush_metadata_store(); }
    void playFile(const QString &filename) { play_file(filename); }
    void nextTrack() { next_track(); }
    void previousTrack() { previous_track(); }
    void togglePlay() { toggle_play(); }
    void toggleShuffle() { toggle_shuffle(); }
    void setPosition(int positionMs) { set_position(positionMs); }
    void extractColorsFromAlbumArt(const QString &filename) { extract_colors_from_album_art(filename); }
    void extractColorsFromImage(const QString &imagePath) { extract_colors_from_image(imagePath); }
    void scanLibrary(bool resetDisplayNames = true) { scan_library(resetDisplayNames); }
    void setLibraryRoot(const QString &path) { set_library_root(path); }
    void stripFilenames() { strip_filenames(); }
    void selectPlaylist(const QString &name) { select_playlist(name); }
    bool createPlaylist(const QString &name) { return create_playlist(name); }
    bool deletePlaylist(const QString &name) { return delete_playlist(name); }
    bool moveSongToPlaylist(const QString &filename, const QString &targetPlaylist) { return move_song_to_playlist(filename, targetPlaylist); }
    bool deleteSong(const QString &filename) { return delete_song(filename); }
    bool playFileAtPath(const QString &filePath) { return play_file_at_path(filePath); }
    void invalidateStatsCache() { invalidate_stats_cache(); }
    Q_INVOKABLE QStringList getMediaFiles(bool emitSignal = true) { return get_media_files(emitSignal); }
    Q_INVOKABLE QString getFormattedDuration(const QString &filename) { return get_formatted_duration(filename); }
    Q_INVOKABLE QString getBand(const QString &filename) { return get_band(filename); }
    Q_INVOKABLE QString getAlbum(const QString &filename) { return get_album(filename); }
    Q_INVOKABLE QString getAlbumArt(const QString &filename) { return get_album_art(filename); }
    Q_INVOKABLE QString getCurrentFile() { return get_current_file(); }
    Q_INVOKABLE QString getNextTrackName() { return get_next_track_name(); }
    Q_INVOKABLE QString getPreviousTrackName() { return get_previous_track_name(); }
    Q_INVOKABLE QString getNextTrackAlbumArt() { return get_next_track_album_art(); }
    Q_INVOKABLE QString getPreviousTrackAlbumArt() { return get_previous_track_album_art(); }
    Q_INVOKABLE QStringList getNeighborAlbumArts() { return get_neighbor_album_arts(); }
    Q_INVOKABLE bool isPlaying() { return is_playing(); }
    Q_INVOKABLE bool isPaused() { return is_paused(); }
    Q_INVOKABLE float getDuration() { return get_duration(); }
    Q_INVOKABLE float getPosition() { return get_position(); }
    Q_INVOKABLE bool isMuted() { return is_muted(); }
    Q_INVOKABLE bool isShuffled() { return is_shuffled(); }
    Q_INVOKABLE QStringList getPlaylistNames() { return get_playlist_names(); }
    Q_INVOKABLE QString getCurrentPlaylistName() { return get_current_playlist_name(); }
    Q_INVOKABLE QStringList getCurrentSongList() { return get_current_song_list(); }
    Q_INVOKABLE QStringList getMovablePlaylistNames() { return get_movable_playlist_names(); }
    Q_INVOKABLE QString getSongPlaylistName(const QString &filename) { return get_song_playlist_name(filename); }
    Q_INVOKABLE QString getSongFilePath(const QString &filename) { return get_song_file_path(filename); }
    Q_INVOKABLE QString getFullFilePath(const QString &filename) { return get_full_file_path(filename); }
    Q_INVOKABLE QString getDisplayName(const QString &filename) { return get_display_name(filename); }
    Q_INVOKABLE QString getDefaultMediaDir() { return get_default_media_dir(); }
    Q_INVOKABLE QString getMediaFolderName() { return get_media_folder_name(); }
    Q_INVOKABLE QVariantMap browseMusicFolders(const QString &path) { return browse_music_folders(path); }
    Q_INVOKABLE bool pickBrowserLibrary() { return pick_browser_library(); }
    Q_INVOKABLE QString getTotalDuration() { return get_total_duration(); }
    Q_INVOKABLE int getAlbumCount() { return get_album_count(); }
    Q_INVOKABLE int getArtistCount() { return get_artist_count(); }
    Q_INVOKABLE QStringList sortMediaFiles(const QString &sortColumn, bool ascending = true) { return sort_media_files(sortColumn, ascending); }

public:
    // Plain C++, not on the service (no slot, no Q_INVOKABLE)
    // GET /art?song=<filename>: the cover file of a song in the library
    // (its own lookup and cache, get_album_art), only if that file is in
    // the art cache folder; "" otherwise. The service serves the bytes,
    // never the path.
    QString libraryArtFile(const QString &filename);
    // browse_music_folders' roots
    QStringList musicBrowseRoots() const;

private slots:
    void _update_position();
    void _handle_media_status(QMediaPlayer::MediaStatus status);
    void _handle_player_error(QMediaPlayer::Error error, const QString &errorString);
    void _save_playback_state_now();
    void _precache_neighbors_start();

private:
    // tests/cpp checks the cover cache directly
    friend class TstMediaManager;
    // ──────────────────────────────────────────────────────────────
    // Internal helpers
    // ──────────────────────────────────────────────────────────────
    void _ensure_directories();
    void _prune_cover_dir();
    void clearTempFilesInternal();
    void _cache_metadata(const QString &filename);
    void _cache_metadata_from_file(const QString &filename);
    // Every place that drops the tag cache also abandons the background
    // pass that was filling it
    void _clear_metadata_cache();
    // The background tag pass: tags of `files` not yet in the cache are
    // read (or taken from the tag store) on a worker; when it is done the
    // cache has them and the playlist statistics are computed and signalled.
    // Nothing on the GUI thread opens a music file for the statistics or a
    // sort.
    void _warm_metadata(const QStringList &files);
    void _finish_stats(const QStringList &files);
    // The loaded track is about to be deleted or moved: let go of the file
    bool _is_loaded_track(const QString &filename) const;
    void _stop_for_removal();
    // Background work that uses `this`; the destructor waits for it
    void _track(const QFuture<void> &work);
    // Persistent tag store (<appdata>/metadata_cache.json): tags are read from a file once, then served from here
    // on every later run as long as its size and mtime are unchanged.
    void _load_meta_store();
    void _save_meta_store();
    bool _meta_from_store(const QString &filename, const QString &filePath, MediaMetadata *out);
    void _meta_to_store(const QString &filename, const QString &filePath, const MediaMetadata &meta);
    // Cover already extracted to the temp dir for this album id (any run)
    QString _cached_art_url_for(const QString &albumId) const;
    QString _extract_art_for(const QString &filePath, const QString &albumId);
    void _apply_album_colors(const QString &themeJson, const QString &filename);
#ifdef OCTAVE_TAGLIB
    static MediaMetadata _read_tags(const QString &filePath, const QString &baseName);
#endif
    void _emit_metadata(const QString &filename);
    qint64 _player_position() const;
    bool _player_playing() const;
    QString _get_album_id(const QString &filename);
    void _manage_cache(const QString &newAlbumId);
    void _attempt_playback_recovery(bool force = false);
    void _reseat_source(const QString &filePath, qint64 position);
    void _apply_output_device();
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

    // Album art extraction helpers (TagLib)
    QString _extract_album_art_mp3(const QString &filePath, const QString &albumId);
    QString _extract_album_art_mp4(const QString &filePath, const QString &albumId);
    QString _extract_album_art_flac(const QString &filePath, const QString &albumId);
    QString _extract_album_art_ogg(const QString &filePath, const QString &albumId);

    // Color extraction (k-means)
    QString _extract_album_colors(const QString &imagePath);
    void extractColorsFrom(const QString &imagePath, const QString &filename);
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
    QMediaDevices *m_mediaDevices = nullptr;
    QElapsedTimer m_pausedSince;   // valid while paused by pause()/toggle_play()
    QString m_pendingSeekFile;     // seek to m_pendingSeekMs once this has loaded
    qint64 m_pendingSeekMs = 0;
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
    float m_duckTarget = 1.0f;      // requested ducking factor
    float m_duckCurrent = 1.0f;     // ramped factor actually applied
    QVariantAnimation m_duckAnim;
    void applyOutputVolume();       // output = muted ? 0 : m_volume * m_duckCurrent
    bool m_isPlaying = false;
    bool m_isPaused = true;
    bool m_shuffle = false;
    // Recovery bookkeeping: attempts on the current file, and how many
    // tracks in a row were unplayable (a library of stubs must not loop)
    QString m_recoveryFile;
    int m_recoveryAttempts = 0;
    int m_consecutiveBadTracks = 0;
    void _skip_bad_track(bool advance = false);
    bool m_autoPlay = false;

    // Playlist management
    QStringList m_originalFiles;
    QStringList m_currentPlaylist;

    // Caching
    QHash<QString, QString> m_albumArtCache;     // albumId -> file:// URL
    // filename -> tags, for the playlist in use (cleared when it changes).
    // As large as that playlist: it was capped at 1000 with an arbitrary
    // entry evicted, so sorting a bigger playlist re-read tags inside the
    // comparator.
    QHash<QString, MediaMetadata> m_metadataCache;
    int m_maxCacheFiles = 500;
    QHash<QString, int> m_accessCount;

    // Album Art Capture theme active state

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

    // Scan guard: the scan is synchronous but emits (scanProgress,
    // playlistsChanged ...) as it goes, and a handler may ask for another
    bool m_scanInProgress = false;

    // Background tag pass (_warm_metadata)
    int m_warmGeneration = 0;
    bool m_warmRunning = false;
    QStringList m_warmFiles;
    std::shared_ptr<std::atomic_bool> m_warmCancel;
    QList<QFuture<void>> m_background;
    // Tag reads from files so far (tests count them)
    static std::atomic<int> s_tagReads;
    // ...and those of them made on the GUI thread (the song list's rows must make none)
    static std::atomic<int> s_guiTagReads;
    // Takes the tag pass's batches not delivered yet (set while one runs)
    std::function<void()> m_warmDrain;
    bool _row_metadata(const QString &filename);

    // Display names
    QHash<QString, QString> m_displayNames;
    QString m_displayNamesPath;

    // Persistent metadata store (see _load_meta_store)
    QJsonObject m_metaStore;
    QString m_metaStorePath;
    QTimer m_metaStoreSaveTimer;
    bool m_metaStoreDirty = false;

    // Album colour extraction runs on a worker; only the newest job applies
    int m_colorJob = 0;
    QString m_lastColorFile;
    qint64 m_lastColorMs = 0;

    // Timers
    QTimer m_positionTimer;
    QTimer m_saveStateTimer;
    // While playing: the position is saved every 10 s, so a power cut
    // mid-song (ignition off) resumes within seconds of where it was
    QTimer m_positionSaveTimer;
    QTimer m_precacheTimer;

    // Junk patterns for display name stripping
    static QRegularExpression s_junkRe;
    static QRegularExpression s_trackNumRe;

    // Audio extensions
    static const QStringList s_audioExtensions;
};

#endif // MEDIAMANAGER_H
