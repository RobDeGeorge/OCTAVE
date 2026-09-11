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

        # Skip if already analyzing
        if self._analyzing:
            logger.info("Analysis already in progress, skipping")
            return

        self._current_file = file_path
        self._analyzing = True

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

            # Decode all audio frames
            samples = []
            sample_rate = audio_stream.rate or 44100

            for frame in container.decode(audio_stream):
                frame_data = frame.to_ndarray()

                # If stereo, convert to mono by averaging channels
                if len(frame_data.shape) > 1:
                    frame_data = frame_data.mean(axis=0)

                samples.append(frame_data)

            container.close()

            if not samples:
                logger.debug("No audio samples decoded")
                return None

            # Concatenate all samples
            all_samples = np.concatenate(samples).astype(np.float32)

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

            # Pre-compute window function once
            window = np.hanning(chunk_size)

            # Magnitudes of the positive frequencies, skipping DC: rfft bins
            # 1..N/2, i.e. 0-4 kHz at the 8 kHz effective rate (the C++
            # backend keeps the same range). The band edges depend only on
            # the bin count, so compute them once.
            fft_len = chunk_size // 2
            log_indices = self._log_band_edges(fft_len, self._num_bars)

            # First pass: collect raw FFT magnitudes
            raw_fft_data = []
            for i in range(num_chunks):
                chunk = all_samples[i * chunk_size:(i + 1) * chunk_size]

                # Apply window function to reduce spectral leakage
                windowed = chunk * window

                # Compute FFT
                fft = np.abs(np.fft.rfft(windowed))[1:1 + fft_len]  # Skip DC

                if len(fft) == 0:
                    raw_fft_data.append([0.0] * self._num_bars)
                    continue

                levels = []
                for j in range(self._num_bars):
                    start_idx = log_indices[j]
                    end_idx = log_indices[j + 1]
                    if end_idx > start_idx:
                        band = fft[start_idx:end_idx]
                        levels.append(np.mean(band))
                    else:
                        levels.append(0.0)

                raw_fft_data.append(levels)

            # Find global max for normalization (use 95th percentile to avoid outliers)
            all_values = [v for chunk in raw_fft_data for v in chunk if v > 0]
            if all_values:
                global_max = np.percentile(all_values, 95)
            else:
                global_max = 1.0

            # Second pass: normalize and map to 0-8 range
            fft_data = []
            for levels in raw_fft_data:
                normalized = []
                for level in levels:
                    if global_max > 0:
                        norm = level / global_max
                        norm = min(1.0, norm) ** 0.6
                        normalized.append(int(norm * 8))
                    else:
                        normalized.append(0)
                fft_data.append(normalized)

            return fft_data

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

