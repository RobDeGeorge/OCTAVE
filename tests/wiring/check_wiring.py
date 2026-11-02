"""AppCore's cross-manager wiring (src/appcore.cpp), checked on the real
binary: `octave --headless --sim` on a free port with a throwaway config
(OCTAVE_BIN names the binary), driven and observed over the service API.

Each test pulls one end of a connect() in AppCore and looks at the other:

  volumeController.applyVolume  -> every output's volume (mediaManager here), saved
  volumeController mute rules   -> mediaManager muted; a raise unmutes
  obdManager.rpmChanged         -> shiftLightManager.rpm
  obdManager.parameterSampled   -> paramStore, and the black box (tripRecorder)
  gauge unit settings           -> parameterCatalog's display units
  a track change                -> the playback state saved in settings
  nowPlaying                    -> audioAnalyzer playing / demand

A QtTest that builds AppCore itself waits for the core library
(TODO/open-backend.md); until then this is the wiring test. ctest runs it as
`wiring`.
"""
import math
import os
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "sdk", "python"))

from octave_client import Client  # noqa: E402
from octave_client.launch import launch_headless  # noqa: E402


def wait_for(predicate, timeout=20.0, step=0.1):
    """predicate() until truthy (returned) or the timeout (None)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return None


class WiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = os.environ.get("OCTAVE_BIN", os.path.join(REPO, "build", "octave"))
        if not os.path.exists(binary):
            raise unittest.SkipTest("no OCTAVE_BIN")
        cls.app = launch_headless(binary=binary, extra=["--sim"], ready_timeout=90)
        cls.c = Client(port=cls.app.port)

    @classmethod
    def tearDownClass(cls):
        cls.c.close()
        cls.app.stop()

    def get(self, obj, prop):
        return self.c.get(obj, prop)

    # -- volume and mute (AppCore::wireVolume) --------------------------------

    def test_volume_reaches_the_outputs_on_the_curve_and_is_saved(self):
        c = self.c
        for percent in (75, 30, 0, 100):
            c.call("volumeController", "applyVolume", percent)
            expected = (percent / 100.0) ** 2.0   # the quadratic curve (volumecontroller.cpp)
            got = wait_for(lambda e=expected: math.isclose(c.call("mediaManager", "getVolume"), e, abs_tol=1e-3),
                           timeout=5)
            self.assertTrue(got, f"mediaManager volume for {percent} %: "
                                      f"{c.call('mediaManager', 'getVolume')}, expected {expected}")
            self.assertEqual(self.get("volumeController", "percent"), percent)
            self.assertTrue(wait_for(lambda p=percent: self.get("settingsManager", "currentVolume") == p, timeout=5),
                            f"currentVolume not saved as {percent}")
            self.assertAlmostEqual(c.call("volumeController", "toLinear", percent), expected, places=3)

    def test_mute_reaches_the_outputs_and_a_raise_unmutes(self):
        c = self.c
        c.call("volumeController", "applyVolume", 40)
        if self.get("volumeController", "muted"):
            c.call("volumeController", "toggleMute")
        c.call("volumeController", "toggleMute")
        self.assertTrue(self.get("volumeController", "muted"))
        self.assertTrue(wait_for(lambda: c.call("mediaManager", "is_muted"), timeout=5), "mediaManager not muted")
        # Raising the volume while muted unmutes, from any input
        c.call("volumeController", "applyVolume", 45)
        self.assertFalse(self.get("volumeController", "muted"))
        self.assertTrue(wait_for(lambda: not c.call("mediaManager", "is_muted"), timeout=5), "mediaManager still muted")

    # -- the car (shift light, value store, black box) -------------------------

    def drive(self):
        self.c.call("sim", "setEngineOn", True)
        self.c.call("sim", "setThrottle", 40)
        rpm = wait_for(lambda: (v := self.c.get("paramStore", "RPM")) and v > 0 and v, timeout=60)
        self.assertIsNotNone(rpm, "no RPM reached paramStore from the simulated car")
        return rpm

    def test_rpm_reaches_the_shift_light(self):
        self.drive()
        # Both follow the same readings; the shift light holds the latest one
        self.assertTrue(wait_for(lambda: self.get("shiftLightManager", "rpm") > 0, timeout=20),
                        "shiftLightManager.rpm stayed 0 while the car runs")
        store = self.get("paramStore", "RPM")
        light = self.get("shiftLightManager", "rpm")
        self.assertLess(abs(store - light), 1500, f"paramStore RPM {store} vs shift light {light}")

    def test_readings_reach_the_black_box(self):
        self.drive()
        self.assertTrue(wait_for(lambda: self.get("tripRecorder", "storeReady"), timeout=20), "black box not ready")
        self.assertTrue(wait_for(lambda: self.get("tripRecorder", "recording"), timeout=30),
                        "no trip started though RPM > 0 reached paramStore")
        self.assertTrue(wait_for(lambda: self.get("tripRecorder", "currentSampleCount") > 10, timeout=30),
                        "the open trip is not collecting samples")

    def test_more_than_rpm_reaches_the_value_store(self):
        self.drive()
        self.c.call("sim", "setThrottle", 60)
        self.assertTrue(wait_for(lambda: (v := self.c.get("paramStore", "SPEED")) and v > 0, timeout=60),
                        "SPEED never reached paramStore")

    # -- gauge units (AppCore::wireGauges) -------------------------------------

    def test_gauge_unit_settings_reach_the_catalog(self):
        c = self.c
        original = self.get("settingsManager", "gaugeSpeedUnit")
        # info() is the recorded (native) unit; displayParameters what the gauges show

        def unit_of():
            rows = self.get("parameterCatalog", "displayParameters") or []
            return next((r.get("unit") or "" for r in rows if r.get("id") == "SPEED"), "").lower()

        try:
            for unit in ("km/h", "mph"):
                c.call("settingsManager", "save_gauge_speed_unit", unit)
                self.assertTrue(wait_for(lambda u=unit: unit_of() == u, timeout=5),
                                f"catalog SPEED unit {unit_of()!r}, expected {unit!r}")
            # Values follow too: 100 MPH shown in km/h
            c.call("settingsManager", "save_gauge_speed_unit", "km/h")
            wait_for(lambda: unit_of() == "km/h", timeout=5)
            self.assertAlmostEqual(c.call("parameterCatalog", "toDisplay", "SPEED", 100), 160.934, delta=0.1)
        finally:
            c.call("settingsManager", "save_gauge_speed_unit", original)

    # -- music: saved state and the visualizer (AppCore::wireAnalyzer) ---------

    def test_a_track_change_is_saved_and_the_analyzer_follows(self):
        c = self.c
        # Play with nothing loaded starts the first of the simulator's test tracks
        if not wait_for(lambda: self.get("nowPlaying", "hasTrack"), timeout=3):
            c.call("nowPlaying", "togglePlay")
        self.assertTrue(wait_for(lambda: self.get("nowPlaying", "hasTrack"), timeout=30),
                        "the simulator's test tracks never loaded")
        first = c.call("mediaManager", "get_current_file")
        c.call("nowPlaying", "next")
        current = wait_for(lambda: (f := c.call("mediaManager", "get_current_file")) != first and f, timeout=10)
        self.assertTrue(current, "next did not change the track")
        self.assertTrue(wait_for(lambda: c.call("settingsManager", "get_last_played_song") == current, timeout=10),
                        f"saved song {c.call('settingsManager', 'get_last_played_song')!r}, playing {current!r}")
        # Playing state reaches the analyzer (nowPlaying.playingChanged -> setPlaying)
        if not self.get("nowPlaying", "playing"):
            c.call("nowPlaying", "togglePlay")
        self.assertTrue(wait_for(lambda: self.get("nowPlaying", "playing"), timeout=10), "not playing")
        c.call("audioAnalyzer", "setDemand", "wiring-test", True)
        try:
            self.assertTrue(wait_for(lambda: any(v > 0 for v in (self.get("audioAnalyzer", "levels") or [])),
                                     timeout=20),
                            "the visualizer shows nothing while a track plays")
        finally:
            c.call("audioAnalyzer", "setDemand", "wiring-test", False)


if __name__ == "__main__":
    unittest.main()
