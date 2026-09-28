"""
Audio Analyzer for Waveform Visualization
Analyzes audio files using FFT to generate real-time waveform visualization data.
"""

from PySide6.QtCore import QObject, Signal, Slot, QTimer
from concurrent.futures import ThreadPoolExecutor
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

        # FFT data storage - list of lists, each inner list is FFT levels for a time chunk
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
        if file_path == self._current_file and self._fft_data:
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
        Returns the computed FFT data list, or None on failure.
        """
        try:
            container = av.open(file_path)
            audio_stream = next((s for s in container.streams if s.type == 'audio'), None)

            if not audio_stream:
                logger.debug("No audio stream found")
                return None

            # Decode all audio frames. Kept to one array per frame here; the
            # mono mix, normalisation and FFT below each run as a single
            # numpy call over the whole track. This runs on a worker thread,
            # but Python-level loops (and numpy calls fed Python lists) hold
            # the GIL, and every Python call the GUI thread makes during a
            # track skip then waits for it: per-chunk / per-bar loops here
            # used to stall the UI by 100-350 ms on each skip.
            frames = []
            sample_rate = audio_stream.rate or 44100

            for frame in container.decode(audio_stream):
                frames.append(frame.to_ndarray())

            container.close()

            if not frames:
                logger.debug("No audio samples decoded")
                return None

            # Planar frames are (channels, n); packed ones are (1, n*channels)
            # or 1-D. If stereo, convert to mono by averaging channels.
            if frames[0].ndim > 1 and frames[0].shape[0] > 1:
                all_samples = np.concatenate(frames, axis=1).mean(axis=0)
            else:
                all_samples = np.concatenate([f.reshape(-1) for f in frames])
            all_samples = all_samples.astype(np.float32)

            # Normalize samples
            max_val = np.max(np.abs(all_samples))
            if max_val > 0:
                all_samples = all_samples / max_val

            # Downsample to ~8kHz for faster processing
            downsample_factor = max(1, sample_rate // 8000)
            all_samples = all_samples[::downsample_factor]
            effective_rate = sample_rate // downsample_factor

            # Compute FFT for time chunks
            chunk_size = int(effective_rate * self._chunk_duration)
            num_chunks = len(all_samples) // chunk_size

            if num_chunks == 0:
                logger.debug("Audio too short for analysis")
                return None

            # Magnitudes of the positive frequencies, skipping DC: rfft bins
            # 1..N/2, i.e. 0-4 kHz at the 8 kHz effective rate (the C++
            # backend keeps the same range). The band edges depend only on
            # the bin count, so compute them once.
            fft_len = chunk_size // 2
            if fft_len == 0:
                return [[0] * self._num_bars for _ in range(num_chunks)]
            edges = np.asarray(self._log_band_edges(fft_len, self._num_bars))

            # All chunks at once: (num_chunks, chunk_size), Hann-windowed to
            # reduce spectral leakage, then one rfft along the chunk axis.
            chunks = all_samples[:num_chunks * chunk_size].reshape(num_chunks, chunk_size)
            chunks = chunks * np.hanning(chunk_size)
            fft = np.abs(np.fft.rfft(chunks, axis=1))[:, 1:1 + fft_len]

            # Mean magnitude per log band; an empty band (only possible when
            # there are fewer bins than bars) is 0.
            widths = np.diff(edges)
            starts = np.minimum(edges[:-1], fft_len - 1)
            sums = np.add.reduceat(fft, starts, axis=1)
            raw = np.where(widths > 0, sums / np.maximum(widths, 1), 0.0)

            # Global max for normalization (95th percentile to avoid outliers)
            positive = raw[raw > 0]
            global_max = np.percentile(positive, 95) if positive.size else 1.0

            # Normalize and map to the 0-8 range
            if global_max > 0:
                levels = (np.minimum(1.0, raw / global_max) ** 0.6 * 8).astype(int)
            else:
                levels = np.zeros(raw.shape, dtype=int)
            return levels.tolist()

        except Exception as e:
            logger.error(f"FFT analysis error: {e}")
            return None

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
        if not self._fft_data or not self._is_active:
            return

        chunk_index = int(position_seconds * 10)
        chunk_index = max(0, min(chunk_index, len(self._fft_data) - 1))

        new_levels = self._fft_data[chunk_index]

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

