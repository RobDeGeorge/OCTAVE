#!/usr/bin/env python3
"""Write a realistic mock drive into OCTAVE's black box so the Trip History
page has something to show while designing the UI.

    python tools/trips/make_mock_trip.py                 # into the real history dir
    python tools/trips/make_mock_trip.py --history DIR   # e.g. a sandbox XDG_CONFIG_HOME copy
    python tools/trips/make_mock_trip.py --minutes 45 --start "2026-10-20 08:12"

Close OCTAVE first. The script inserts the trip, its samples and events into
<history>/blackbox.sqlite the same way TripStore does, but leaves it open with
a `trip_end` event; on the next start TripStore::recoverOpenTrips() closes it
cleanly with the real summariser and writes the CSV + JSON pair under
<history>/trips/, so the result is byte-for-byte what a recorded drive
produces. Run it again for another drive (each gets a different start time).

The drive: a cold start, a stop-and-go city stretch, a highway cruise, a short
adapter dropout, and the return, with coolant / oil warming up, voltage dipping
at idle, fuel trending down and the usual trims, loads and temperatures.
"""
from __future__ import annotations

import argparse
import datetime as dt
import math
import os
import random
import sqlite3
import sys
from pathlib import Path

SCHEMA = [
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE IF NOT EXISTS trips("
    "id INTEGER PRIMARY KEY, start_ms INTEGER NOT NULL, end_ms INTEGER, "
    "clean INTEGER NOT NULL DEFAULT 0, vin TEXT, make TEXT, model_year INTEGER, "
    "sample_count INTEGER NOT NULL DEFAULT 0, summary TEXT, file_base TEXT)",
    "CREATE TABLE IF NOT EXISTS params(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)",
    "CREATE TABLE IF NOT EXISTS samples("
    "trip_id INTEGER NOT NULL, ts_ms INTEGER NOT NULL, param_id INTEGER NOT NULL, value REAL NOT NULL)",
    "CREATE INDEX IF NOT EXISTS samples_trip_param_ts ON samples(trip_id, param_id, ts_ms)",
    "CREATE TABLE IF NOT EXISTS events("
    "trip_id INTEGER NOT NULL, ts_ms INTEGER NOT NULL, kind TEXT NOT NULL, detail TEXT)",
    "CREATE INDEX IF NOT EXISTS events_trip ON events(trip_id, ts_ms)",
    "INSERT OR IGNORE INTO meta(key, value) VALUES('schema', '1')",
]


def default_history_dir() -> Path:
    if sys.platform.startswith("win"):
        base = Path(os.environ.get("APPDATA", Path.home()))
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return base / "OCTAVE" / "history"


def speed_profile(minutes: float):
    """Target speed (mph) at time t seconds: city, highway, city."""
    total = minutes * 60.0
    city1 = total * 0.3
    hwy_end = total * 0.72

    def target(t: float) -> float:
        if t < 20:
            return 0.0
        if t < city1:
            # stop-and-go: 70 s cycles, lights every other cycle
            phase = (t % 70.0) / 70.0
            cycle = int(t // 70.0)
            if cycle % 2 == 1 and phase > 0.7:
                return 0.0
            return 32.0 * math.sin(math.pi * min(phase / 0.7, 1.0)) ** 0.6
        if t < hwy_end:
            return 65.0 + 4.0 * math.sin(t / 37.0) + 2.0 * math.sin(t / 11.0)
        if t > total - 25:
            return 0.0
        phase = ((t - hwy_end) % 60.0) / 60.0
        return 28.0 * math.sin(math.pi * min(phase / 0.8, 1.0)) ** 0.6

    return target


def build_samples(start_ms: int, minutes: float, seed: int):
    rng = random.Random(seed)
    target = speed_profile(minutes)
    total = int(minutes * 60)
    rows = []          # (ts_ms, param, value)
    last = {}          # param -> (value, ts) for the 5 s hold rule TripRecorder applies

    def put(ts: int, param: str, value: float, decimals: int = 1):
        value = round(value, decimals)
        prev = last.get(param)
        if prev and prev[0] == value and ts - prev[1] < 5000:
            return
        last[param] = (value, ts)
        rows.append((ts, param, value))

    speed = 0.0
    rpm = 820.0
    coolant = 24.0
    oil = 22.0
    fuel = 68.4
    gear_ratios = [3.83, 2.33, 1.44, 1.0, 0.79]
    ambient = 19.0
    baro = 98.2
    dropout = (total * 0.5, total * 0.5 + 9)   # 9 s without the adapter
    for tick in range(total * 4):               # 4 Hz for the fast PIDs
        t = tick / 4.0
        ts = start_ms + int(t * 1000)
        if dropout[0] <= t < dropout[1]:
            continue
        # speed follows the target with inertia
        goal = target(t)
        if goal > speed:
            speed = min(goal, speed + 2.2 * 0.25 + rng.uniform(0, 0.4))
        else:
            speed = max(goal, speed - 3.0 * 0.25 - rng.uniform(0, 0.5))
        throttle = 12.0 if speed < 0.5 and goal < 0.5 else max(8.0, min(92.0, 18 + (goal - speed) * 9 + speed * 0.35))
        # gear from speed; rpm = mph x axle x gear x 336 / tire inches (3.73, 30")
        gear = len(gear_ratios) - 1
        for g in range(len(gear_ratios)):
            if speed < 12 + g * 14:
                gear = g
                break
        if speed < 1.0:
            rpm = 780 + 60 * math.sin(t * 1.3) + rng.uniform(-25, 25)
            if t < 90:
                rpm += 350 * max(0.0, 1 - t / 90)   # cold fast idle
        else:
            rpm = max(900.0, min(4600.0, speed * 3.73 * gear_ratios[gear] * 336 / 30)) + rng.uniform(-30, 30)
        put(ts, "SPEED", speed, 0)
        put(ts, "RPM", rpm, 0)
        put(ts, "THROTTLE_POS", throttle + rng.uniform(-1, 1), 1)
        if tick % 2 == 0:                      # 2 Hz
            load = min(98.0, 14 + throttle * 0.75 + rng.uniform(-2, 2))
            put(ts, "ENGINE_LOAD", load, 1)
            put(ts, "MAF", 2.5 + rpm / 1000 * (1.2 + load / 40) + rng.uniform(-0.3, 0.3), 2)
            put(ts, "INTAKE_PRESSURE", 30 + load * 0.62 + rng.uniform(-1, 1), 0)
            put(ts, "TIMING_ADVANCE", 8 + (rpm - 800) / 180 - load * 0.08 + rng.uniform(-0.5, 0.5), 1)
            put(ts, "O2_B1S1", 0.45 + 0.4 * math.sin(t * 2.1) + rng.uniform(-0.05, 0.05), 3)
        if tick % 4 == 0:                      # 1 Hz
            coolant += (92.0 - coolant) * 0.0022 + (0.004 if speed < 1 else 0)
            oil += (104.0 - oil) * 0.0012
            fuel -= (0.0010 + load * 0.00004) if speed > 0.5 else 0.0003
            volt = 14.25 - (0.35 if speed < 0.5 else 0.0) + rng.uniform(-0.04, 0.04)
            put(ts, "COOLANT_TEMP", coolant, 0)
            put(ts, "OIL_TEMP", oil, 0)
            put(ts, "CONTROL_MODULE_VOLTAGE", volt, 2)
            put(ts, "ELM_VOLTAGE", volt - 0.1, 1)
            put(ts, "FUEL_LEVEL", fuel, 1)
            put(ts, "INTAKE_TEMP", ambient + 9 + min(12, t / 90) + rng.uniform(-0.5, 0.5), 0)
            put(ts, "SHORT_FUEL_TRIM_1", 1.5 + 2.5 * math.sin(t / 7.0) + rng.uniform(-0.8, 0.8), 1)
            put(ts, "COMMANDED_EQUIV_RATIO", 14.7 + 0.3 * math.sin(t / 5.0), 2)
            put(ts, "RUN_TIME", t, 0)
        if tick % 20 == 0:                     # every 5 s: the slow background sweep
            put(ts, "LONG_FUEL_TRIM_1", 2.3 + 0.2 * math.sin(t / 300), 1)
            put(ts, "AMBIANT_AIR_TEMP", ambient + (0.5 if t > total / 2 else 0), 0)
            put(ts, "BAROMETRIC_PRESSURE", baro, 0)
            put(ts, "FUEL_PRESSURE", 379 + rng.uniform(-3, 3), 0)
            put(ts, "CATALYST_TEMP_B1S1", min(620.0, 80 + t * 0.9 + load * 0.8), 0)
            put(ts, "DISTANCE_SINCE_DTC_CLEAR", 1412 + t * speed / 3600 * 1.609, 0)
    events = [
        (start_ms + 400, "mil", "off"),
        (start_ms + 1200, "dtc", "none"),
        (start_ms + int(dropout[0] * 1000), "disconnected", ""),
        (start_ms + int(dropout[1] * 1000), "reconnected", ""),
        (start_ms + int((total - 1) * 1000), "trip_end", "mock drive"),
    ]
    return rows, events


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--history", type=Path, default=default_history_dir(), help="OCTAVE history dir (default: the app's)")
    ap.add_argument("--minutes", type=float, default=28.0, help="drive length (default 28)")
    ap.add_argument("--start", help='local start time "YYYY-MM-DD HH:MM" (default: yesterday 08:12)')
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--vin", default="1J4FA49S03P123456")
    args = ap.parse_args()

    if args.start:
        start = dt.datetime.strptime(args.start, "%Y-%m-%d %H:%M")
    else:
        start = (dt.datetime.now() - dt.timedelta(days=1)).replace(hour=8, minute=12, second=0, microsecond=0)
    start_ms = int(start.timestamp() * 1000)

    args.history.mkdir(parents=True, exist_ok=True)
    (args.history / "trips").mkdir(exist_ok=True)
    db = sqlite3.connect(args.history / "blackbox.sqlite", timeout=5)
    try:
        for sql in SCHEMA:
            db.execute(sql)
        if db.execute("SELECT 1 FROM trips WHERE id = ?", (start_ms,)).fetchone():
            print(f"a trip already starts at {start:%Y-%m-%d %H:%M}; pick another --start", file=sys.stderr)
            return 1
        rows, events = build_samples(start_ms, args.minutes, args.seed)
        db.execute("INSERT INTO trips(id, start_ms, vin, make, model_year) VALUES(?, ?, ?, ?, ?)",
                   (start_ms, start_ms, args.vin, "Jeep", 2003))
        ids = {}
        for _, param, _ in rows:
            if param not in ids:
                db.execute("INSERT OR IGNORE INTO params(name) VALUES(?)", (param,))
                ids[param] = db.execute("SELECT id FROM params WHERE name = ?", (param,)).fetchone()[0]
        db.executemany("INSERT INTO samples(trip_id, ts_ms, param_id, value) VALUES(?, ?, ?, ?)",
                       [(start_ms, ts, ids[p], v) for ts, p, v in rows])
        db.execute("UPDATE trips SET sample_count = ? WHERE id = ?", (len(rows), start_ms))
        db.executemany("INSERT INTO events(trip_id, ts_ms, kind, detail) VALUES(?, ?, ?, ?)",
                       [(start_ms, ts, k, d) for ts, k, d in events])
        db.commit()
    finally:
        db.close()
    print(f"mock drive {start:%Y-%m-%d %H:%M}, {args.minutes:g} min, {len(rows)} samples over {len(ids)} PIDs "
          f"-> {args.history / 'blackbox.sqlite'}")
    print("Start OCTAVE: it closes the trip with its own summariser and writes the CSV + JSON under trips/.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
