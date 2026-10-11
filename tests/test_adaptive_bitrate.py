"""Adaptive phone-mirror bit rate (backend/phone_mirror/adaptive_bitrate.py,
twin of src/phone_mirror/adaptivebitrate.h): simulated packet streams."""

from backend.phone_mirror.adaptive_bitrate import (
    AdaptiveBitRate, DISCONTINUITY_US, WARMUP_US,
)

MBPS = 1_000_000
FPS = 60
FRAME_US = 1_000_000 // FPS
OFFSET_US = 123_456_789   # clocks of phone and OCTAVE never agree


def feed(abr, seconds, start_us=0, queue_us=lambda t: 0, fill=1.0):
    """Packets at FPS for `seconds`, each sized to use `fill` of the current
    target, arriving `queue_us(t)` late. Returns (end_us, sent rates)."""
    sent = []
    t = start_us
    end = start_us + int(seconds * 1_000_000)
    while t < end:
        size = int(abr.target * fill / 8 / FPS)
        r = abr.on_packet(t, t + OFFSET_US + 5_000 + queue_us(t), size)
        if r:
            sent.append(r)
        t += FRAME_US
    return t, sent


def test_clear_link_climbs_to_max():
    abr = AdaptiveBitRate(2 * MBPS, 16 * MBPS, 6 * MBPS)
    assert abr.take_initial() == 6 * MBPS
    _, sent = feed(abr, 40)
    assert abr.target == 16 * MBPS
    assert sent == sorted(sent)          # only ever went up
    assert sent[-1] == 16 * MBPS


def test_nothing_changes_during_warmup():
    abr = AdaptiveBitRate(2 * MBPS, 16 * MBPS, 8 * MBPS)
    abr.take_initial()
    _, sent = feed(abr, WARMUP_US / 1e6 - 0.1, queue_us=lambda t: 500_000)
    assert sent == []


def test_congestion_cuts_the_rate_and_holds_it():
    abr = AdaptiveBitRate(2 * MBPS, 16 * MBPS, 16 * MBPS)
    abr.take_initial()
    t, _ = feed(abr, 5)                                  # healthy baseline
    # Then a growing backlog (the link carries ~half of what is sent)
    t0 = t
    t, sent = feed(abr, 6, start_us=t, queue_us=lambda x: (x - t0) // 2)
    assert sent and all(b < a for a, b in zip([16 * MBPS] + sent[:-1], sent, strict=True))
    assert abr.target <= int(16 * MBPS * 0.7 * 0.7)
    low = abr.target
    # Link recovers: no raise inside the hold window right after the last cut
    _, sent = feed(abr, 3, start_us=t)
    assert abr.target == low


def test_never_below_min():
    abr = AdaptiveBitRate(2 * MBPS, 16 * MBPS, 3 * MBPS)
    abr.take_initial()
    t, _ = feed(abr, 3)
    t0 = t
    feed(abr, 30, start_us=t, queue_us=lambda x: (x - t0) // 2 % (DISCONTINUITY_US // 2))
    assert abr.target == 2 * MBPS


def test_still_screen_does_not_raise():
    # The phone sends tiny repeat frames: a clear link proves nothing about capacity
    abr = AdaptiveBitRate(2 * MBPS, 16 * MBPS, 6 * MBPS)
    abr.take_initial()
    _, sent = feed(abr, 30, fill=0.05)
    assert sent == []
    assert abr.target == 6 * MBPS


def test_single_key_frame_spike_is_not_congestion():
    abr = AdaptiveBitRate(2 * MBPS, 16 * MBPS, 16 * MBPS)
    abr.take_initial()
    # One late packet per second (a key frame), the rest on time
    _, sent = feed(abr, 20, queue_us=lambda x: 400_000 if x % 1_000_000 < FRAME_US else 0)
    assert sent == []
    assert abr.target == 16 * MBPS


def test_clock_jump_resets_baseline():
    abr = AdaptiveBitRate(2 * MBPS, 16 * MBPS, 16 * MBPS)
    abr.take_initial()
    t, _ = feed(abr, 5)
    # Phone clock steps back 10 s (delay jumps up): not a queue
    _, sent = feed(abr, 10, start_us=t, queue_us=lambda x: 10_000_000)
    assert sent == []
    assert abr.target == 16 * MBPS
