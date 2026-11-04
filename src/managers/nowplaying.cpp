#include "nowplaying.h"
#include "androidautomedia.h"
#include "mediamanager.h"
#include "phonemirrormanager.h"
#include "settingsmanager.h"
#include "spotifymanager.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNowPlaying, "octave.nowplaying")

namespace {
QString sourceName(NowPlaying::Source s)
{
    switch (s) {
    case NowPlaying::Source::Local: return QStringLiteral("local");
    case NowPlaying::Source::Phone: return QStringLiteral("phone");
    case NowPlaying::Source::AndroidAuto: return QStringLiteral("androidauto");
    case NowPlaying::Source::Spotify: return QStringLiteral("spotify");
    }
    return QStringLiteral("local");
}
}

NowPlaying::NowPlaying(MediaManager *media, PhoneMirrorManager *phone, SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_media(media)
    , m_phoneMirror(phone)
    , m_settings(settings)
{
    m_tick.setInterval(kPhoneTickMs);
    connect(&m_tick, &QTimer::timeout, this, &NowPlaying::tick);

    if (m_media) {
        connect(m_media, &MediaManager::currentMediaChanged, this, [this](const QString &file) {
            m_localFile = file;
            if (m_source == Source::Local) {
                readLocalTrack();
                emit trackChanged();
            }
        });
        // Tags and art can land after the track starts
        connect(m_media, &MediaManager::metadataChanged, this, [this] {
            if (m_source == Source::Local)
                readLocalTrack();
        });
        connect(m_media, &MediaManager::playStateChanged, this, &NowPlaying::onLocalPlayStateChanged);
        connect(m_media, &MediaManager::durationChanged, this, [this](qint64 ms) {
            if (m_source == Source::Local)
                setDuration(int(ms));
        });
        connect(m_media, &MediaManager::positionChanged, this, [this](qint64 ms) {
            if (m_source == Source::Local)
                setPosition(int(ms));
        });
        connect(m_media, &MediaManager::shuffleStateChanged, this, [this](bool on) {
            if (m_source != Source::Spotify)
                setShuffled(on);
        });
        m_localFile = m_media->get_current_file();
        m_shuffled = m_media->is_shuffled();
    }

    if (m_phoneMirror) {
        connect(m_phoneMirror, &PhoneMirrorManager::mediaActiveChanged, this, &NowPlaying::onPhoneActiveChanged);
        connect(m_phoneMirror, &PhoneMirrorManager::mediaChanged, this, [this] {
            if (m_source == Source::Phone)
                readPhoneState();
        });
        connect(m_phoneMirror, &PhoneMirrorManager::mediaTrackChanged, this, [this] {
            if (m_source == Source::Phone) {
                readPhoneTrack();
                emit trackChanged();
            }
        });
        connect(m_phoneMirror, &PhoneMirrorManager::mediaArtChanged, this, [this] {
            if (m_source == Source::Phone)
                setArtUrl(m_phoneMirror->mediaArtUrl());
        });
        if (m_phoneMirror->mediaActive())
            m_source = Source::Phone;
    }

    if (m_settings) {
        m_resumeAa = m_settings->get_setting(QStringLiteral("mediaLastSource")).toString() == QLatin1String("androidauto")
                     && m_settings->get_setting(QStringLiteral("mediaLastPlaying")).toBool();
    }

    readSource();

    // Album Art Capture: the theme follows the art of whatever is playing.
    // An art change covers a new track, a source switch and art arriving
    // late (the phone sends it after the metadata); same art, same colours.
    connect(this, &NowPlaying::artUrlChanged, this, [this] {
        if (albumArtCapture())
            extractArtColors();
    });
    if (m_settings) {
        connect(m_settings, &SettingsManager::themeSettingChanged, this, [this] {
            if (albumArtCapture())
                extractArtColors();
        });
    }
}

void NowPlaying::setAndroidAuto(AndroidAutoMedia *aa)
{
    if (m_aa || !aa)
        return;
    m_aa = aa;
    connect(m_aa, &AndroidAutoMedia::mediaFocusRequested, this, &NowPlaying::onAaFocusRequested);
    connect(m_aa, &AndroidAutoMedia::connectedChanged, this, &NowPlaying::onAaConnectedChanged);
    connect(m_aa, &AndroidAutoMedia::microphoneOpenChanged, this, &NowPlaying::onAaMicrophone);
    connect(m_aa, &AndroidAutoMedia::playingChanged, this, [this] {
        if (m_source != Source::AndroidAuto)
            return;
        // The resume is done once it plays (the first focus request after
        // connecting is Android Auto's own, not the music)
        if (m_aa->playing())
            m_resumeAa = false;
        setPlaying(m_aa->playing());
        // Not while it disconnects: an unplugged phone resumes when it's back
        if (m_aa->connected())
            remember();
    });
    connect(m_aa, &AndroidAutoMedia::trackChanged, this, [this] {
        if (m_source == Source::AndroidAuto) {
            readAaTrack();
            emit trackChanged();
        }
    });
    connect(m_aa, &AndroidAutoMedia::artChanged, this, [this] {
        if (m_source == Source::AndroidAuto)
            setArtUrl(m_aa->artUrl());
    });
    connect(m_aa, &AndroidAutoMedia::positionReported, this, [this] {
        if (m_source == Source::AndroidAuto)
            setPosition(m_aa->positionMs());
    });
}

void NowPlaying::setSpotify(SpotifyManager *spotify)
{
    if (m_spotify || !spotify)
        return;
    m_spotify = spotify;
    // Played from OCTAVE's Spotify pages: the user's choice
    connect(m_spotify, &SpotifyManager::playbackRequested, this, [this] {
        // Mirroring: it plays on the phone, the phone already has the controls
        // (and its audio comes through OCTAVE, on OCTAVE's volume)
        if (m_source == Source::Phone && phoneRunsSpotify())
            return;
        switchTo(Source::Spotify);
    });
    connect(m_spotify, &SpotifyManager::startedElsewhere, this, &NowPlaying::onSpotifyStartedElsewhere);
    connect(m_spotify, &SpotifyManager::playingChanged, this, [this] {
        if (m_source == Source::Spotify) {
            setPlaying(m_spotify->playing());
            remember();
        }
    });
    connect(m_spotify, &SpotifyManager::trackChanged, this, [this] {
        if (m_source == Source::Spotify) {
            readSpotify();
            emit trackChanged();
        }
    });
    connect(m_spotify, &SpotifyManager::artChanged, this, [this] {
        if (m_source == Source::Spotify)
            setArtUrl(m_spotify->artUrl());
    });
    connect(m_spotify, &SpotifyManager::positionReported, this, [this] {
        if (m_source == Source::Spotify)
            setPosition(m_spotify->positionMs());
    });
    connect(m_spotify, &SpotifyManager::modesChanged, this, [this] {
        if (m_source == Source::Spotify)
            setShuffled(m_spotify->shuffle());
    });
    // Signed out while it owned the music: back to OCTAVE's player, paused
    connect(m_spotify, &SpotifyManager::stateChanged, this, [this] {
        if (m_source == Source::Spotify && !m_spotify->signedIn())
            switchTo(Source::Local);
    });
}

void NowPlaying::onSpotifyStartedElsewhere()
{
    // Started from the phone or another Spotify app: shown here when
    // nothing else plays (the user is listening to it); never over music
    // another source is playing
    if (m_source == Source::Spotify || m_playing)
        return;
    qCInfo(lcNowPlaying) << "Spotify started playing elsewhere; nothing else plays, so it owns the music";
    switchTo(Source::Spotify);
}

bool NowPlaying::phoneRunsSpotify() const
{
    return m_phoneMirror && m_phoneMirror->mediaApp().startsWith(QLatin1String("com.spotify."));
}

bool NowPlaying::spotifyPlaysForOctave() const
{
    if (!m_spotify)
        return false;
    // The phone's own report: a poll may be seconds behind it
    return m_source == Source::Spotify ? m_spotify->playing() : m_source == Source::Phone && phoneRunsSpotify() && m_playing;
}

void NowPlaying::pauseSpotifyBeforeExit()
{
    if (!spotifyPlaysForOctave())
        return;
    // Over the mirror too: it reaches the phone with no internet
    if (m_source == Source::Phone)
        m_phoneMirror->mediaPause();
    m_spotify->pauseBeforeExit(kExitPauseMs);
}

void NowPlaying::readSpotify()
{
    setTrack(m_spotify->hasPlayback(), m_spotify->title(), m_spotify->artist(), m_spotify->album());
    setDuration(m_spotify->durationMs());
}

bool NowPlaying::albumArtCapture() const
{
    return m_settings && m_settings->themeSetting() == QLatin1String("Album Art Capture");
}

void NowPlaying::extractArtColors()
{
    if (!m_media)
        return;
    if (m_source != Source::Local) {
        if (!m_artUrl.isEmpty())
            m_media->extract_colors_from_image(m_artUrl);
    } else if (!m_localFile.isEmpty()) {
        m_media->extract_colors_from_album_art(m_localFile);
    }
}

// ── source ─────────────────────────────────────────────────────────────

QString NowPlaying::source() const
{
    return sourceName(m_source);
}

void NowPlaying::switchTo(Source s)
{
    if (s == m_source)
        return;
    const Source old = m_source;
    // The new owner first: the old one's reaction to being stopped (the
    // phone's mediaActiveChanged(false), the local pause) must see it
    m_source = s;
    qCInfo(lcNowPlaying) << "music source" << sourceName(old) << "->" << sourceName(s);
    if (s != Source::AndroidAuto)
        m_resumeAa = false;
    m_resumeLocalAfterMic = false;
    switch (old) {
    case Source::Local:
        if (m_media && m_media->is_playing())
            m_media->pause();
        break;
    case Source::Phone:
        if (m_phoneMirror)
            m_phoneMirror->releaseMedia(true);
        break;
    case Source::AndroidAuto:
        if (m_aa)
            m_aa->revokeFocus();
        break;
    case Source::Spotify:
        // Not when the phone takes over from itself: the music Spotify just
        // started on the mirrored phone must not be paused by the hand-off
        if (m_spotify && m_spotify->playing() && !(s == Source::Phone && phoneRunsSpotify()))
            m_spotify->pause();
        break;
    }
    readSource();
    emit sourceChanged();
    emit capabilitiesChanged();   // canShuffle follows the source
    emit trackChanged();
    remember();
}

void NowPlaying::readSource()
{
    switch (m_source) {
    case Source::Phone:
        readPhoneTrack();
        readPhoneState();
        setArtUrl(m_phoneMirror->mediaArtUrl());
        break;
    case Source::AndroidAuto:
        readAaTrack();
        setArtUrl(m_aa->artUrl());
        setPlaying(m_aa->playing());
        setPosition(m_aa->positionMs());
        setCanSeek(false);
        break;
    case Source::Spotify:
        readSpotify();
        setArtUrl(m_spotify->artUrl());
        setPlaying(m_spotify->playing());
        setPosition(m_spotify->positionMs());
        setCanSeek(m_spotify->hasPlayback());
        setShuffled(m_spotify->shuffle());
        break;
    case Source::Local:
        readLocalTrack();
        if (m_media) {
            setPlaying(m_media->is_playing());
            setDuration(int(m_media->get_duration()));
            setPosition(int(m_media->get_position()));
        } else {
            setPlaying(false);
            setDuration(0);
            setPosition(0);
        }
        setCanSeek(m_media && !m_localFile.isEmpty());
        if (m_media)
            setShuffled(m_media->is_shuffled());
        break;
    }
    updateTick();
}

void NowPlaying::onPhoneActiveChanged(bool active)
{
    // PhoneMirrorManager decides when the phone takes the controls
    if (active)
        switchTo(Source::Phone);
    else if (m_source == Source::Phone)
        switchTo(Source::Local);
}

void NowPlaying::onLocalPlayStateChanged(bool playing)
{
    // Local playback starting is the user's: it takes the music
    if (playing) {
        m_resumeAa = false;
        switchTo(Source::Local);
    }
    if (m_source == Source::Local) {
        setPlaying(playing);
        remember();
    }
}

void NowPlaying::onAaFocusRequested()
{
    QString why;
    if (m_source == Source::AndroidAuto)
        why = QStringLiteral("it owns the music");
    else if (m_aa->recentUserIntent())
        why = QStringLiteral("the user asked for it");
    else if (m_resumeAa)
        why = QStringLiteral("it played when OCTAVE last stopped");
    if (why.isEmpty()) {
        qCInfo(lcNowPlaying) << "Android Auto wants to play: refused," << source() << "owns the music";
        m_aa->denyFocus();
        return;
    }
    qCInfo(lcNowPlaying) << "Android Auto wants to play: granted," << why;
    switchTo(Source::AndroidAuto);
    m_aa->grantFocus();
}

void NowPlaying::onAaConnectedChanged()
{
    if (m_source == Source::AndroidAuto) {
        setPlaying(m_aa->playing());
        updateTick();
    }
    if (!m_aa->connected() || !m_resumeAa)
        return;
    // Resume after a restart: the phone usually starts its music by itself
    // (the focus request is granted above); if it doesn't, press play
    QTimer::singleShot(kResumeKeyDelayMs, this, [this] {
        if (!m_resumeAa || !m_aa->connected() || m_aa->playing())
            return;
        qCInfo(lcNowPlaying) << "resuming Android Auto";
        m_aa->playPause();
    });
}

void NowPlaying::onAaMicrophone(bool open)
{
    // A voice request interrupts local music; it comes back afterwards
    // unless the request started something else
    if (open) {
        if (m_source == Source::Local && m_media && m_media->is_playing()) {
            m_media->pause();
            m_resumeLocalAfterMic = true;
        }
    } else if (m_resumeLocalAfterMic) {
        m_resumeLocalAfterMic = false;
        if (m_source == Source::Local && m_media && !m_media->is_playing())
            m_media->toggle_play();
    }
}

void NowPlaying::stopSaving()
{
    m_saving = false;
}

void NowPlaying::remember()
{
    if (!m_settings || !m_saving)
        return;
    const QString src = source();
    if (m_settings->get_setting(QStringLiteral("mediaLastSource")).toString() != src)
        m_settings->save_setting(QStringLiteral("mediaLastSource"), src);
    // While a resume is pending, Android Auto not playing yet is no news
    if (m_resumeAa && !m_playing)
        return;
    if (m_settings->get_setting(QStringLiteral("mediaLastPlaying")).toBool() != m_playing)
        m_settings->save_setting(QStringLiteral("mediaLastPlaying"), m_playing);
}

void NowPlaying::readLocalTrack()
{
    if (!m_media || m_localFile.isEmpty()) {
        setTrack(false, {}, {}, {});
        setArtUrl({});
        setCanSeek(false);
        return;
    }
    setTrack(true, m_media->get_display_name(m_localFile), m_media->get_band(m_localFile),
             m_media->get_album(m_localFile));
    setArtUrl(m_media->get_album_art(m_localFile));
    setCanSeek(true);
}

void NowPlaying::readPhoneTrack()
{
    const bool has = m_phoneMirror->mediaAvailable();
    setTrack(has, m_phoneMirror->mediaTitle(), m_phoneMirror->mediaArtist(), m_phoneMirror->mediaAlbum());
}

void NowPlaying::readPhoneState()
{
    // Every report: play/pause, a seek, metadata. The position is
    // authoritative right after one.
    readPhoneTrack();
    setPlaying(m_phoneMirror->mediaPlaying());
    setDuration(m_phoneMirror->mediaDuration());
    setCanSeek(m_phoneMirror->mediaCanSeek());
    setPosition(m_phoneMirror->mediaPosition());
    updateTick();
}

void NowPlaying::readAaTrack()
{
    setTrack(m_aa->hasTrack(), m_aa->title(), m_aa->artist(), m_aa->album());
    setDuration(m_aa->durationMs());
}

void NowPlaying::tick()
{
    if (m_source == Source::Phone)
        setPosition(m_phoneMirror->mediaPosition());
    else if (m_source == Source::AndroidAuto)
        setPosition(m_aa->positionMs());
    else if (m_source == Source::Spotify)
        setPosition(m_spotify->positionMs());
}

void NowPlaying::updateTick()
{
    const bool run = m_source != Source::Local && m_playing;
    if (run && !m_tick.isActive())
        m_tick.start();
    else if (!run)
        m_tick.stop();
}

// ── controls ───────────────────────────────────────────────────────────

void NowPlaying::togglePlay()
{
    switch (m_source) {
    case Source::Spotify:
        if (m_spotify->signedIn()) {
            m_spotify->togglePlay();
            return;
        }
        break;
    case Source::Phone:
        m_phoneMirror->mediaPlayPause();
        return;
    case Source::AndroidAuto:
        if (m_aa->connected()) {
            m_aa->playPause();
            return;
        }
        // Android Auto is gone: play goes to OCTAVE's own player
        break;
    case Source::Local:
        break;
    }
    if (m_media)
        m_media->toggle_play();
}

void NowPlaying::next()
{
    switch (m_source) {
    case Source::Spotify:
        if (m_spotify->signedIn()) {
            m_spotify->next();
            return;
        }
        break;
    case Source::Phone:
        m_phoneMirror->mediaNext();
        return;
    case Source::AndroidAuto:
        if (m_aa->connected()) {
            m_aa->next();
            return;
        }
        break;
    case Source::Local:
        break;
    }
    if (m_media)
        m_media->next_track();
}

void NowPlaying::previous()
{
    switch (m_source) {
    case Source::Spotify:
        if (m_spotify->signedIn()) {
            m_spotify->previous();
            return;
        }
        break;
    case Source::Phone:
        m_phoneMirror->mediaPrevious();
        return;
    case Source::AndroidAuto:
        if (m_aa->connected()) {
            m_aa->previous();
            return;
        }
        break;
    case Source::Local:
        break;
    }
    if (m_media)
        m_media->previous_track();
}

void NowPlaying::seek(int positionMs)
{
    if (!m_canSeek)
        return;
    if (m_source == Source::Phone)
        m_phoneMirror->mediaSeek(positionMs);
    else if (m_source == Source::Spotify)
        m_spotify->seek(positionMs);
    else if (m_source == Source::Local && m_media)
        m_media->set_position(positionMs);
    setPosition(positionMs);   // the source confirms with its next report
}

void NowPlaying::toggleShuffle()
{
    if (m_source == Source::Local && m_media)
        m_media->toggle_shuffle();
    else if (m_source == Source::Spotify)
        m_spotify->toggleShuffle();
}

void NowPlaying::useLocal()
{
    switchTo(Source::Local);
}

void NowPlaying::useSpotify()
{
    if (m_spotify && m_spotify->signedIn())
        switchTo(Source::Spotify);
}

// ── setters ────────────────────────────────────────────────────────────

void NowPlaying::setPlaying(bool v)
{
    if (v == m_playing)
        return;
    m_playing = v;
    emit playingChanged();
    updateTick();
}

void NowPlaying::setTrack(bool has, const QString &title, const QString &artist, const QString &album)
{
    if (has == m_hasTrack && title == m_title && artist == m_artist && album == m_album)
        return;
    m_hasTrack = has;
    m_title = title;
    m_artist = artist;
    m_album = album;
    emit trackInfoChanged();
}

void NowPlaying::setArtUrl(const QString &url)
{
    if (url == m_artUrl)
        return;
    m_artUrl = url;
    ++m_artRevision;
    emit artUrlChanged();
}

void NowPlaying::setDuration(int ms)
{
    ms = qMax(0, ms);
    if (ms == m_durationMs)
        return;
    m_durationMs = ms;
    emit durationChanged();
}

void NowPlaying::setPosition(int ms)
{
    ms = qMax(0, ms);
    if (ms == m_positionMs)
        return;
    m_positionMs = ms;
    emit positionChanged();
}

void NowPlaying::setCanSeek(bool v)
{
    if (v == m_canSeek)
        return;
    m_canSeek = v;
    emit capabilitiesChanged();
}

void NowPlaying::setShuffled(bool v)
{
    if (v == m_shuffled)
        return;
    m_shuffled = v;
    emit shuffledChanged();
}
