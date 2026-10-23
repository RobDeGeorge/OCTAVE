#include "audioanalyzer.h"
#include "../util/perfscope.h"

#include <QUrl>
#include <QFileInfo>
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

    m_liveTimer.setInterval(kLiveFrameMs);
    connect(&m_liveTimer, &QTimer::timeout, this, &AudioAnalyzer::update_live);
}

// ════════════════════════════════════════════════════════════════
// Driving (replaces MediaRoom's 16 ms timer, its analyze_file calls on
// track change and its play-state syncs)
// ════════════════════════════════════════════════════════════════

void AudioAnalyzer::setViewActive(bool active)
{
    setDemand(QStringLiteral("view"), active);
}

void AudioAnalyzer::setDemand(const QString &consumer, bool wanted)
{
    if (wanted)
        m_demand.insert(consumer);
    else
        m_demand.remove(consumer);
    const bool active = !m_demand.isEmpty();
    if (active == m_viewActive)
        return;
    m_viewActive = active;
    // A track that changed while no view was up (or mid card animation,
    // so the decode doesn't compete with its frames) is analysed now
    if (m_viewActive && !m_live && !m_localFile.isEmpty())
        analyze_file(m_localFile);
    refreshActive();
}

void AudioAnalyzer::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    refreshActive();
}

void AudioAnalyzer::setLocalFile(const QString &filePath)
{
    if (filePath == m_localFile)
        return;
    m_localFile = filePath;
    // Next pass: a page reacting to the same track change (Media Room's card
    // animation) holds the view first, so the decode waits for it to settle
    QMetaObject::invokeMethod(this, [this] {
        if (m_viewActive && !m_live && !m_localFile.isEmpty())
            analyze_file(m_localFile);
    }, Qt::QueuedConnection);
}

void AudioAnalyzer::setPosition(qint64 positionMs)
{
    m_positionMs = positionMs;
    if (m_isActive && !m_live)
        update_position(static_cast<float>(positionMs / 1000.0));
}

void AudioAnalyzer::refreshActive()
{
    const bool active = m_viewActive && m_playing;
    if (active != m_isActive) {
        set_active(active);
        if (active && !m_live)
            update_position(static_cast<float>(m_positionMs / 1000.0));
    }
    const bool liveFrames = active && m_live;
    if (liveFrames && !m_liveTimer.isActive())
        m_liveTimer.start();
    else if (!liveFrames)
        m_liveTimer.stop();
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
// Edge i is fftLen^(i/numBars) truncated. With
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
// Append one decoded buffer to `mono` (cleared first), averaging
// channels, in whatever sample format the platform decoder chose.
// ════════════════════════════════════════════════════════════════

static void appendMono(const QAudioBuffer &buf, std::vector<float> &mono, int &sampleRate)
{
    mono.clear();
    const QAudioFormat f = buf.format();
    const int frames   = static_cast<int>(buf.frameCount());
    const int channels = std::max(1, f.channelCount());
    if (frames <= 0)
        return;
    if (sampleRate <= 0)
        sampleRate = f.sampleRate();

    const float invCh = 1.0f / static_cast<float>(channels);

    switch (f.sampleFormat()) {
    case QAudioFormat::Float: {
        const float *p = buf.constData<float>();
        for (int i = 0; i < frames; ++i, p += channels) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += p[c];
            mono.push_back(sum * invCh);
        }
        break;
    }
    case QAudioFormat::Int16: {
        const auto *p = buf.constData<int16_t>();
        constexpr float k = 1.0f / 32768.0f;
        for (int i = 0; i < frames; ++i, p += channels) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += static_cast<float>(p[c]) * k;
            mono.push_back(sum * invCh);
        }
        break;
    }
    case QAudioFormat::Int32: {
        const auto *p = buf.constData<int32_t>();
        constexpr float k = 1.0f / 2147483648.0f;
        for (int i = 0; i < frames; ++i, p += channels) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += static_cast<float>(p[c]) * k;
            mono.push_back(sum * invCh);
        }
        break;
    }
    case QAudioFormat::UInt8: {
        const auto *p = buf.constData<uint8_t>();
        constexpr float k = 1.0f / 128.0f;
        for (int i = 0; i < frames; ++i, p += channels) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += (static_cast<float>(p[c]) - 128.0f) * k;
            mono.push_back(sum * invCh);
        }
        break;
    }
    default:
        break;
    }
}

// ════════════════════════════════════════════════════════════════
// Streaming analysis state: decoded audio goes in buffer by buffer and
// only the per-chunk band magnitudes are kept (~4 KB per second of
// audio). Collecting the whole decoded track first used hundreds of MB
// per hour of audio, enough to freeze an Orange Pi on a long mix. At most
// kMaxAnalysisSeconds are analysed.
// ════════════════════════════════════════════════════════════════

static constexpr int kMaxAnalysisSeconds = 3 * 60 * 60;

namespace {
struct ChunkStream {
    int numBars;
    double chunkDuration;

    bool ready = false;
    int factor = 1;             // keep every factor-th native sample
    int chunkSize = 0;
    int fftSize = 0;
    int fftLen = 0;
    long long position = 0;     // native samples seen (decimation phase)
    long long kept = 0;         // decimated samples kept
    long long maxSamples = 0;
    std::vector<float> window;
    std::vector<int> edges;
    std::vector<float> chunk;   // decimated samples of the chunk being filled
    std::vector<std::complex<float>> buf;
    std::vector<float> raw;     // numBars magnitudes per finished chunk

    ChunkStream(int bars, double dur) : numBars(bars), chunkDuration(dur) {}

    // Decimate to ~8 kHz: every (rate // 8000)th sample of the whole
    // track, effective rate nativeRate / factor.
    bool init(int nativeRate)
    {
        factor = std::max(1, nativeRate / 8000);
        const int sampleRate = nativeRate / factor;
        chunkSize = static_cast<int>(sampleRate * chunkDuration);
        if (chunkSize < 2)
            return false;
        // Magnitudes of the positive frequencies, skipping DC: bins 1..N/2,
        // i.e. 0 to sampleRate/2 (~4 kHz) at the decimated rate.
        fftSize = nextPow2(chunkSize);
        fftLen = fftSize / 2;
        window = AudioAnalyzer::hannWindow(chunkSize);
        edges = logBandEdges(fftLen, numBars);
        chunk.reserve(chunkSize);
        buf.resize(fftSize);
        maxSamples = static_cast<long long>(kMaxAnalysisSeconds) * sampleRate;
        ready = true;
        return true;
    }

    bool full() const { return ready && kept >= maxSamples; }

    void add(const std::vector<float> &mono)
    {
        const long long n = static_cast<long long>(mono.size());
        long long i = (factor - position % factor) % factor;
        for (; i < n && !full(); i += factor) {
            chunk.push_back(mono[i]);
            ++kept;
            if (static_cast<int>(chunk.size()) == chunkSize)
                finishChunk();
        }
        position += n;
    }

    void finishChunk()
    {
        // Zero-padded, windowed complex buffer
        std::fill(buf.begin(), buf.end(), std::complex<float>(0.0f, 0.0f));
        for (int i = 0; i < chunkSize; ++i)
            buf[i] = {chunk[i] * window[i], 0.0f};
        chunk.clear();

        AudioAnalyzer::fft_radix2(buf);

        // Mean magnitude per log band (+1 skips DC)
        for (int b = 0; b < numBars; ++b) {
            const int startIdx = edges[b];
            const int endIdx   = edges[b + 1];
            float level = 0.0f;
            if (endIdx > startIdx) {
                float sum = 0.0f;
                for (int i = startIdx; i < endIdx; ++i)
                    sum += std::abs(buf[i + 1]);
                level = sum / static_cast<float>(endIdx - startIdx);
            }
            raw.push_back(level);
        }
    }
};
} // namespace

// ════════════════════════════════════════════════════════════════
// Worker: decode audio file and compute per-chunk FFT levels
// Runs on a QtConcurrent thread — must NOT touch Qt GUI objects.
//
// Decodes with QAudioDecoder (driven by a local QEventLoop so it
// works on this worker thread) in the file's native format, mixing
// each buffer to mono and decimating to ~8 kHz as it arrives.
// ════════════════════════════════════════════════════════════════

AudioAnalyzer::AnalysisResult AudioAnalyzer::analyzeAudio(
    const QString &filePath, int numBars, double chunkDuration)
{
    OCTAVE_PERF_SCOPE("audio._analyze_audio (FFT)");
    AnalysisResult result;

    // ── Decode via QAudioDecoder (cross-platform) ────────────────────────
    // Uses the native backend on each platform: MediaCodec on Android,
    // FFmpeg/GStreamer on Linux, Windows Media Foundation on Windows,
    // AVFoundation on macOS. QtMultimedia is already linked for QMediaPlayer.
    //
    // Deliberately no setAudioFormat(): asking the FFmpeg backend to convert
    // an MP3 to 8 kHz mono Int16 yields no buffers and no error on Qt 6.11
    // (the decode just sat on the watchdog for 30 s and the visualizer stayed
    // dark), while the native format decodes in a few hundred ms. WAV sources
    // converted fine, which is what hid the failure. The mono mix-down and
    // the decimation happen below in appendMono() and ChunkStream, so the
    // decoder's choice of rate, channel count and sample format no longer
    // matters — on Android this also sidesteps MediaCodec's format quirks.
    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(filePath));

    ChunkStream stream(numBars, chunkDuration);
    std::vector<float> mono;       // one buffer, mixed to mono, native rate
    int  nativeRate = 0;
    bool errorFlag  = false;
    bool timedOut   = false;
    bool badRate    = false;

    QEventLoop loop;
    auto drain = [&]() {
        while (decoder.bufferAvailable() && !stream.full() && !badRate) {
            QAudioBuffer buf = decoder.read();
            if (!buf.isValid()) break;
            appendMono(buf, mono, nativeRate);
            if (mono.empty())
                continue;
            if (!stream.ready && !stream.init(nativeRate)) {
                badRate = true;
                break;
            }
            stream.add(mono);
        }
        if (stream.full() || badRate)
            loop.quit();
    };
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, drain);
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
        &decoder,
        [&loop, &errorFlag, &filePath, &decoder](QAudioDecoder::Error err) {
            Q_UNUSED(err);
            errorFlag = true;
            qCWarning(lcAudioAnalyzer) << "QAudioDecoder error for" << filePath
                                       << "-" << decoder.errorString();
            loop.quit();
        });

    // Watchdog — bail out if decode takes too long (e.g. codec stall).
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&loop, &timedOut]() {
        timedOut = true;
        loop.quit();
    });
    watchdog.start(30000);

    decoder.start();
    loop.exec();
    drain();            // buffers queued between the last bufferReady and finished
    decoder.stop();

    if (errorFlag)
        return result;
    if (timedOut) {
        qCWarning(lcAudioAnalyzer) << "QAudioDecoder timed out after 30 s for" << filePath
                                   << "-" << stream.kept << "samples analysed at"
                                   << nativeRate << "Hz";
        return result;
    }
    if (badRate || nativeRate <= 0 || stream.kept == 0) {
        qCWarning(lcAudioAnalyzer) << "QAudioDecoder produced no usable audio data for" << filePath;
        return result;
    }
    if (stream.full())
        qCInfo(lcAudioAnalyzer) << "Analysing only the first" << kMaxAnalysisSeconds
                                << "s of" << filePath;

    std::vector<float> &raw = stream.raw;
    if (raw.empty())
        return result;   // shorter than one chunk

    // ── Global normalisation (95th percentile) ─
    // Scaling the samples scales every magnitude and this max alike, so
    // the levels need no peak normalisation of the samples first.
    std::vector<float> allValues;
    allValues.reserve(raw.size());
    for (float v : raw)
        if (v > 0.0f)
            allValues.push_back(v);

    float globalMax = 1.0f;
    if (!allValues.empty()) {
        const size_t idx95 = std::min(static_cast<size_t>(allValues.size() * 0.95),
                                      allValues.size() - 1);
        std::nth_element(allValues.begin(), allValues.begin() + idx95, allValues.end());
        globalMax = allValues[idx95];
        if (globalMax <= 0.0f)
            globalMax = 1.0f;
    }
    allValues.clear();
    allValues.shrink_to_fit();

    // ── Normalise → 0-8 integer levels ─────────────────────────
    result.fftData.resize(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        float norm = std::min(1.0f, raw[i] / globalMax);
        norm = std::pow(norm, 0.6f);                   // perceptual curve
        result.fftData[i] = static_cast<quint8>(norm * 8.0f);
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
    // Don't keep driving the bars from the previous track meanwhile.
    m_fftData.clear();

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

    // The track changed while this one was analysed: drop the stale result
    // and analyse the track that is actually playing. Clear before calling
    // because analyze_file may itself set m_pendingFile again if yet another
    // skip lands during this chained analysis.
    if (!m_pendingFile.isEmpty() && m_pendingFile != m_currentFile) {
        const QString next = m_pendingFile;
        m_pendingFile.clear();
        m_fftData.clear();
        analyze_file(next);
        return;
    }
    m_pendingFile.clear();

    AnalysisResult result = m_watcher.result();
    if (result.success) {
        m_fftData = std::move(result.fftData);
        emit analysisComplete();
        if (m_isActive && !m_live)
            update_position(static_cast<float>(m_positionMs / 1000.0));
        qCInfo(lcAudioAnalyzer) << "Analysis complete:"
                                << chunkCount() << "chunks generated";
    } else {
        m_fftData.clear();
        qCWarning(lcAudioAnalyzer) << "Analysis failed for" << m_currentFile;
    }
}

void AudioAnalyzer::update_position(float positionSeconds)
{
    if (m_fftData.empty() || !m_isActive)
        return;

    const int chunkIndex = std::max(0, static_cast<int>(positionSeconds * 10.0f));

    // Build QVariantList and compare; flat bars past the analysed part
    // (kMaxAnalysisSeconds)
    QVariantList varList;
    varList.reserve(m_numBars);
    if (static_cast<size_t>(chunkIndex) >= chunkCount()) {
        for (int b = 0; b < m_numBars; ++b)
            varList.append(0);
    } else {
        const quint8 *levels = m_fftData.data() + static_cast<size_t>(chunkIndex) * m_numBars;
        for (int b = 0; b < m_numBars; ++b)
            varList.append(static_cast<int>(levels[b]));
    }

    if (varList != m_currentLevels) {
        m_currentLevels = varList;
        emit fftDataChanged(m_currentLevels);
    }
}

void AudioAnalyzer::set_active(bool active)
{
    qCInfo(lcAudioAnalyzer) << "set_active called with:" << active
                            << ", has FFT data:" << chunkCount() << "chunks";
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


// ════════════════════════════════════════════════════════════════
// Live mode (phone mirror)
//
// The phone's audio arrives as PCM that OCTAVE plays itself, so there
// is no file to pre-analyse. ScrcpyClient hands every chunk it writes
// to the audio sink to feed_live_pcm(), which mixes it to mono and
// decimates it to 8 kHz into a one-second ring. update_live(), every
// kLiveFrameMs while live and shown, analyses the 100 ms ending at the
// sample being heard (the sink's backlog behind the newest one) with
// the same window, FFT and log bands as a file chunk. A stream has no
// whole-track 95th percentile to normalise by, so the reference
// follows each frame's 95th percentile: it jumps up at once and
// decays over kLiveReleaseSec, floored so silence shows flat bars;
// kLiveRefScale brings the bar heights in line with a file's.
// ════════════════════════════════════════════════════════════════

static constexpr int   kLiveInputRate   = 48000;   // ScrcpyClient: s16le stereo
static constexpr int   kLiveFactor      = kLiveInputRate / 8000;
static constexpr int   kLiveRate        = kLiveInputRate / kLiveFactor;
static constexpr int   kLiveRingSamples = kLiveRate;           // 1 s
static constexpr int   kLiveStaleMs     = 250;   // no PCM for this long: the stream stopped
static constexpr float kLiveReleaseSec  = 1.0f;
// The held per-frame 95th percentile runs higher than a track's global one;
// scaled down, the bars reach the same heights as a pre-analysed file
static constexpr float kLiveRefScale    = 0.85f;
// Mean band magnitude of a ~-46 dBFS tone (ScrcpyClient's "is this sound"
// threshold); below it the bars stay flat instead of amplifying noise
static constexpr float kLiveRefFloor    = 1.0f;

QVariantList AudioAnalyzer::zeroLevels() const
{
    QVariantList zeros;
    zeros.reserve(m_numBars);
    for (int i = 0; i < m_numBars; ++i)
        zeros.append(0);
    return zeros;
}

void AudioAnalyzer::resetLive()
{
    m_liveRing.assign(kLiveRingSamples, 0.0f);
    m_liveTotal = 0;
    m_livePhase = 0;
    m_liveBufferedMs = 0;
    m_liveRef = kLiveRefFloor;
    m_liveSinceFeed.invalidate();
    m_liveSinceUpdate.invalidate();
}

void AudioAnalyzer::set_live(bool live)
{
    if (live == m_live)
        return;
    qCInfo(lcAudioAnalyzer) << "live mode" << (live ? "on" : "off");
    m_live = live;
    if (live) {
        const int chunkSize = static_cast<int>(kLiveRate * m_chunkDuration);
        const int fftSize = nextPow2(chunkSize);
        m_liveWindow = hannWindow(chunkSize);
        m_liveEdges = logBandEdges(fftSize / 2, m_numBars);
        m_liveBuf.resize(fftSize);
        m_liveBands.resize(m_numBars);
        resetLive();
    } else {
        m_liveRing.clear();
        m_liveRing.shrink_to_fit();
    }
    m_currentLevels = zeroLevels();
    emit fftDataChanged(m_currentLevels);
    refreshActive();
    // Back to local playback: its track's bars, if one is showing
    if (!m_live && m_viewActive && !m_localFile.isEmpty())
        analyze_file(m_localFile);
}

void AudioAnalyzer::feed_live_pcm(const QByteArray &pcm, int bufferedMs)
{
    if (!m_live)
        return;
    const auto *s = reinterpret_cast<const qint16 *>(pcm.constData());
    const int frames = pcm.size() / 4;
    constexpr float k = 1.0f / 65536.0f;   // average of two channels, to -1..1
    int i = (kLiveFactor - m_livePhase) % kLiveFactor;
    for (; i < frames; i += kLiveFactor) {
        m_liveRing[static_cast<size_t>(m_liveTotal % kLiveRingSamples)] =
            (static_cast<float>(s[2 * i]) + static_cast<float>(s[2 * i + 1])) * k;
        ++m_liveTotal;
    }
    m_livePhase = (m_livePhase + frames) % kLiveFactor;
    m_liveBufferedMs = std::max(0, bufferedMs);
    m_liveSinceFeed.start();
}

void AudioAnalyzer::update_live()
{
    if (!m_live || !m_isActive)
        return;

    const int chunkSize = static_cast<int>(m_liveWindow.size());
    const qint64 sinceFeed = m_liveSinceFeed.isValid() ? m_liveSinceFeed.elapsed() : -1;
    QVariantList varList;
    varList.reserve(m_numBars);

    if (sinceFeed < 0 || sinceFeed > kLiveStaleMs || m_liveTotal < chunkSize) {
        varList = zeroLevels();
    } else {
        // The newest sample is still bufferedMs - sinceFeed ahead of the speaker
        const long long delay = std::clamp<long long>(
            (m_liveBufferedMs - sinceFeed) * kLiveRate / 1000, 0, kLiveRingSamples - chunkSize);
        const long long end = std::max<long long>(chunkSize, m_liveTotal - delay);
        std::fill(m_liveBuf.begin(), m_liveBuf.end(), std::complex<float>(0.0f, 0.0f));
        for (int i = 0; i < chunkSize; ++i) {
            const float v = m_liveRing[static_cast<size_t>((end - chunkSize + i) % kLiveRingSamples)];
            m_liveBuf[i] = {v * m_liveWindow[i], 0.0f};
        }
        fft_radix2(m_liveBuf);

        // Mean magnitude per log band (+1 skips DC), as in ChunkStream
        for (int b = 0; b < m_numBars; ++b) {
            const int startIdx = m_liveEdges[b];
            const int endIdx   = m_liveEdges[b + 1];
            float sum = 0.0f;
            for (int i = startIdx; i < endIdx; ++i)
                sum += std::abs(m_liveBuf[i + 1]);
            m_liveBands[b] = endIdx > startIdx ? sum / static_cast<float>(endIdx - startIdx) : 0.0f;
        }

        std::vector<float> sorted(m_liveBands);
        const size_t idx95 = std::min(static_cast<size_t>(sorted.size() * 0.95), sorted.size() - 1);
        std::nth_element(sorted.begin(), sorted.begin() + idx95, sorted.end());
        const float p95 = sorted[idx95];
        const float dt = m_liveSinceUpdate.isValid() ? m_liveSinceUpdate.elapsed() / 1000.0f : 0.0f;
        m_liveRef = std::max({p95, kLiveRefFloor, m_liveRef * std::exp(-dt / kLiveReleaseSec)});

        const float ref = std::max(kLiveRefFloor, m_liveRef * kLiveRefScale);
        for (int b = 0; b < m_numBars; ++b) {
            const float norm = std::pow(std::min(1.0f, m_liveBands[b] / ref), 0.6f);
            varList.append(static_cast<int>(norm * 8.0f));
        }
    }
    m_liveSinceUpdate.start();

    if (varList != m_currentLevels) {
        m_currentLevels = varList;
        emit fftDataChanged(m_currentLevels);
    }
}
