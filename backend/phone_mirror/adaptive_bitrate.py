"""
Adaptive video bit rate for phone mirroring (setting scrcpyBitRateAuto).

Mirrors src/phone_mirror/adaptivebitrate.h; tests in tests/test_adaptive_bitrate.py.

Every video packet carries the phone's presentation time. Arrival time on our
clock minus that is the one-way delay plus an unknown clock offset; the
smallest delay seen over the last BASELINE_WINDOWS seconds stands in for "link
idle", so anything above it is queueing: the link (Wi-Fi, adb over USB) or our
decoder is not keeping up. Each one-second window is judged on its mean
queueing delay (the single worst packet left out, so a key frame does not
count as congestion):
  - over CONGESTED_US: cut the rate by DECREASE (at most every DROP_GAP_US)
  - under CLEAR_US for GOOD_WINDOWS_TO_RAISE windows in a row,
    HOLD_AFTER_DROP_US after the last cut, and the stream actually using at
    least APP_LIMITED_FRACTION of the rate (a still screen proves nothing):
    raise it by INCREASE_FRACTION, at least INCREASE_MIN_BPS
The result stays within [min, max]; max is the user's bit-rate setting.
"""

from collections import deque

WINDOW_US = 1_000_000
BASELINE_WINDOWS = 10
WARMUP_US = 2_000_000
CONGESTED_US = 150_000
CLEAR_US = 40_000
DISCONTINUITY_US = 3_000_000   # a clock jump, not a queue
DECREASE = 0.7
INCREASE_FRACTION = 0.1
INCREASE_MIN_BPS = 1_000_000
GOOD_WINDOWS_TO_RAISE = 2
HOLD_AFTER_DROP_US = 5_000_000
DROP_GAP_US = 2_000_000
APP_LIMITED_FRACTION = 0.5
MIN_CHANGE_FRACTION = 0.05


class AdaptiveBitRate:
    def __init__(self, min_bps: int, max_bps: int, start_bps: int):
        self._min = min_bps
        self._max = max(min_bps, max_bps)
        self._target = min(self._max, max(self._min, start_bps))
        self._sent = 0
        self._start_us = None
        self._window_start_us = 0
        self._window_min = None
        self._history: deque = deque(maxlen=BASELINE_WINDOWS)   # per-window minimum delay
        self._sum = 0
        self._worst = 0
        self._count = 0
        self._bytes = 0
        self._good = 0
        self._last_drop_us = 0
        self.last_queue_us = 0   # last mean queueing delay judged, for logging

    @property
    def target(self) -> int:
        return self._target

    def take_initial(self) -> int:
        """The rate to announce once at the start of a stream (the phone may
        still run a rate from an earlier session)."""
        self._sent = self._target
        return self._target

    def on_packet(self, pts_us: int, arrival_us: int, size: int) -> int:
        """Feed every media packet: phone presentation time and local arrival
        time (both µs, any origin), payload size. Returns the new rate in bps
        when it changed enough to send to the phone, 0 otherwise."""
        delay = arrival_us - pts_us
        if self._start_us is None:
            self._start_us = arrival_us
            self._window_start_us = arrival_us
            self._last_drop_us = -(1 << 62)
        self._window_min = delay if self._window_min is None else min(self._window_min, delay)
        baseline = min([self._window_min, *self._history])
        queue = delay - baseline
        if queue > DISCONTINUITY_US:
            # The phone's clock (or ours) jumped: start the baseline over
            self._history.clear()
            self._window_min = delay
            queue = 0
        self._sum += queue
        self._worst = max(self._worst, queue)
        self._count += 1
        self._bytes += size
        if arrival_us - self._window_start_us < WINDOW_US:
            return 0
        changed = self._evaluate(arrival_us)
        self._history.append(self._window_min)
        self._window_min = None
        self._window_start_us = arrival_us
        self._sum = self._worst = self._count = self._bytes = 0
        return changed

    def _evaluate(self, now_us: int) -> int:
        if now_us - self._start_us < WARMUP_US or self._count == 0:
            return 0
        drop_worst = self._count >= 3
        mean_queue = int((self._sum - (self._worst if drop_worst else 0))
                         / (self._count - (1 if drop_worst else 0)))
        self.last_queue_us = mean_queue
        seconds = (now_us - self._window_start_us) / 1e6
        used_bps = self._bytes * 8.0 / seconds if seconds > 0 else 0.0
        if mean_queue > CONGESTED_US:
            self._good = 0
            if now_us - self._last_drop_us >= DROP_GAP_US:
                self._target = max(self._min, int(self._target * DECREASE))
                self._last_drop_us = now_us
        elif mean_queue < CLEAR_US:
            self._good += 1
            if (self._good >= GOOD_WINDOWS_TO_RAISE and now_us - self._last_drop_us >= HOLD_AFTER_DROP_US
                    and used_bps >= APP_LIMITED_FRACTION * self._target):
                self._target = min(self._max, self._target + max(INCREASE_MIN_BPS,
                                                                 int(self._target * INCREASE_FRACTION)))
                self._good = 0
        else:
            self._good = 0
        if self._target == self._sent:
            return 0
        at_bound = self._target in (self._min, self._max)
        if not at_bound and abs(self._target - self._sent) < MIN_CHANGE_FRACTION * self._sent:
            return 0
        self._sent = self._target
        return self._target
