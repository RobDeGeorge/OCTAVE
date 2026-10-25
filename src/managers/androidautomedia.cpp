#include "androidautomedia.h"
#include "settingsmanager.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QUrl>

Q_LOGGING_CATEGORY(lcAaMedia, "octave.androidauto.media")

namespace {
// Aap::Focus and Aap::Key (src/android_auto/aapproto.h, only built with
// native Android Auto); the values are the protocol's
constexpr int kRequestGain = 1, kRequestRelease = 4;
constexpr int kFocusGain = 1, kFocusLoss = 3;
constexpr int kKeyPlayPause = 85, kKeyNext = 87, kKeyPrevious = 88;
constexpr int kStatePlaying = 2;
}

AndroidAutoMedia::AndroidAutoMedia(QObject *parent)
    : QObject(parent)
{
}

int AndroidAutoMedia::positionMs() const
{
    int ms = m_reportedPositionMs;
    if (m_playing && m_sinceReport.isValid())
        ms += int(m_sinceReport.elapsed());
    return m_durationMs > 0 ? qMin(ms, m_durationMs) : ms;
}

void AndroidAutoMedia::noteUserIntent()
{
    m_sinceIntent.start();
}

bool AndroidAutoMedia::recentUserIntent() const
{
    return m_sinceIntent.isValid() && m_sinceIntent.elapsed() < kIntentWindowMs;
}

qint64 AndroidAutoMedia::msSinceConnect() const
{
    return m_connected && m_sinceConnect.isValid() ? m_sinceConnect.elapsed() : -1;
}

void AndroidAutoMedia::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    m_focus = false;
    m_pendingGain = false;
    if (connected) {
        m_sinceConnect.start();
    } else {
        // The track stays on show (paused) until something else plays
        setPlaying(false);
        if (m_micOpen)
            setMicrophoneOpen(false);
        setGuidancePlaying(false);
        setSystemSoundPlaying(false);
    }
    emit connectedChanged();
}

void AndroidAutoMedia::setPlaybackStatus(int state, int positionS, const QString &app)
{
    m_app = app;
    m_reportedPositionMs = qMax(0, positionS) * 1000;
    m_sinceReport.start();
    setPlaying(state == kStatePlaying);
    emit positionReported();
}

void AndroidAutoMedia::setMetadata(const QString &title, const QString &artist, const QString &album,
                                   const QByteArray &art, int durationS)
{
    const int durationMs = qMax(0, durationS) * 1000;
    if (title != m_title || artist != m_artist || album != m_album || durationMs != m_durationMs) {
        m_title = title;
        m_artist = artist;
        m_album = album;
        m_durationMs = durationMs;
        emit trackChanged();
    }
    QString url;
    if (!art.isEmpty()) {
        const QString dir = SettingsManager::getAppDataDir() + QStringLiteral("/temp");
        QDir().mkpath(dir);
        // Alternate between two files: a new URL makes QML reload the image
        m_artSeq = (m_artSeq + 1) % 2;
        const QString path = dir + QStringLiteral("/androidauto_media_art_%1").arg(m_artSeq);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly) && f.write(art) == art.size())
            url = QUrl::fromLocalFile(path).toString();
        else
            qCWarning(lcAaMedia) << "Could not write Android Auto album art to" << path;
    }
    if (url != m_artUrl) {
        m_artUrl = url;
        emit artChanged();
    }
}

void AndroidAutoMedia::focusRequested(int requestType)
{
    if (requestType == kRequestGain) {
        m_pendingGain = true;
        emit mediaFocusRequested();
        // Nobody decided (no NowPlaying): Android Auto plays
        if (m_pendingGain)
            grantFocus();
    } else if (requestType == kRequestRelease) {
        m_focus = false;
        emit mediaFocusReleased();
    }
}

void AndroidAutoMedia::grantFocus()
{
    if (!m_pendingGain)
        return;
    m_pendingGain = false;
    m_focus = true;
    emit focusAnswer(kFocusGain);
}

void AndroidAutoMedia::denyFocus()
{
    if (!m_pendingGain)
        return;
    m_pendingGain = false;
    m_focus = false;
    qCInfo(lcAaMedia) << "Android Auto media refused: another source has the music";
    // Answering LOSS makes Android Auto take focus on the phone for the car
    // and ask again at once, ~50 times a second (Android Auto 17.9). Grant,
    // then take it back unsolicited, as a car radio does: the phone pauses.
    emit focusAnswer(kFocusGain);
    emit focusChange(kFocusLoss);
}

void AndroidAutoMedia::revokeFocus()
{
    if (!m_connected || !m_focus)
        return;
    m_focus = false;
    emit focusChange(kFocusLoss);
}

void AndroidAutoMedia::setMicrophoneOpen(bool open)
{
    if (open == m_micOpen)
        return;
    m_micOpen = open;
    // A voice request ("play some music") is the user's
    noteUserIntent();
    emit microphoneOpenChanged(open);
}

void AndroidAutoMedia::setGuidancePlaying(bool on)
{
    const bool before = interruptsLocal();
    m_guidance = on;
    if (interruptsLocal() != before)
        emit interruptsLocalChanged(interruptsLocal());
}

void AndroidAutoMedia::setSystemSoundPlaying(bool on)
{
    const bool before = interruptsLocal();
    m_system = on;
    if (interruptsLocal() != before)
        emit interruptsLocalChanged(interruptsLocal());
}

void AndroidAutoMedia::playPause()
{
    if (!m_connected)
        return;
    noteUserIntent();
    emit keyPress(kKeyPlayPause);
}

void AndroidAutoMedia::next()
{
    if (!m_connected)
        return;
    noteUserIntent();
    emit keyPress(kKeyNext);
}

void AndroidAutoMedia::previous()
{
    if (!m_connected)
        return;
    noteUserIntent();
    emit keyPress(kKeyPrevious);
}

void AndroidAutoMedia::setPlaying(bool v)
{
    if (v == m_playing)
        return;
    m_playing = v;
    emit playingChanged();
}
