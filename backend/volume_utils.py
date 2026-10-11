"""
Shared volume curve + dispatcher for all audio outputs.

OCTAVE maps the UI volume percent (0–100) to a linear audio volume (0.0–1.0)
through a quadratic curve (y = x²). This gives a more natural perceptual
response than linear at low volumes. Every audio output in the app must use
this same curve or they will drift out of sync when the user moves the slider.

Before this module existed, the curve was hardcoded in ~9 call sites across
main.py and QML. If the curve ever changed, every site had to change in
lockstep. This module centralizes both the math and the dispatch.

Call sites should use VolumeController.applyVolume() whenever possible —
that's the single entry point that keeps settings, local media, phone
mirror, and ESP32 feedback all in sync.
"""

from PySide6.QtCore import QObject, Slot


def to_linear(percent) -> float:
    """Convert a 0-100 UI volume percent to a 0.0-1.0 linear volume.

    Clamps out-of-range inputs. Accepts int or float for convenience.
    """
    pct = max(0.0, min(100.0, float(percent)))
    return (pct / 100.0) ** 2.0


class VolumeController(QObject):
    """Single entry point for applying a UI volume percent to every output.

    Registered in main.py as a QML context property so both Python hardware
    handlers (gesture sensor, ESP32 knob) and QML slider widgets can funnel
    volume changes through one method. Any output may be None on platforms
    where it doesn't exist (e.g. phone mirror is desktop-only).
    """

    def __init__(
        self,
        settings_manager,
        media_manager,
        phone_mirror_manager=None,
        esp32_manager=None,
        parent=None,
    ):
        super().__init__(parent)
        self._settings = settings_manager
        self._media = media_manager
        self._phone_mirror = phone_mirror_manager
        self._esp32 = esp32_manager

    @Slot(int)
    def applyVolume(self, percent: int) -> None:
        """Apply a volume percent (0-100) to every connected audio output.

        - settings_manager stores the raw percent (idempotent if unchanged)
        - media_manager and phone_mirror_manager receive the linear value
        - esp32_manager receives the raw percent
        """
        percent = max(0, min(100, int(percent)))
        linear = to_linear(percent)

        if self._settings is not None:
            self._settings.setCurrentVolume(percent)
        if self._media is not None:
            self._media.setVolume(linear)
        if self._phone_mirror is not None:
            self._phone_mirror.setVolume(linear)
        if self._esp32 is not None:
            self._esp32.send_volume_update(percent)

    @Slot(float, result=float)
    def toLinear(self, percent: float) -> float:
        """Pure math helper for QML — exposes the curve without dispatching."""
        return to_linear(percent)
