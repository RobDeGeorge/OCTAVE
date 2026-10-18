// Adaptive phone-mirror bit rate (src/phone_mirror/adaptivebitrate.h) against
// simulated packet streams. Ported from tests/test_adaptive_bitrate.py.

#include <QTest>

#include <functional>
#include <vector>

#include "phone_mirror/adaptivebitrate.h"

namespace {
constexpr int kMbps = 1000000;
constexpr int kFps = 60;
constexpr qint64 kFrameUs = 1000000 / kFps;
constexpr qint64 kOffsetUs = 123456789;   // clocks of phone and OCTAVE never agree

struct FeedResult {
    qint64 endUs;
    std::vector<int> sent;
};

// Packets at kFps for `seconds`, each sized to use `fill` of the current
// target, arriving `queueUs(t)` late.
FeedResult feed(AdaptiveBitRate &abr, double seconds, qint64 startUs = 0,
                const std::function<qint64(qint64)> &queueUs = [](qint64) { return qint64(0); },
                double fill = 1.0)
{
    FeedResult r{startUs, {}};
    const qint64 end = startUs + qint64(seconds * 1000000);
    while (r.endUs < end) {
        const int size = int(abr.target() * fill / 8 / kFps);
        const qint64 t = r.endUs;
        if (const int rate = abr.onPacket(t, t + kOffsetUs + 5000 + queueUs(t), size))
            r.sent.push_back(rate);
        r.endUs += kFrameUs;
    }
    return r;
}
} // namespace

class TstAdaptiveBitRate : public QObject
{
    Q_OBJECT

private slots:
    void clearLinkClimbsToMax()
    {
        AdaptiveBitRate abr(2 * kMbps, 16 * kMbps, 6 * kMbps);
        QCOMPARE(abr.takeInitial(), 6 * kMbps);
        const FeedResult r = feed(abr, 40);
        QCOMPARE(abr.target(), 16 * kMbps);
        QVERIFY(std::is_sorted(r.sent.begin(), r.sent.end()));   // only ever went up
        QVERIFY(!r.sent.empty());
        QCOMPARE(r.sent.back(), 16 * kMbps);
    }

    void nothingChangesDuringWarmup()
    {
        AdaptiveBitRate abr(2 * kMbps, 16 * kMbps, 8 * kMbps);
        abr.takeInitial();
        const FeedResult r = feed(abr, AdaptiveBitRate::kWarmupUs / 1e6 - 0.1, 0, [](qint64) { return qint64(500000); });
        QVERIFY(r.sent.empty());
    }

    void congestionCutsTheRateAndHoldsIt()
    {
        AdaptiveBitRate abr(2 * kMbps, 16 * kMbps, 16 * kMbps);
        abr.takeInitial();
        FeedResult r = feed(abr, 5);   // healthy baseline
        // Then a growing backlog: the link carries about half of what is sent
        const qint64 t0 = r.endUs;
        r = feed(abr, 6, t0, [t0](qint64 x) { return (x - t0) / 2; });
        QVERIFY(!r.sent.empty());
        int previous = 16 * kMbps;
        for (int rate : r.sent) {
            QVERIFY(rate < previous);
            previous = rate;
        }
        QVERIFY(abr.target() <= int(16 * kMbps * 0.7 * 0.7));
        const int low = abr.target();
        // Link recovers: no raise inside the hold window after the last cut
        feed(abr, 3, r.endUs);
        QCOMPARE(abr.target(), low);
    }

    void neverBelowMin()
    {
        AdaptiveBitRate abr(2 * kMbps, 16 * kMbps, 3 * kMbps);
        abr.takeInitial();
        const FeedResult r = feed(abr, 3);
        const qint64 t0 = r.endUs;
        feed(abr, 30, t0, [t0](qint64 x) { return (x - t0) / 2 % (AdaptiveBitRate::kDiscontinuityUs / 2); });
        QCOMPARE(abr.target(), 2 * kMbps);
    }

    void stillScreenDoesNotRaise()
    {
        // Tiny repeat frames: a clear link proves nothing about capacity
        AdaptiveBitRate abr(2 * kMbps, 16 * kMbps, 6 * kMbps);
        abr.takeInitial();
        const FeedResult r = feed(abr, 30, 0, [](qint64) { return qint64(0); }, 0.05);
        QVERIFY(r.sent.empty());
        QCOMPARE(abr.target(), 6 * kMbps);
    }

    void singleKeyFrameSpikeIsNotCongestion()
    {
        AdaptiveBitRate abr(2 * kMbps, 16 * kMbps, 16 * kMbps);
        abr.takeInitial();
        // One late packet per second (a key frame), the rest on time
        const FeedResult r = feed(abr, 20, 0, [](qint64 x) { return x % 1000000 < kFrameUs ? qint64(400000) : 0; });
        QVERIFY(r.sent.empty());
        QCOMPARE(abr.target(), 16 * kMbps);
    }

    void clockJumpResetsBaseline()
    {
        AdaptiveBitRate abr(2 * kMbps, 16 * kMbps, 16 * kMbps);
        abr.takeInitial();
        const FeedResult r = feed(abr, 5);
        // Phone clock steps back 10 s (delay jumps up): not a queue
        const FeedResult after = feed(abr, 10, r.endUs, [](qint64) { return qint64(10000000); });
        QVERIFY(after.sent.empty());
        QCOMPARE(abr.target(), 16 * kMbps);
    }
};

QTEST_GUILESS_MAIN(TstAdaptiveBitRate)
#include "tst_adaptivebitrate.moc"
