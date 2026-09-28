# First 3D View visit from the Sensor Home tile freezes ~1.2 s

**Status:** deferred (found in 0.9.4 phone testing, not a regression)
**Last updated:** 2026-09-27

## Problem

On the Galaxy S22 (0.9.4 rc2), the first time the 3D Jeep (CarMenu) is opened from the "3D View" tile on Sensor Home, the screen stops updating for ~1.2 s, then there's another ~0.37 s gap before the Jeep appears. The second visit is smooth (max frame gap 21 ms).

Page pre-building (`pagePrewarmTimer` in `frontend/Main.qml`) pre-builds and warm-renders the Sensor button's target: `lastSensorPage`, or SensorHome when that's empty. On a fresh install `lastSensorPage` is empty, so SensorHome is pre-built but CarMenu isn't. The tile then builds CarMenu cold through `stackView.openPage()` (`SensorHome.qml` `pushSubpage()`), paying the Quick3D scene build plus its first GPU draw on the tap. Once CarMenu has been visited it becomes `lastSensorPage` and is pre-built on later launches, so this only hits until then.

## Why it's parked

It's a one-off per install, predates 0.9.4, and pre-building fixes it for returning users. Pre-building CarMenu unconditionally would cost startup time and memory on devices where nobody opens the 3D view.

## Options

1. When SensorHome is the pre-build target and Quick3D is available (SensorHome already probes it via `Qt.createComponent("CarMenu.qml", PreferSynchronous)`), also queue CarMenu last in the pre-build queue.
2. Or pre-build CarMenu lazily the first time SensorHome becomes active (`StackView.onActivated`), since the user is then one tap away.

Option 2 is cheaper for users who never open Sensors. Either needs a Pi CPU check (roxy session): a hidden CarMenu has been measured at ~+1 point idle, which is fine.

Delete this file when done.
