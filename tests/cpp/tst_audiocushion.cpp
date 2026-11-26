// The phone-audio cushion (src/phone_mirror/audiocushion.h): how much sound
// is kept ahead of the speaker, and when late sound is dropped.

#include <QTest>

#include "phone_mirror/audiocushion.h"

class TstAudioCushion : public QObject
{
    Q_OBJECT

private slots:
    void holdsBackUntilTheTargetIsQueued()
    {
        AudioCushion c;
        QCOMPARE(c.targetMs(), AudioCushion::kStartMs);
        QVERIFY(!c.step(0, 20, 0, true).write);
        QVERIFY(!c.step(100, 120, 0, true).write);
        const auto s = c.step(180, 200, 0, true);
        QVERIFY(s.write);
        QCOMPARE(s.dropMs, 0);
        c.wrote(180);
        // Steady: the device holds the cushion, each chunk is written as it comes
        QVERIFY(c.step(200, 20, 190, false).write);
        QCOMPARE(c.underruns(), 0);
    }

    void aTrickleIsPlayedAnyway()
    {
        // Less than the target ever arrives (the last notes before a pause)
        AudioCushion c;
        QVERIFY(!c.step(0, 40, 0, true).write);
        QVERIFY(!c.step(500, 60, 0, true).write);
        QVERIFY(c.step(AudioCushion::kStartMs + AudioCushion::kPrimeGiveUpMs, 80, 0, true).write);
    }

    void runningDryDoublesTheCushionUpToTheMaximum()
    {
        AudioCushion c;
        qint64 now = 0;
        auto play = [&] {   // fill, start playing
            QVERIFY(c.step(now, c.targetMs(), 0, true).write);
            c.wrote(now);
        };
        play();
        int expected = AudioCushion::kStartMs;
        for (int i = 1; i <= 4; ++i) {
            now += 400;   // the link stalled: the device ran dry, then a little arrived
            const auto s = c.step(now, 20, 0, true);
            expected = std::min(AudioCushion::kMaxMs, expected * 2);
            QCOMPARE(c.targetMs(), expected);
            QCOMPARE(c.underruns(), i);
            QCOMPARE(s.grew, i <= 2);   // 400, 800, then it stays
            QVERIFY(!s.write);          // held back until the new target is there
            QVERIFY(c.priming());
            now += 50;
            play();
        }
        QCOMPARE(c.targetMs(), AudioCushion::kMaxMs);
    }

    void aLongSilenceIsNotJitter()
    {
        // The link dropped (or the phone stopped sending) for seconds: the
        // cushion is refilled, not grown
        AudioCushion c;
        QVERIFY(c.step(0, 200, 0, true).write);
        c.wrote(0);
        const auto s = c.step(AudioCushion::kPauseMs + 500, 20, 0, true);
        QVERIFY(!s.write);
        QVERIFY(!s.grew);
        QCOMPARE(c.underruns(), 0);
        QCOMPARE(c.targetMs(), AudioCushion::kStartMs);
    }

    void aLateBurstIsKeptUpToTheSlackThenTheOldestGoes()
    {
        AudioCushion c;
        QVERIFY(c.step(0, 200, 0, true).write);
        c.wrote(0);
        // 500 ms came at once on top of a full device: kept (700 <= 200 + 600)
        auto s = c.step(20, 500, 200, false);
        QVERIFY(s.write);
        QCOMPARE(s.dropMs, 0);
        // 900 ms on top: back down to the target
        s = c.step(40, 900, 200, false);
        QVERIFY(s.write);
        QCOMPARE(s.dropMs, 900);
        QCOMPARE(c.droppedMs(), 900);
        // Never more than is queued on our side: the device's part cannot be taken back
        s = c.step(60, 100, 900, false);
        QCOMPARE(s.dropMs, 100);
    }

    void aReopenedDeviceIsNotAnUnderrun()
    {
        AudioCushion c;
        QVERIFY(c.step(0, 200, 0, true).write);
        c.wrote(0);
        c.prime();   // another output was picked
        QVERIFY(!c.step(100, 20, 0, true).write);
        QCOMPARE(c.underruns(), 0);
        QCOMPARE(c.targetMs(), AudioCushion::kStartMs);
    }

    void aLearnedTargetCarriesToTheNextSession()
    {
        AudioCushion c;
        c.setTargetMs(400);
        QCOMPARE(c.targetMs(), 400);
        QVERIFY(!c.step(0, 300, 0, true).write);
        QVERIFY(c.step(10, 400, 0, true).write);
        c.setTargetMs(5000);
        QCOMPARE(c.targetMs(), AudioCushion::kMaxMs);
        c.setTargetMs(0);
        QCOMPARE(c.targetMs(), AudioCushion::kStartMs);
    }
};

QTEST_GUILESS_MAIN(TstAudioCushion)
#include "tst_audiocushion.moc"
