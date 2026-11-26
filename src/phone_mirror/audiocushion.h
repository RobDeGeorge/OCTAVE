#ifndef AUDIOCUSHION_H
#define AUDIOCUSHION_H

// How much phone audio is kept ahead of the speaker, for phone mirroring.
//
// The phone's sound arrives as PCM in real time over the same adb link as
// the picture (one TCP stream for all of it, so a burst of video delays the
// audio behind it), and is played from the GUI thread. Whatever is queued
// ahead of the speaker, the cushion, is all that covers a late packet or a
// busy GUI thread: when it runs out the music stops for a moment. A big
// cushion never runs out but puts the sound behind the picture, so it starts
// small and grows only where the link shows it is needed:
//   - audio is held back until targetMs is queued (at the start, and again
//     after the output ran dry), then written;
//   - each time the output runs dry mid-stream the target doubles, up to
//     kMaxMs (a gap of kPauseMs or more is the phone going quiet or the link
//     dropping, not jitter, and changes nothing);
//   - it never shrinks during a session: taking latency back out means
//     cutting sound;
//   - late audio that arrives in a burst is kept, up to kSlackMs over the
//     target; beyond that the oldest is dropped so the sound cannot fall
//     further and further behind.
// The device buffer is opened at targetMs (scrcpyclient.cpp), since
// PulseAudio and PipeWire wait for a full buffer before they play.
//
// Pure rules, no Qt objects: tests/cpp/tst_audiocushion.cpp.

#include <QtGlobal>
#include <algorithm>

class AudioCushion
{
public:
    static constexpr int kStartMs = 200;
    static constexpr int kMaxMs = 800;
    static constexpr int kSlackMs = 600;
    static constexpr int kDryMs = 10;          // this little left in the device is "ran dry"
    static constexpr qint64 kPauseMs = 2000;
    static constexpr int kPrimeGiveUpMs = 400; // a trickle is played anyway after target + this

    struct Step {
        bool write = false;   // write what is queued (false: keep holding it back)
        int dropMs = 0;       // first drop this much of the oldest queued audio
        bool grew = false;    // the target grew: reopen the device at targetMs()
    };

    int targetMs() const { return m_targetMs; }
    void setTargetMs(int ms) { m_targetMs = std::clamp(ms, kStartMs, kMaxMs); }
    int underruns() const { return m_underruns; }
    qint64 droppedMs() const { return m_droppedMs; }
    bool priming() const { return m_priming; }

    // The device was (re)opened, or the stream starts: hold back again
    void prime()
    {
        m_priming = true;
        m_primingSinceMs = -1;
        m_lastWriteMs = -1;
    }

    // Before writing: queuedMs waits on our side, deviceMs is still in the
    // output device, deviceIdle is the device saying it has nothing to play
    Step step(qint64 nowMs, int queuedMs, int deviceMs, bool deviceIdle)
    {
        Step s;
        if (!m_priming && m_lastWriteMs >= 0 && (deviceIdle || deviceMs <= kDryMs)) {
            if (nowMs - m_lastWriteMs < kPauseMs) {
                ++m_underruns;
                const int grown = std::min(kMaxMs, m_targetMs * 2);
                s.grew = grown != m_targetMs;
                m_targetMs = grown;
            }
            m_priming = true;
            m_primingSinceMs = -1;
            if (s.grew)
                deviceMs = 0;   // the device is reopened empty
        }
        if (m_priming) {
            if (m_primingSinceMs < 0)
                m_primingSinceMs = nowMs;
            if (queuedMs + deviceMs < m_targetMs && nowMs - m_primingSinceMs < m_targetMs + kPrimeGiveUpMs)
                return s;
            m_priming = false;
        }
        const int total = queuedMs + deviceMs;
        if (total > m_targetMs + kSlackMs) {
            s.dropMs = std::min(queuedMs, total - m_targetMs);
            m_droppedMs += s.dropMs;
        }
        s.write = true;
        return s;
    }

    void wrote(qint64 nowMs) { m_lastWriteMs = nowMs; }

private:
    int m_targetMs = kStartMs;
    bool m_priming = true;
    qint64 m_primingSinceMs = -1;
    qint64 m_lastWriteMs = -1;
    int m_underruns = 0;
    qint64 m_droppedMs = 0;
};

#endif // AUDIOCUSHION_H
