#ifndef ADAPTIVEBITRATE_H
#define ADAPTIVEBITRATE_H

// Adaptive video bit rate for phone mirroring (setting scrcpyBitRateAuto).
// Mirrors backend/phone_mirror/adaptive_bitrate.py; the tests in
// tests/test_adaptive_bitrate.py exercise the Python twin, keep them in step.
//
// Every video packet carries the phone's presentation time. Arrival time on
// our clock minus that is the one-way delay plus an unknown clock offset; the
// smallest delay seen over the last kBaselineWindows seconds stands in for
// "link idle", so anything above it is queueing: the link (Wi-Fi, adb over
// USB) or our decoder is not keeping up. Each one-second window is judged on
// its mean queueing delay (the single worst packet left out, so a key frame
// does not count as congestion):
//   - over kCongestedMs: cut the rate by kDecrease (at most every kDropGapUs)
//   - under kClearMs for kGoodWindowsToRaise windows in a row, kHoldAfterDropUs
//     after the last cut, and the stream actually using at least
//     kAppLimitedFraction of the rate (a still screen proves nothing):
//     raise it by kIncreaseFraction, at least kIncreaseMinBps
// The result stays within [min, max]; max is the user's bit-rate setting.

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

class AdaptiveBitRate
{
public:
    static constexpr qint64 kWindowUs = 1000000;
    static constexpr int kBaselineWindows = 10;
    static constexpr qint64 kWarmupUs = 2000000;
    static constexpr qint64 kCongestedUs = 150000;
    static constexpr qint64 kClearUs = 40000;
    static constexpr qint64 kDiscontinuityUs = 3000000;   // a clock jump, not a queue
    static constexpr double kDecrease = 0.7;
    static constexpr double kIncreaseFraction = 0.1;
    static constexpr int kIncreaseMinBps = 1000000;
    static constexpr int kGoodWindowsToRaise = 2;
    static constexpr qint64 kHoldAfterDropUs = 5000000;
    static constexpr qint64 kDropGapUs = 2000000;
    static constexpr double kAppLimitedFraction = 0.5;
    static constexpr double kMinChangeFraction = 0.05;

    AdaptiveBitRate(int minBps, int maxBps, int startBps)
        : m_min(minBps), m_max(std::max(minBps, maxBps)),
          m_target(std::clamp(startBps, m_min, m_max)) {}

    int target() const { return m_target; }
    // Last mean queueing delay judged, for logging
    qint64 lastQueueUs() const { return m_lastQueueUs; }

    // Feed every media packet: phone presentation time and local arrival time
    // (both µs, any origin), payload size. Returns the new rate in bps when it
    // changed enough to send to the phone, 0 otherwise.
    int onPacket(qint64 ptsUs, qint64 arrivalUs, int bytes)
    {
        const qint64 delay = arrivalUs - ptsUs;
        if (m_startUs < 0) {
            m_startUs = arrivalUs;
            m_windowStartUs = arrivalUs;
            m_lastDropUs = std::numeric_limits<qint64>::min() / 2;
        }
        m_windowMin = std::min(m_windowMin, delay);
        qint64 baseline = m_windowMin;
        for (qint64 m : m_history)
            baseline = std::min(baseline, m);
        qint64 queue = delay - baseline;
        if (queue > kDiscontinuityUs) {
            // The phone's clock (or ours) jumped: start the baseline over
            m_history.clear();
            m_windowMin = delay;
            queue = 0;
        }
        m_sum += queue;
        m_worst = std::max(m_worst, queue);
        ++m_count;
        m_bytes += bytes;
        if (arrivalUs - m_windowStartUs < kWindowUs)
            return 0;
        const int changed = evaluate(arrivalUs);
        m_history.push_back(m_windowMin);
        if (int(m_history.size()) > kBaselineWindows)
            m_history.pop_front();
        m_windowMin = std::numeric_limits<qint64>::max();
        m_windowStartUs = arrivalUs;
        m_sum = 0; m_worst = 0; m_count = 0; m_bytes = 0;
        return changed;
    }

    // The rate to announce once at the start of a stream (the phone may still
    // run a rate from an earlier session)
    int takeInitial()
    {
        m_sent = m_target;
        return m_target;
    }

private:
    int evaluate(qint64 nowUs)
    {
        if (nowUs - m_startUs < kWarmupUs || m_count == 0)
            return 0;
        const bool dropWorst = m_count >= 3;
        const qint64 meanQueue = (m_sum - (dropWorst ? m_worst : 0)) / (m_count - (dropWorst ? 1 : 0));
        m_lastQueueUs = meanQueue;
        const double seconds = double(nowUs - m_windowStartUs) / 1e6;
        const double usedBps = seconds > 0 ? m_bytes * 8.0 / seconds : 0.0;
        if (meanQueue > kCongestedUs) {
            m_good = 0;
            if (nowUs - m_lastDropUs >= kDropGapUs) {
                m_target = std::max(m_min, int(m_target * kDecrease));
                m_lastDropUs = nowUs;
            }
        } else if (meanQueue < kClearUs) {
            if (++m_good >= kGoodWindowsToRaise && nowUs - m_lastDropUs >= kHoldAfterDropUs
                && usedBps >= kAppLimitedFraction * m_target) {
                m_target = std::min(m_max, m_target + std::max(kIncreaseMinBps, int(m_target * kIncreaseFraction)));
                m_good = 0;
            }
        } else {
            m_good = 0;
        }
        if (m_target == m_sent)
            return 0;
        const bool atBound = m_target == m_min || m_target == m_max;
        if (!atBound && std::abs(m_target - m_sent) < kMinChangeFraction * m_sent)
            return 0;
        m_sent = m_target;
        return m_target;
    }

    int m_min;
    int m_max;
    int m_target;
    int m_sent = 0;
    qint64 m_startUs = -1;
    qint64 m_windowStartUs = 0;
    qint64 m_windowMin = std::numeric_limits<qint64>::max();
    std::deque<qint64> m_history;   // per-window minimum delay
    qint64 m_sum = 0;
    qint64 m_worst = 0;
    int m_count = 0;
    qint64 m_bytes = 0;
    int m_good = 0;
    qint64 m_lastDropUs = 0;
    qint64 m_lastQueueUs = 0;
};

#endif // ADAPTIVEBITRATE_H
