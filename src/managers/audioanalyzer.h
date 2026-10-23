#ifndef AUDIOANALYZER_H
#define AUDIOANALYZER_H

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QFuture>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QTimer>

#include <vector>
#include <complex>
#include <cmath>

// Waveform visualizer bars: m_numBars levels (0-8) while shown and playing.
// Local tracks are pre-analysed (decoded off-thread, one frame per 100 ms)
// and read at each playback position report (MediaManager's 100 ms tick, so
// at most 10 changes a second); phone audio is analysed live from the PCM
// OCTAVE plays, every kLiveFrameMs (the window is 100 ms). main.cpp feeds it
// what is playing (NowPlaying's source and play state, MediaManager's track
// and position); a frontend only says whether its bars are on screen
// (setDemand(consumer, wanted); the QML UI's setViewActive is the consumer
// "view") and draws `levels`. Nothing runs, and no file is decoded, while no
// consumer wants the bars. Over the service a consumer is released when its
// connection closes.
class AudioAnalyzer : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "media")
    Q_CLASSINFO("octave.scope.read", "get_current_levels get_num_bars is_analyzed setDemand")
    Q_CLASSINFO("octave.scope.full", "analyze_file feed_live_pcm setLocalFile update_live update_position")
    Q_PROPERTY(QVariantList levels READ get_current_levels NOTIFY fftDataChanged)
    Q_PROPERTY(int numBars READ get_num_bars CONSTANT)

public:
    static constexpr int kLiveFrameMs = 50;

    explicit AudioAnalyzer(QObject *parent = nullptr);
    ~AudioAnalyzer() override;


signals:
    void fftDataChanged(const QVariantList &levels);
    void analysisComplete();
    void analysisStarted();

public slots:
    // A view showing the bars is on screen (and not mid-animation): the
    // QML UI's own consumer, setDemand("view", active)
    void setViewActive(bool active);
    // A frontend's bars are on screen (consumer = a stable name of its
    // own); the analysis runs while any consumer wants them
    void setDemand(const QString &consumer, bool wanted);
    // What is playing (main.cpp): local or phone, playing or not, and the
    // local track's full path ("" for none). A new track is analysed once a
    // view is active.
    void setPlaying(bool playing);
    void setLocalFile(const QString &filePath);
    // The local playback position (MediaManager::positionChanged)
    void setPosition(qint64 positionMs);

    void analyze_file(const QString &filePath);
    void update_position(float positionSeconds);
    void set_active(bool active);
    QVariantList get_current_levels() const;
    int get_num_bars() const;
    bool is_analyzed() const;
    void clear();

    // ── Live mode (phone mirror) ──────────────────────────────
    //    Bars from the PCM OCTAVE is playing right now instead of a
    //    pre-analysed file. update_live() runs every kLiveFrameMs while
    //    live and active, as update_position() runs per position report.
    void set_live(bool live);
    void update_live();
    // s16le 48 kHz stereo just written to the audio sink; bufferedMs is
    // how much the sink still holds ahead of it (what is not heard yet)
    void feed_live_pcm(const QByteArray &pcm, int bufferedMs);

public:
    // ── FFT helpers (no external deps) ─────────────────────────
    //    Public so the streaming analysis state in the .cpp can use them.
    static void fft_radix2(std::vector<std::complex<float>> &x);
    static std::vector<float> hannWindow(int N);

private:
    // ── Audio decoding + analysis ──────────────────────────────
    struct AnalysisResult {
        std::vector<quint8> fftData;    // numBars levels per chunk, flat
        bool success = false;
    };

    static AnalysisResult analyzeAudio(const QString &filePath, int numBars, double chunkDuration);

    void onAnalysisDone();

    // Driving: bars move while a view is active and something plays
    void refreshActive();
    bool               m_viewActive = false;   // any consumer in m_demand
    QSet<QString>      m_demand;
    bool               m_playing    = false;
    QString            m_localFile;
    qint64             m_positionMs = 0;
    QTimer             m_liveTimer;

    // ── Pre-computed FFT data ──────────────────────────────────
    //    Bar levels 0-8, m_numBars per time chunk (~100 ms), flat
    std::vector<quint8> m_fftData;
    size_t chunkCount() const { return m_fftData.size() / m_numBars; }

    QString      m_currentFile;
    int          m_numBars        = 96;
    double       m_chunkDuration  = 0.1;   // seconds

    QVariantList m_currentLevels;          // mirrors m_numBars zeros

    bool         m_isActive       = false;
    bool         m_analyzing      = false;

    // Latest file requested while a previous analysis was still running.
    // Processed once onAnalysisDone completes the current job so fast
    // "skip" sequences don't leave the visualizer stuck on the old track's
    // data (or blank if the old track was never analyzed).
    QString      m_pendingFile;

    // Async worker
    QFutureWatcher<AnalysisResult> m_watcher;

    // Live mode: decimated (~8 kHz) mono ring of the last second played,
    // analysed with the same window, FFT and bands as a file. GUI thread only.
    void resetLive();
    QVariantList zeroLevels() const;
    bool               m_live           = false;
    std::vector<float> m_liveRing;
    long long          m_liveTotal      = 0;   // decimated samples ever written
    int                m_livePhase      = 0;   // decimation phase (stereo frames)
    int                m_liveBufferedMs = 0;
    float              m_liveRef        = 0.0f;  // adaptive normalisation (95th pct, peak-held)
    QElapsedTimer      m_liveSinceFeed;
    QElapsedTimer      m_liveSinceUpdate;
    std::vector<float> m_liveWindow;
    std::vector<int>   m_liveEdges;
    std::vector<std::complex<float>> m_liveBuf;
    std::vector<float> m_liveBands;
};

#endif // AUDIOANALYZER_H
