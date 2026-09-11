#include "audioanalyzer.h"

#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QLoggingCategory>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QAudioBuffer>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <numeric>
#include <cstring>

Q_LOGGING_CATEGORY(lcAudioAnalyzer, "octave.audioanalyzer")

// ════════════════════════════════════════════════════════════════
// Construction / destruction
// ════════════════════════════════════════════════════════════════

AudioAnalyzer::AudioAnalyzer(QObject *parent)
    : QObject(parent)
{
    // Initialise current levels to zeros
    m_currentLevels.reserve(m_numBars);
    for (int i = 0; i < m_numBars; ++i)
        m_currentLevels.append(0);

    connect(&m_watcher, &QFutureWatcher<AnalysisResult>::finished,
            this, &AudioAnalyzer::onAnalysisDone);
}

AudioAnalyzer::~AudioAnalyzer()
{
    m_watcher.cancel();
    m_watcher.waitForFinished();
}

// ════════════════════════════════════════════════════════════════
// Minimal in-place radix-2 Cooley-Tukey FFT
// ════════════════════════════════════════════════════════════════

void AudioAnalyzer::fft_radix2(std::vector<std::complex<float>> &x)
{
    const int N = static_cast<int>(x.size());
    if (N <= 1)
        return;

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }

    // Butterfly stages
    for (int len = 2; len <= N; len <<= 1) {
        const float angle = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < N; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j]             = u + v;
                x[i + j + len / 2]   = u - v;
                w *= wlen;
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════
// Hann window
// ════════════════════════════════════════════════════════════════

std::vector<float> AudioAnalyzer::hannWindow(int N)
{
    std::vector<float> w(N);
    for (int i = 0; i < N; ++i)
        w[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(N)));
    return w;
}

// ════════════════════════════════════════════════════════════════
// Next power of two (for zero-padding to radix-2 length)
// ════════════════════════════════════════════════════════════════

static int nextPow2(int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// ════════════════════════════════════════════════════════════════
// Logarithmic band edges: numBars+1 bin indices from 0 to fftLen.
//
// Edge i is fftLen^(i/numBars) truncated, the same curve the Python
// backend uses (np.logspace(0, log10(fftLen), numBars+1)). With
// ~400-500 bins over 96 bars the first dozen edges all truncate to
// 1, which used to leave those bars permanently at 0; every band is
// therefore forced to be at least one bin wide, so edges are strictly
// increasing (the first bands are then linear until the log curve
// overtakes them). Edge 0 is the first non-DC bin.
// ════════════════════════════════════════════════════════════════

static std::vector<int> logBandEdges(int fftLen, int numBars)
{
    std::vector<int> edges(numBars + 1);
    edges[0] = 0;
    for (int i = 1; i <= numBars; ++i) {
        const double t = static_cast<double>(i) / numBars;
        int idx = static_cast<int>(std::pow(static_cast<double>(fftLen), t));
        idx = std::max(idx, edges[i - 1] + 1);
        edges[i] = std::min(idx, fftLen);
    }
    edges[numBars] = fftLen;
    return edges;
}

// ════════════════════════════════════════════════════════════════
// Worker: decode audio file and compute per-chunk FFT levels
// Runs on a QtConcurrent thread — must NOT touch Qt GUI objects.
//
// Decodes with QAudioDecoder (driven by a local QEventLoop so it
// works on this worker thread) to 8 kHz mono PCM, matching the
// Python version's PyAV decode + 8 kHz downsample.
// ════════════════════════════════════════════════════════════════

AudioAnalyzer::AnalysisResult AudioAnalyzer::analyzeAudio(
    const QString &filePath, int numBars, double chunkDuration)
{
    AnalysisResult result;

    QByteArray pcmBuffer;

    // ── Decode via QAudioDecoder (cross-platform) ────────────────────────
    // Uses the native backend on each platform: MediaCodec on Android,
    // GStreamer/FFmpeg on Linux, Windows Media Foundation on Windows,
    // AVFoundation on macOS. QtMultimedia is already linked for QMediaPlayer.
    // We request Int16 mono at 8 kHz; we convert to normalised float32 below
    // (simpler cross-backend than asking for Float because some Android
    // MediaCodec configurations reject Float sample formats).
    QAudioDecoder decoder;
    QAudioFormat fmt;
    fmt.setSampleRate(8000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    decoder.setAudioFormat(fmt);
    decoder.setSource(QUrl::fromLocalFile(filePath));

    QByteArray rawInt16;
    bool errorFlag = false;

    QEventLoop loop;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder,
        [&decoder, &rawInt16]() {
            while (decoder.bufferAvailable()) {
                QAudioBuffer buf = decoder.read();
                if (!buf.isValid()) break;
                rawInt16.append(reinterpret_cast<const char *>(buf.constData<char>()),
                                buf.byteCount());
            }
        });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
        &decoder,
        [&loop, &errorFlag, &filePath](QAudioDecoder::Error err) {
            Q_UNUSED(err);
            errorFlag = true;
            qCWarning(lcAudioAnalyzer) << "QAudioDecoder error for" << filePath;
            loop.quit();
        });

    // Watchdog — bail out if decode takes too long (e.g. codec stall).
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);

    decoder.start();
    loop.exec();
    decoder.stop();

    if (errorFlag || rawInt16.isEmpty()) {
        return result;
    }

    // Convert Int16 → normalised float32 matching the old ffmpeg output path.
    const auto *srcI16 = reinterpret_cast<const int16_t *>(rawInt16.constData());
    const int i16Count = rawInt16.size() / static_cast<int>(sizeof(int16_t));
    pcmBuffer.resize(i16Count * static_cast<int>(sizeof(float)));
    auto *dstF32 = reinterpret_cast<float *>(pcmBuffer.data());
    constexpr float kInv32k = 1.0f / 32768.0f;
    for (int i = 0; i < i16Count; ++i) {
        dstF32[i] = static_cast<float>(srcI16[i]) * kInv32k;
    }

    if (pcmBuffer.isEmpty()) {
        qCWarning(lcAudioAnalyzer) << "QAudioDecoder produced no audio data for" << filePath;
        return result;
    }

    // ── PCM data is now a contiguous float array at 8 kHz mono ─
    const auto *samples     = reinterpret_cast<const float *>(pcmBuffer.constData());
    const int   sampleCount = pcmBuffer.size() / static_cast<int>(sizeof(float));
    const int   sampleRate  = 8000;

    if (sampleCount == 0)
        return result;

    // Normalise
    float maxVal = 0.0f;
    for (int i = 0; i < sampleCount; ++i)
        maxVal = std::max(maxVal, std::abs(samples[i]));

    std::vector<float> normalised(sampleCount);
    if (maxVal > 0.0f) {
        for (int i = 0; i < sampleCount; ++i)
            normalised[i] = samples[i] / maxVal;
    } else {
        std::fill(normalised.begin(), normalised.end(), 0.0f);
    }

    // ── Chunk into ~chunkDuration windows and FFT each ─────────
    const int chunkSize = static_cast<int>(sampleRate * chunkDuration);
    const int numChunks = sampleCount / chunkSize;

    if (numChunks == 0)
        return result;

    const int fftSize = nextPow2(chunkSize);
    const auto window = hannWindow(chunkSize);

    // Magnitudes of the positive frequencies, skipping DC: bins 1..N/2, i.e.
    // 0-4 kHz at the 8 kHz decode rate (the Python backend keeps the same
    // range: np.abs(np.fft.rfft(chunk))[1:]).
    const int fftLen = fftSize / 2;
    const std::vector<int> logIndices = logBandEdges(fftLen, numBars);

    // First pass: raw magnitudes per chunk per bar
    std::vector<std::vector<float>> rawFftData(numChunks);

    for (int c = 0; c < numChunks; ++c) {
        const int offset = c * chunkSize;

        // Zero-padded, windowed complex buffer
        std::vector<std::complex<float>> buf(fftSize, {0.0f, 0.0f});
        for (int i = 0; i < chunkSize; ++i)
            buf[i] = {normalised[offset + i] * window[i], 0.0f};

        fft_radix2(buf);

        std::vector<float> mag(fftLen);
        for (int i = 0; i < fftLen; ++i)
            mag[i] = std::abs(buf[i + 1]);   // +1 to skip DC

        // Bin into bars
        std::vector<float> levels(numBars, 0.0f);
        for (int b = 0; b < numBars; ++b) {
            int startIdx = logIndices[b];
            int endIdx   = logIndices[b + 1];
            if (endIdx > startIdx) {
                float sum = 0.0f;
                for (int i = startIdx; i < endIdx; ++i)
                    sum += mag[i];
                levels[b] = sum / static_cast<float>(endIdx - startIdx);
            }
        }

        rawFftData[c] = std::move(levels);
    }

    // ── Global normalisation (95th percentile, matching Python) ─
    std::vector<float> allValues;
    allValues.reserve(numChunks * numBars);
    for (const auto &chunk : rawFftData)
        for (float v : chunk)
            if (v > 0.0f)
                allValues.push_back(v);

    float globalMax = 1.0f;
    if (!allValues.empty()) {
        std::sort(allValues.begin(), allValues.end());
        int idx95 = static_cast<int>(allValues.size() * 0.95);
        idx95 = std::min(idx95, static_cast<int>(allValues.size()) - 1);
        globalMax = allValues[idx95];
        if (globalMax <= 0.0f)
            globalMax = 1.0f;
    }

    // ── Second pass: normalise → 0-8 integer levels ────────────
    result.fftData.resize(numChunks);
    for (int c = 0; c < numChunks; ++c) {
        result.fftData[c].resize(numBars);
        for (int b = 0; b < numBars; ++b) {
            float norm = rawFftData[c][b] / globalMax;
            norm = std::min(1.0f, norm);
            norm = std::pow(norm, 0.6f);                   // perceptual curve
            result.fftData[c][b] = static_cast<int>(norm * 8.0f);
        }
    }

    result.success = true;
    return result;
}

// ════════════════════════════════════════════════════════════════
// Public slots
// ════════════════════════════════════════════════════════════════

void AudioAnalyzer::analyze_file(const QString &filePath)
{
    qCInfo(lcAudioAnalyzer) << "analyze_file called with:" << filePath;

    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        qCWarning(lcAudioAnalyzer) << "Invalid file path for analysis:" << filePath;
        return;
    }

    // Already analyzed this file
    if (filePath == m_currentFile && !m_fftData.empty()) {
        qCInfo(lcAudioAnalyzer) << "File already analyzed:" << filePath;
        return;
    }

    // Already busy — remember the latest requested file so onAnalysisDone
    // can chain into it once the current analysis completes. Without this,
    // rapid skip presses drop every request after the first, leaving the
    // visualizer showing the previous track's FFT (or nothing).
    if (m_analyzing) {
        m_pendingFile = filePath;
        qCInfo(lcAudioAnalyzer) << "Analysis already in progress, queued:" << filePath;
        return;
    }

    m_currentFile = filePath;
    m_analyzing   = true;

    emit analysisStarted();
    qCInfo(lcAudioAnalyzer) << "Starting audio analysis for:" << filePath;

    // Capture locals for the lambda (numBars, chunkDuration)
    const int    bars     = m_numBars;
    const double chunkDur = m_chunkDuration;

    QFuture<AnalysisResult> future = QtConcurrent::run(
        [filePath, bars, chunkDur]() {
            return AudioAnalyzer::analyzeAudio(filePath, bars, chunkDur);
        });

    m_watcher.setFuture(future);
}

void AudioAnalyzer::onAnalysisDone()
{
    m_analyzing = false;

    AnalysisResult result = m_watcher.result();
    if (result.success) {
        m_fftData = std::move(result.fftData);
        emit analysisComplete();
        qCInfo(lcAudioAnalyzer) << "Analysis complete:"
                                << m_fftData.size() << "chunks generated";
    } else {
        m_fftData.clear();
        qCWarning(lcAudioAnalyzer) << "Analysis failed for" << m_currentFile;
    }

    // If the user skipped to another track while this analysis was running,
    // service that latest request now. Clear before calling because
    // analyze_file may itself set m_pendingFile again if yet another skip
    // lands during this chained analysis.
    if (!m_pendingFile.isEmpty() && m_pendingFile != m_currentFile) {
        const QString next = m_pendingFile;
        m_pendingFile.clear();
        analyze_file(next);
    } else {
        m_pendingFile.clear();
    }
}

void AudioAnalyzer::update_position(float positionSeconds)
{
    if (m_fftData.empty() || !m_isActive)
        return;

    int chunkIndex = static_cast<int>(positionSeconds * 10.0f);
    chunkIndex = std::clamp(chunkIndex, 0, static_cast<int>(m_fftData.size()) - 1);

    const auto &newLevels = m_fftData[chunkIndex];

    // Build QVariantList and compare
    QVariantList varList;
    varList.reserve(static_cast<int>(newLevels.size()));
    for (int v : newLevels)
        varList.append(v);

    if (varList != m_currentLevels) {
        m_currentLevels = varList;
        emit fftDataChanged(m_currentLevels);
    }
}

void AudioAnalyzer::set_active(bool active)
{
    qCInfo(lcAudioAnalyzer) << "set_active called with:" << active
                            << ", has FFT data:" << m_fftData.size() << "chunks";
    m_isActive = active;
    if (!active) {
        // Emit zeros to clear the visualizer
        m_currentLevels.clear();
        m_currentLevels.reserve(m_numBars);
        for (int i = 0; i < m_numBars; ++i)
            m_currentLevels.append(0);
        emit fftDataChanged(m_currentLevels);
    }
}

QVariantList AudioAnalyzer::get_current_levels() const
{
    return m_currentLevels;
}

int AudioAnalyzer::get_num_bars() const
{
    return m_numBars;
}

bool AudioAnalyzer::is_analyzed() const
{
    return !m_fftData.empty();
}

void AudioAnalyzer::clear()
{
    m_fftData.clear();
    m_currentFile.clear();

    m_currentLevels.clear();
    m_currentLevels.reserve(m_numBars);
    for (int i = 0; i < m_numBars; ++i)
        m_currentLevels.append(0);
}

