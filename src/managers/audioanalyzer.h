#ifndef AUDIOANALYZER_H
#define AUDIOANALYZER_H

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QByteArray>
#include <QMap>
#include <QVector>
#include <QFuture>
#include <QFutureWatcher>

#include <vector>
#include <complex>
#include <cmath>

class AudioAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit AudioAnalyzer(QObject *parent = nullptr);
    ~AudioAnalyzer() override;

signals:
    void fftDataChanged(const QVariantList &levels);
    void analysisComplete();
    void analysisStarted();

public slots:
    void analyze_file(const QString &filePath);
    void update_position(float positionSeconds);
    void set_active(bool active);
    QVariantList get_current_levels() const;
    int get_num_bars() const;
    bool is_analyzed() const;
    void clear();

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
};

#endif // AUDIOANALYZER_H
