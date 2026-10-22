#include "nowplaying.h"
#include "mediamanager.h"
#include "phonemirrormanager.h"

NowPlaying::NowPlaying(MediaManager *media, PhoneMirrorManager *phone, QObject *parent)
    : QObject(parent)
    , m_media(media)
    , m_phoneMirror(phone)
{
    m_phoneTick.setInterval(kPhoneTickMs);
    connect(&m_phoneTick, &QTimer::timeout, this, &NowPlaying::tickPhone);

    if (m_media) {
        connect(m_media, &MediaManager::currentMediaChanged, this, [this](const QString &file) {
            m_localFile = file;
            if (!m_phone) {
                readLocalTrack();
                emit trackChanged();
            }
        });
        // Tags and art can land after the track starts
        connect(m_media, &MediaManager::metadataChanged, this, [this] {
            if (!m_phone)
                readLocalTrack();
        });
        connect(m_media, &MediaManager::playStateChanged, this, [this](bool playing) {
            if (!m_phone)
                setPlaying(playing);
        });
        connect(m_media, &MediaManager::durationChanged, this, [this](qint64 ms) {
            if (!m_phone)
                setDuration(int(ms));
        });
        connect(m_media, &MediaManager::positionChanged, this, [this](qint64 ms) {
            if (!m_phone)
                setPosition(int(ms));
        });
        connect(m_media, &MediaManager::shuffleStateChanged, this, [this](bool on) {
            setShuffled(on);
        });
        m_localFile = m_media->get_current_file();
        m_shuffled = m_media->is_shuffled();
    }

    if (m_phoneMirror) {
        connect(m_phoneMirror, &PhoneMirrorManager::mediaActiveChanged, this, [this] { onSourceChanged(); });
        connect(m_phoneMirror, &PhoneMirrorManager::mediaChanged, this, [this] {
            if (m_phone)
                readPhoneState();
        });
        connect(m_phoneMirror, &PhoneMirrorManager::mediaTrackChanged, this, [this] {
            if (m_phone) {
                readPhoneTrack();
                emit trackChanged();
            }
        });
        connect(m_phoneMirror, &PhoneMirrorManager::mediaArtChanged, this, [this] {
            if (m_phone)
                setArtUrl(m_phoneMirror->mediaArtUrl());
        });
    }

    // Handoff: the phone taking the controls pauses OCTAVE's player; a local
    // track starting hands them back and pauses the phone. Connected after the
    // source connects above on purpose: Qt calls them in connection order, so
    // onSourceChanged has switched the source before these run.
    if (m_media && m_phoneMirror) {
        connect(m_phoneMirror, &PhoneMirrorManager::mediaActiveChanged, m_media, [this](bool active) {
            if (active && m_media->is_playing())
                m_media->pause();
        });
        connect(m_media, &MediaManager::playStateChanged, m_phoneMirror, [this](bool playing) {
            if (playing)
                m_phoneMirror->releaseMedia(true);
        });
    }

    onSourceChanged();
}

// ── source ─────────────────────────────────────────────────────────────

void NowPlaying::onSourceChanged()
{
    const bool phone = m_phoneMirror && m_phoneMirror->mediaActive();
    const bool switched = phone != m_phone;
    m_phone = phone;
    if (m_phone) {
        readPhoneTrack();
        readPhoneState();
        setArtUrl(m_phoneMirror->mediaArtUrl());
    } else {
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
    }
    updateTick();
    if (switched) {
        emit sourceChanged();
        emit capabilitiesChanged();   // canShuffle follows the source
        emit trackChanged();
    }
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

void NowPlaying::tickPhone()
{
    setPosition(m_phoneMirror->mediaPosition());
}

void NowPlaying::updateTick()
{
    const bool run = m_phone && m_playing;
    if (run && !m_phoneTick.isActive())
        m_phoneTick.start();
    else if (!run)
        m_phoneTick.stop();
}

// ── controls ───────────────────────────────────────────────────────────

void NowPlaying::togglePlay()
{
    if (m_phone)
        m_phoneMirror->mediaPlayPause();
    else if (m_media)
        m_media->toggle_play();
}

void NowPlaying::next()
{
    if (m_phone)
        m_phoneMirror->mediaNext();
    else if (m_media)
        m_media->next_track();
}

void NowPlaying::previous()
{
    if (m_phone)
        m_phoneMirror->mediaPrevious();
    else if (m_media)
        m_media->previous_track();
}

void NowPlaying::seek(int positionMs)
{
    if (!m_canSeek)
        return;
    if (m_phone)
        m_phoneMirror->mediaSeek(positionMs);
    else if (m_media)
        m_media->set_position(positionMs);
    setPosition(positionMs);   // the source confirms with its next report
}

void NowPlaying::toggleShuffle()
{
    if (!m_phone && m_media)
        m_media->toggle_shuffle();
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
