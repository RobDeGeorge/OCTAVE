"""
Audio Analyzer for Waveform Visualization
Analyzes audio files using FFT to generate real-time waveform visualization data.
"""

from PySide6.QtCore import QObject, Signal, Slot, QTimer, QElapsedTimer
from concurrent.futures import ThreadPoolExecutor
import math
import os

from backend.logging_config import get_logger
logger = get_logger(__name__)

from backend.lazy_import import available, lazy_module

# numpy and PyAV are only needed when a track is analysed; importing them at
# startup cost ~0.2 s before the first frame, so they load on first use.
NUMPY_AVAILABLE = available("numpy")
if not NUMPY_AVAILABLE:
    logger.warning("NumPy not available - waveform visualization will be disabled")
np = lazy_module("numpy")

AV_AVAILABLE = available("av")
if not AV_AVAILABLE:
    logger.warning("PyAV not available - waveform visualization will be disabled")
av = lazy_module("av")

# Longest stretch of a track that is analysed; the bars stay flat after it.
MAX_ANALYSIS_SECONDS = 3 * 60 * 60
# Chunks FFT'd per batch while decoding (60 s at 100 ms chunks).
ANALYSIS_BATCH_CHUNKS = 600

# Live mode (phone mirror), see update_live; same values as the C++ kLive*.
LIVE_INPUT_RATE = 48000          # ScrcpyClient: s16le stereo
LIVE_FACTOR = LIVE_INPUT_RATE // 8000
LIVE_RATE = LIVE_INPUT_RATE // LIVE_FACTOR
LIVE_RING_SAMPLES = LIVE_RATE    # 1 s
LIVE_STALE_MS = 250              # no PCM for this long: the stream stopped
LIVE_RELEASE_SEC = 1.0
# The held per-frame 95th percentile runs higher than a track's global one;
# scaled down, the bars reach the same heights as a pre-analysed file
LIVE_REF_SCALE = 0.85
# Mean band magnitude of a ~-46 dBFS tone (ScrcpyClient's "is this sound"
# threshold); below it the bars stay flat instead of amplifying noise
LIVE_REF_FLOOR = 1.0


class AudioAnalyzer(QObject):
    """
    Analyzes audio files to generate FFT visualization data.
    Pre-computes FFT for entire track, then provides data based on playback position.
    """

    # Signal emitted with FFT levels as a list of integers (0-8)
    fftDataChanged = Signal(list)

    # Signal emitted when analysis is complete
    analysisComplete = Signal()

    # Signal emitted when analysis starts
    analysisStarted = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)

        # FFT data storage - (num_chunks, num_bars) uint8 array of levels,
        # one row per time chunk; [] when there is none
        self._fft_data = []
        self._current_file = ""
        self._num_bars = 96
        self._chunk_duration = 0.1  # 100ms chunks (10 chunks per second)

        # Current levels emitted to QML (no animation, just the looked-up data)
        self._current_levels = [0] * self._num_bars

        # Track if we're actively playing
        self._is_active = False

        # Thread pool for off-thread analysis
        self._executor = ThreadPoolExecutor(max_workers=1)
        self._analyzing = False
        self._pending_future = None
        # Newest file requested while an analysis was running; analysed next.
        self._queued_file = None

        # Poll timer — checks if the worker thread finished, keeps Qt work on the main thread
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(100)
        self._poll_timer.timeout.connect(self._check_analysis_done)

        # Live mode (GUI thread only): decimated mono ring of the last second
        self._live = False
        self._live_ring = None
        self._live_total = 0
        self._live_phase = 0
        self._live_buffered_ms = 0
        self._live_ref = LIVE_REF_FLOOR
        self._live_since_feed = QElapsedTimer()
        self._live_since_update = QElapsedTimer()
        self._live_window = None
        self._live_starts = None
        self._live_widths = None
        self._live_fft_len = 0

    @Slot(str)
    def analyze_file(self, file_path: str):
        """
        Analyze an audio file and pre-compute FFT visualization data.
        Called when a new track starts playing. Runs off the main thread.
        """
        logger.info(f"analyze_file called with: {file_path}")

        if not AV_AVAILABLE or not NUMPY_AVAILABLE:
            logger.warning("PyAV or NumPy not available, skipping analysis")
            return

        if not file_path or not os.path.exists(file_path):
            logger.warning(f"Invalid file path for analysis: {file_path}")
            return

        # Skip if already analyzed
        if file_path == self._current_file and len(self._fft_data):
            logger.info(f"File already analyzed: {file_path}")
            return

        # One analysis at a time. Remember only the newest request: its
        # result must not be replaced by the one still running (which used to
        # leave the previous track's levels showing after a quick skip).
        if self._analyzing:
            logger.info("Analysis in progress, queued: %s", file_path)
            self._queued_file = file_path
            return

        self._current_file = file_path
        self._analyzing = True
        # Don't keep driving the bars from the previous track meanwhile.
        self._fft_data = []

        self.analysisStarted.emit()
        logger.info(f"Starting audio analysis for: {file_path}")

        # Submit work to the thread pool and poll for completion from the main thread
        self._pending_future = self._executor.submit(self._analyze_audio, file_path)
        self._poll_timer.start()

    def _check_analysis_done(self):
        """Poll callback — runs on the main thread via QTimer."""
        if self._pending_future is None or not self._pending_future.done():
            return

        self._poll_timer.stop()
        future = self._pending_future
        self._pending_future = None
        self._analyzing = False

        queued, self._queued_file = self._queued_file, None
        if queued and queued != self._current_file:
            # The track changed while this one was analysed: drop the stale
            # result and analyse the track that is actually playing.
            future.cancel()
            self._fft_data = []
            self.analyze_file(queued)
            return

        try:
            result = future.result()
            if result is not None:
                self._fft_data = result
                self.analysisComplete.emit()
                logger.info(f"Analysis complete: {len(self._fft_data)} chunks generated")
            else:
                self._fft_data = []
        except Exception as e:
            logger.error(f"Error in audio analysis thread: {e}", exc_info=True)
            self._fft_data = []

    def _analyze_audio(self, file_path: str):
        """
        Decode audio and compute FFT for visualization.
        Runs on a worker thread — must not touch Qt objects.
        Returns a (num_chunks, num_bars) uint8 array of 0-8 levels, or None
        on failure.

        Streams: each decoded frame is mixed to mono and decimated to ~8 kHz
        on arrival, and the FFT runs every ANALYSIS_BATCH_CHUNKS chunks, so
        only the per-chunk band magnitudes are kept for the whole track
        (~4 KB per second of audio). Decoding whole tracks first used ~1 MB
        of RAM per second of audio: a long mix or podcast needed several GB
        and froze the Orange Pi. At most MAX_ANALYSIS_SECONDS are analysed.
        """
        try:
            container = av.open(file_path)
        except Exception as e:
            logger.error(f"FFT analysis error: {e}")
            return None
        try:
            audio_stream = next((s for s in container.streams if s.type == 'audio'), None)

            if not audio_stream:
                logger.debug("No audio stream found")
                return None

            # Decimate to ~8 kHz: every factor-th sample of the whole track,
            # effective rate rate // factor (the C++ backend does the same).
            sample_rate = audio_stream.rate or 44100
            factor = max(1, sample_rate // 8000)
            effective_rate = sample_rate // factor
            chunk_size = int(effective_rate * self._chunk_duration)
            fft_len = chunk_size // 2
            if fft_len == 0:
                logger.debug("Sample rate too low for analysis")
                return None

            # Magnitudes of the positive frequencies, skipping DC: rfft bins
            # 1..N/2, i.e. 0-4 kHz at the 8 kHz effective rate (the C++
            # backend keeps the same range). The band edges depend only on
            # the bin count, so compute them once.
            edges = np.asarray(self._log_band_edges(fft_len, self._num_bars))
            widths = np.diff(edges)
            starts = np.minimum(edges[:-1], fft_len - 1)
            window = np.hanning(chunk_size).astype(np.float32)

            def band_levels(samples):
                # (n, chunk_size) Hann-windowed chunks, one rfft along the
                # chunk axis, then the mean magnitude per log band; an empty
                # band (only possible when there are fewer bins than bars)
                # is 0.
                chunks = samples.reshape(-1, chunk_size) * window
                fft = np.abs(np.fft.rfft(chunks, axis=1))[:, 1:1 + fft_len]
                sums = np.add.reduceat(fft, starts, axis=1)
                return np.where(widths > 0, sums / np.maximum(widths, 1), 0.0).astype(np.float32)

            max_samples = int(MAX_ANALYSIS_SECONDS * effective_rate)
            batch_samples = ANALYSIS_BATCH_CHUNKS * chunk_size
            pending = []        # decimated samples not yet FFT'd
            pending_len = 0
            kept = 0            # decimated samples so far
            position = 0        # native samples so far (decimation phase)
            raw_batches = []

            def flush(final):
                nonlocal pending, pending_len
                if not pending:
                    return
                samples = np.concatenate(pending)
                usable = (len(samples) // chunk_size) * chunk_size
                if usable:
                    raw_batches.append(band_levels(samples[:usable]))
                rest = samples[usable:]
                pending = [rest] if len(rest) and not final else []
                pending_len = len(rest) if pending else 0

            for frame in container.decode(audio_stream):
                data = frame.to_ndarray()
                channels = len(frame.layout.channels)
                if data.ndim > 1 and data.shape[0] > 1:
                    # Planar: (channels, n)
                    mono = data.mean(axis=0, dtype=np.float32)
                elif channels > 1:
                    # Packed: (1, n * channels), interleaved
                    mono = data.reshape(-1, channels).mean(axis=1, dtype=np.float32)
                else:
                    mono = data.reshape(-1).astype(np.float32)

                piece = mono[(-position) % factor::factor]
                position += len(mono)
                if kept + len(piece) > max_samples:
                    piece = piece[:max_samples - kept]
                pending.append(piece)
                pending_len += len(piece)
                kept += len(piece)

                if pending_len >= batch_samples:
                    flush(final=False)
                if kept >= max_samples:
                    logger.info("Analysing only the first %d s of %s",
                                MAX_ANALYSIS_SECONDS, file_path)
                    break
            flush(final=True)

            if not raw_batches:
                logger.debug("Audio too short for analysis")
                return None
            raw = np.concatenate(raw_batches)

            # Global max for normalization (95th percentile to avoid
            # outliers). Scaling the samples scales every magnitude and this
            # max alike, so the levels need no peak normalisation of the
            # samples first.
            positive = raw[raw > 0]
            global_max = np.percentile(positive, 95) if positive.size else 1.0

            # Normalize and map to the 0-8 range
            if global_max > 0:
                return (np.minimum(1.0, raw / global_max) ** 0.6 * 8).astype(np.uint8)
            return np.zeros(raw.shape, dtype=np.uint8)

        except Exception as e:
            logger.error(f"FFT analysis error: {e}")
            return None
        finally:
            container.close()

    @staticmethod
    def _log_band_edges(fft_len, num_bars):
        """Logarithmic band edges: num_bars + 1 bin indices from 0 to fft_len.

        Edge i is fft_len ** (i / num_bars) truncated (np.logspace(0,
        log10(fft_len), num_bars + 1)), the same curve the C++ backend uses.
        With ~400-500 bins over 96 bars the first dozen edges all truncate to
        1, which used to leave those bars permanently at 0; every band is
        therefore forced to be at least one bin wide, so edges are strictly
        increasing (the first bands are then linear until the log curve
        overtakes them). Edge 0 is the first non-DC bin.
        """
        edges = [0] * (num_bars + 1)
        for i in range(1, num_bars + 1):
            idx = int(fft_len ** (i / num_bars))
            idx = max(idx, edges[i - 1] + 1)
            edges[i] = min(idx, fft_len)
        edges[num_bars] = fft_len
        return edges

    @Slot(float)
    def update_position(self, position_seconds: float):
        """
        Look up pre-computed FFT levels for the current playback position
        and emit them directly to QML. No intermediate animation — QML
        handles any smoothing it needs.
        """
        if not len(self._fft_data) or not self._is_active:
            return

        chunk_index = int(position_seconds * 10)
        if chunk_index >= len(self._fft_data):
            # Past the analysed part (MAX_ANALYSIS_SECONDS): flat bars
            new_levels = [0] * self._num_bars
        else:
            new_levels = self._fft_data[max(0, chunk_index)].tolist()

        # Only emit if levels actually changed
        if new_levels != self._current_levels:
            self._current_levels = new_levels
            self.fftDataChanged.emit(new_levels)

    @Slot(bool)
    def set_active(self, active: bool):
        """Enable or disable the visualizer."""
        logger.info(f"set_active called with: {active}, has FFT data: {len(self._fft_data)} chunks")
        self._is_active = active
        if not active:
            # Emit zeros to clear the visualizer
            self._current_levels = [0] * self._num_bars
            self.fftDataChanged.emit(self._current_levels)

    @Slot(result=list)
    def get_current_levels(self) -> list:
        """Get current FFT levels for QML."""
        return self._current_levels

    @Slot(result=int)
    def get_num_bars(self) -> int:
        """Get number of frequency bars."""
        return self._num_bars

    @Slot(result=bool)
    def is_analyzed(self) -> bool:
        """Check if current file has been analyzed."""
        return len(self._fft_data) > 0

    @Slot()
    def clear(self):
        """Clear analysis data."""
        self._fft_data = []
        self._current_file = ""
        self._current_levels = [0] * self._num_bars

    # ── Live mode (phone mirror) ─────────────────────────────────────
    #
    # The phone's audio arrives as PCM that OCTAVE plays itself, so there is
    # no file to pre-analyse. ScrcpyClient hands every chunk it writes to the
    # audio sink to feed_live_pcm(), which mixes it to mono and decimates it
    # to 8 kHz into a one-second ring. update_live(), polled at the visualizer
    # frame rate, analyses the 100 ms ending at the sample being heard (the
    # sink's backlog behind the newest one) with the same window, FFT and log
    # bands as a file chunk. A stream has no whole-track 95th percentile to
    # normalise by, so the reference follows each frame's 95th percentile: it
    # jumps up at once and decays over LIVE_RELEASE_SEC, floored so silence
    # shows flat bars; LIVE_REF_SCALE brings the bar heights in line with a
    # file's. C++ twin: AudioAnalyzer::update_live.

    @Slot(bool)
    def set_live(self, live: bool):
        """Drive the bars from phone PCM (feed_live_pcm) instead of a file."""
        live = bool(live) and NUMPY_AVAILABLE
        if live == self._live:
            return
        logger.info("live mode %s", "on" if live else "off")
        self._live = live
        if live:
            chunk_size = int(LIVE_RATE * self._chunk_duration)
            self._live_fft_len = chunk_size // 2
            edges = np.asarray(self._log_band_edges(self._live_fft_len, self._num_bars))
            self._live_widths = np.diff(edges)
            self._live_starts = np.minimum(edges[:-1], self._live_fft_len - 1)
            self._live_window = np.hanning(chunk_size).astype(np.float32)
            self._live_ring = np.zeros(LIVE_RING_SAMPLES, dtype=np.float32)
            self._live_total = 0
            self._live_phase = 0
            self._live_buffered_ms = 0
            self._live_ref = LIVE_REF_FLOOR
            self._live_since_feed.invalidate()
            self._live_since_update.invalidate()
        else:
            self._live_ring = None
        self._current_levels = [0] * self._num_bars
        self.fftDataChanged.emit(self._current_levels)

    def feed_live_pcm(self, pcm: bytes, buffered_ms: int):
        """s16le 48 kHz stereo just written to the audio sink; buffered_ms is
        how much the sink still holds ahead of it (what is not heard yet)."""
        if not self._live:
            return
        frames = len(pcm) // 4
        if frames == 0:
            return
        stereo = np.frombuffer(pcm, dtype="<i2", count=frames * 2).reshape(-1, 2)
        start = (LIVE_FACTOR - self._live_phase) % LIVE_FACTOR
        piece = stereo[start::LIVE_FACTOR].sum(axis=1, dtype=np.float32) * (1.0 / 65536.0)
        self._live_phase = (self._live_phase + frames) % LIVE_FACTOR
        n = len(piece)
        if n:
            if n >= LIVE_RING_SAMPLES:
                piece = piece[-LIVE_RING_SAMPLES:]
                self._live_total += n - LIVE_RING_SAMPLES
                n = LIVE_RING_SAMPLES
            pos = self._live_total % LIVE_RING_SAMPLES
            first = min(n, LIVE_RING_SAMPLES - pos)
            self._live_ring[pos:pos + first] = piece[:first]
            self._live_ring[:n - first] = piece[first:]
            self._live_total += n
        self._live_buffered_ms = max(0, int(buffered_ms))
        self._live_since_feed.start()

    @Slot()
    def update_live(self):
        """Emit the bars for the phone audio being heard right now."""
        if not self._live or not self._is_active:
            return
        chunk_size = len(self._live_window)
        since_feed = self._live_since_feed.elapsed() if self._live_since_feed.isValid() else -1
        if since_feed < 0 or since_feed > LIVE_STALE_MS or self._live_total < chunk_size:
            new_levels = [0] * self._num_bars
        else:
            # The newest sample is still buffered_ms - since_feed ahead of the speaker
            delay = (self._live_buffered_ms - since_feed) * LIVE_RATE // 1000
            delay = min(max(delay, 0), LIVE_RING_SAMPLES - chunk_size)
            end = max(chunk_size, self._live_total - delay)
            idx = np.arange(end - chunk_size, end) % LIVE_RING_SAMPLES
            fft = np.abs(np.fft.rfft(self._live_ring[idx] * self._live_window))[1:1 + self._live_fft_len]
            sums = np.add.reduceat(fft, self._live_starts)
            bands = np.where(self._live_widths > 0, sums / np.maximum(self._live_widths, 1), 0.0)
            p95 = float(np.sort(bands)[min(int(len(bands) * 0.95), len(bands) - 1)])
            dt = self._live_since_update.elapsed() / 1000.0 if self._live_since_update.isValid() else 0.0
            self._live_ref = max(p95, LIVE_REF_FLOOR, self._live_ref * math.exp(-dt / LIVE_RELEASE_SEC))
            ref = max(LIVE_REF_FLOOR, self._live_ref * LIVE_REF_SCALE)
            new_levels = (np.minimum(1.0, bands / ref) ** 0.6 * 8).astype(np.uint8).tolist()
        self._live_since_update.start()
        if new_levels != self._current_levels:
            self._current_levels = new_levels
            self.fftDataChanged.emit(new_levels)
