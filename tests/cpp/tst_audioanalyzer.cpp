// AudioAnalyzer drives the waveform visualizer itself: frames run only while
// a view is on screen and something plays, a local track is analysed once a
// view is up (after the page's own reaction to the track change), and phone
// audio is analysed live. Replaces MediaRoom's 16 ms timer, its analyze_file
// calls and its play-state syncs.

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

#include "managers/audioanalyzer.h"

namespace {
// 100 ms of a loud 1 kHz tone, s16le 48 kHz stereo (ScrcpyClient's format)
QByteArray tone()
{
    QByteArray pcm(4800 * 4, Qt::Uninitialized);
    auto *s = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < 4800; ++i)
        s[2 * i] = s[2 * i + 1] = qint16(12000 * std::sin(2 * M_PI * 1000 * i / 48000.0));
    return pcm;
}

// 2 s of a 440 Hz tone as a 48 kHz mono 16-bit WAV. (Not the 1 kHz one
// above: FFmpeg sniffs content too, and a loud 1 kHz WAV probes as another
// format.)
QByteArray wav()
{
    const int frames = 96000;
    QByteArray data(frames * 2, Qt::Uninitialized);
    auto *s = reinterpret_cast<qint16 *>(data.data());
    for (int i = 0; i < frames; ++i)
        s[i] = qint16(8000 * std::sin(2 * M_PI * 440 * i / 48000.0));
    QByteArray out;
    auto u32 = [&out](quint32 v) { out.append(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&out](quint16 v) { out.append(reinterpret_cast<const char *>(&v), 2); };
    out += "RIFF"; u32(36 + data.size()); out += "WAVEfmt ";
    u32(16); u16(1); u16(1); u32(48000); u32(96000); u16(2); u16(16);
    out += "data"; u32(data.size());
    return out + data;
}

bool anyNonZero(const QVariantList &levels)
{
    for (const QVariant &v : levels)
        if (v.toInt() > 0)
            return true;
    return false;
}
} // namespace

class TstAudioAnalyzer : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        m_file = m_tmp->path() + "/track.wav";
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(wav());
    }

    void liveFramesOnlyWhileShownAndPlaying()
    {
        AudioAnalyzer a;
        a.set_live(true);
        QSignalSpy levels(&a, &AudioAnalyzer::fftDataChanged);

        // Playing, but no view: nothing analysed
        a.setPlaying(true);
        for (int i = 0; i < 5; ++i) {
            a.feed_live_pcm(tone(), 0);
            QTest::qWait(AudioAnalyzer::kLiveFrameMs);
        }
        QVERIFY(!anyNonZero(a.get_current_levels()));

        // On screen: bars move
        a.setViewActive(true);
        QTRY_VERIFY_WITH_TIMEOUT((a.feed_live_pcm(tone(), 0), anyNonZero(a.get_current_levels())), 2000);

        // Paused: one flat frame, then nothing
        a.setPlaying(false);
        QVERIFY(!anyNonZero(a.get_current_levels()));
        levels.clear();
        a.feed_live_pcm(tone(), 0);
        QTest::qWait(5 * AudioAnalyzer::kLiveFrameMs);
        QCOMPARE(levels.count(), 0);
    }

    void hidingTheViewFlattensTheBars()
    {
        AudioAnalyzer a;
        a.set_live(true);
        a.setPlaying(true);
        a.setViewActive(true);
        QTRY_VERIFY_WITH_TIMEOUT((a.feed_live_pcm(tone(), 0), anyNonZero(a.get_current_levels())), 2000);
        a.setViewActive(false);
        QVERIFY(!anyNonZero(a.get_current_levels()));
        QCOMPARE(a.property("levels").toList(), a.get_current_levels());
        QCOMPARE(a.property("numBars").toInt(), 96);
    }

    void localTrackAnalysedOnceAViewIsUp()
    {
        AudioAnalyzer a;
        QSignalSpy started(&a, &AudioAnalyzer::analysisStarted);
        a.setLocalFile(m_file);
        QTest::qWait(50);
        QCOMPARE(started.count(), 0);   // nobody is looking: no decode

        a.setViewActive(true);
        QCOMPARE(started.count(), 1);
    }

    void trackChangeWaitsForThePagesHold()
    {
        // The page reacts to the same track change by holding the view (its
        // card animation): the decode must not start under it
        AudioAnalyzer a;
        a.setViewActive(true);
        QSignalSpy started(&a, &AudioAnalyzer::analysisStarted);
        a.setLocalFile(m_file);
        a.setViewActive(false);          // same event-loop pass
        QTest::qWait(50);
        QCOMPARE(started.count(), 0);
        a.setViewActive(true);           // settled
        QCOMPARE(started.count(), 1);
    }

    void localBarsFollowThePosition()
    {
        AudioAnalyzer a;
        QSignalSpy done(&a, &AudioAnalyzer::analysisComplete);
        a.setLocalFile(m_file);
        a.setViewActive(true);
        QVERIFY(done.wait(15000));
        a.setPlaying(true);
        a.setPosition(500);
        QVERIFY(anyNonZero(a.get_current_levels()));

        // Hidden: position reports change nothing
        a.setViewActive(false);
        QSignalSpy levels(&a, &AudioAnalyzer::fftDataChanged);
        a.setPosition(1500);
        QCOMPARE(levels.count(), 0);
        // Past the end of the track: flat
        a.setViewActive(true);
        a.setPosition(9000);
        QVERIFY(!anyNonZero(a.get_current_levels()));
    }

    void phoneSourceSkipsLocalAnalysis()
    {
        AudioAnalyzer a;
        a.set_live(true);
        a.setViewActive(true);
        QSignalSpy started(&a, &AudioAnalyzer::analysisStarted);
        a.setLocalFile(m_file);
        QTest::qWait(50);
        QCOMPARE(started.count(), 0);
        a.set_live(false);               // back to local: its bars
        QCOMPARE(started.count(), 1);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_file;
};

QTEST_MAIN(TstAudioAnalyzer)
#include "tst_audioanalyzer.moc"
