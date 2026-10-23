"""Pilot tests for the console against a running OCTAVE service.

    OCTAVE_PORT=19830 pytest frontends/console/tests      # default port 9876

They skip when nothing answers on the port. Use a throwaway instance with the
simulated car (`octave --dev --profile`, or octave-svc): the tests press
buttons that change settings, clear trouble codes and drive the player, and
restore what they change. Set OCTAVE_SHOTS=DIR to keep SVG screenshots."""

import asyncio
import json
import os
import socket
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from octave_console.app import OctaveConsole  # noqa: E402
from octave_console.link import Client  # noqa: E402
from octave_console.panes import parse_command  # noqa: E402
from textual.widgets import DataTable, Input, RichLog  # noqa: E402

HOST = os.environ.get("OCTAVE_HOST", "127.0.0.1")
PORT = int(os.environ.get("OCTAVE_PORT", "9876"))
SHOTS = os.environ.get("OCTAVE_SHOTS")
SIZE = (170, 48)


def service_up():
    try:
        socket.create_connection((HOST, PORT), timeout=1).close()
        return True
    except OSError:
        return False


live = pytest.mark.skipif(not service_up(), reason=f"no OCTAVE service on {HOST}:{PORT}")


@pytest.fixture
def sdk():
    with Client(HOST, PORT) as client:
        yield client


async def wait_for(pilot, cond, timeout=8.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if cond():
            return True
        await pilot.pause(0.1)
    return cond()


async def sdk_wait(cond, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if cond():
            return True
        await asyncio.sleep(0.1)
    return cond()


def sim(action, **args):
    """A dev-only simulator action (--dev --profile builds): makes the values move."""
    with socket.create_connection((HOST, PORT), timeout=2) as s:
        s.sendall((json.dumps({"action": action, "args": args}) + "\n").encode())
        s.recv(4096)


def shot(app, name):
    if SHOTS:
        Path(SHOTS).mkdir(parents=True, exist_ok=True)
        app.save_screenshot(f"{name}.svg", path=SHOTS)


async def linked(pilot, app):
    assert await wait_for(pilot, lambda: app.connected and app.query_one("#pane-live").order), "never linked"


# ── offline ──────────────────────────────────────────────────────────────


def test_parse_command():
    assert parse_command("call volumeController applyVolume 40") == (
        "call", {"object": "volumeController", "method": "applyVolume", "args": [40]})
    assert parse_command("set volumeController muted true") == (
        "set", {"object": "volumeController", "property": "muted", "value": True})
    assert parse_command('call obdManager setParameterDemand x \'["RPM"]\'')[1]["args"] == ["x", ["RPM"]]
    assert parse_command("rows downloadManager downloadQueue 0 5")[1] == {
        "object": "downloadManager", "property": "downloadQueue", "from": 0, "count": 5}
    with pytest.raises(ValueError):
        parse_command("frobnicate x")


async def test_no_service_shows_no_link():
    app = OctaveConsole(HOST, 1)   # nothing listens on port 1
    async with app.run_test(size=SIZE) as pilot:
        await pilot.pause(0.5)
        assert not app.connected
        assert "NO LINK" in str(app.query_one("#status").render())


# ── live ─────────────────────────────────────────────────────────────────


@live
async def test_live_table_values_sort_pin():
    app = OctaveConsole(HOST, PORT)
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-live")
        assert "RPM" in pane.order
        assert await wait_for(pilot, lambda: "RPM" in pane.values)
        # demand follows the visible rows
        assert await wait_for(pilot, lambda: pane.sent_demand)
        assert set(pane.sent_demand) <= set(pane.order)
        # sort by value, descending
        await pilot.click("#live-table")
        while pane.sort_key != "value":
            await pilot.press("s")
        await pilot.press("r")
        assert pane.sort_key == "value" and pane.reverse
        pane.rebuild()   # values keep moving; check one consistent sort (no await before the check)
        nums = [v for v in (pane.to_display(p, pane.values.get(p)) for p in pane.order) if isinstance(v, (int, float))]
        assert nums[:5] == sorted(nums, reverse=True)[:5]
        # pin the top row and RPM
        await pilot.press("p")
        pane.table.move_cursor(row=pane.order.index("RPM"))
        await pilot.press("p")
        assert "RPM" in pane.pins and len(pane.pins) == 2
        assert set(pane.pins) <= set(pane.sent_demand or []) or await wait_for(
            pilot, lambda: set(pane.pins) <= set(pane.sent_demand or []))
        sim("sim_throttle", value=70)
        await pilot.pause(2.5)
        sim("sim_throttle", value=0)
        assert len(set(pane.history["RPM"])) > 1
        shot(app, "live")
        # leaving the pane drops the demand
        await pilot.press("3")
        assert await wait_for(pilot, lambda: pane.sent_demand == [])


# ── media bar ────────────────────────────────────────────────────────────


@live
async def test_volume_mute_and_transport(sdk):
    app = OctaveConsole(HOST, PORT)
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        sdk.call("volumeController", "applyVolume", 40)
        assert await wait_for(pilot, lambda: app.state_of("volumeController", "percent") == 40)
        await pilot.press("plus")
        assert await sdk_wait(lambda: sdk.get("volumeController", "percent") == 45)
        await pilot.press("minus", "minus")
        assert await sdk_wait(lambda: sdk.get("volumeController", "percent") == 35)
        await pilot.press("m")
        assert await sdk_wait(lambda: sdk.get("volumeController", "muted") is True)
        assert await wait_for(pilot, lambda: "MUTE" in str(app.query_one("#media").render()))
        await pilot.press("m")
        assert await sdk_wait(lambda: sdk.get("volumeController", "muted") is False)

        was = sdk.get("nowPlaying", "playing")
        await pilot.press("space")
        assert await sdk_wait(lambda: sdk.get("nowPlaying", "playing") != was)
        if sdk.get("nowPlaying", "playing"):
            title = sdk.get("nowPlaying", "title")
            assert await wait_for(pilot, lambda: title in str(app.query_one("#media").render()))
            await pilot.press("greater_than_sign")
            assert await sdk_wait(lambda: sdk.get("nowPlaying", "title") != title)
            shot(app, "media")
            await pilot.press("space")
            assert await sdk_wait(lambda: sdk.get("nowPlaying", "playing") is False)


# ── OBD ──────────────────────────────────────────────────────────────────


@live
async def test_obd_codes_confirm_and_freeze(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="obd")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        dtcs = app.query_one("#dtcs", DataTable)
        await pilot.press("t")
        assert await wait_for(pilot, lambda: app.note == "reading trouble codes")
        await pilot.pause(1.0)
        had_codes = bool(sdk.call("obdManager", "get_dtc_codes"))   # the sim's two codes, until a clear
        if had_codes:
            assert await wait_for(pilot, lambda: dtcs.row_count > 0)
        await pilot.press("f")
        assert await wait_for(pilot, lambda: app.note == "reading freeze frame")
        assert await wait_for(pilot, lambda: ("P0" if had_codes else "Freeze frame") in str(app.query_one("#freeze").render()))
        shot(app, "obd")
        # clearing asks first; "n" leaves the codes alone
        await pilot.press("X")
        assert await wait_for(pilot, lambda: type(app.screen).__name__ == "Confirm")
        shot(app, "obd-confirm")
        await pilot.press("n")
        assert await wait_for(pilot, lambda: type(app.screen).__name__ != "Confirm")
        await pilot.pause(0.5)
        assert app.note != "clearing trouble codes"
        assert bool(sdk.call("obdManager", "get_dtc_codes")) == had_codes
        # "y" clears them
        await pilot.press("X")
        await wait_for(pilot, lambda: type(app.screen).__name__ == "Confirm")
        await pilot.press("y")
        assert await wait_for(pilot, lambda: app.note == "clearing trouble codes")
        assert await sdk_wait(lambda: not sdk.call("obdManager", "get_dtc_codes"), timeout=8)
        assert await wait_for(pilot, lambda: dtcs.row_count == 0)
        # the adapters list holds the simulated port
        assert app.query_one("#adapters", DataTable).row_count >= 1


# ── trips ────────────────────────────────────────────────────────────────


@live
async def test_trips_list_and_detail():
    app = OctaveConsole(HOST, PORT, start_tab="trips")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-trips")
        assert await wait_for(pilot, lambda: pane.ids)
        await pilot.click("#trip-list")
        await pilot.press("enter")
        assert await wait_for(pilot, lambda: f"Trip {pane.ids[0]}" in str(app.query_one("#trip-detail").render()))
        shot(app, "trips")


# ── settings ─────────────────────────────────────────────────────────────


@live
async def test_settings_toggle_and_edit(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="settings")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-settings")
        assert await wait_for(pilot, lambda: len(pane.props) > 50 and "showClock" in app.state.get("settingsManager", {}))
        # a bool flips in place
        before = sdk.get("settingsManager", "showClock")
        await pilot.click("#settings-filter")
        await pilot.press(*"showclock", "enter")
        assert pane.shown_names == ["showClock"]
        await pilot.press("enter")
        assert await sdk_wait(lambda: sdk.get("settingsManager", "showClock") is (not before))
        await pilot.press("enter")
        assert await sdk_wait(lambda: sdk.get("settingsManager", "showClock") is before)
        # a number through the editor
        size = sdk.get("settingsManager", "clockSize")
        app.query_one("#settings-filter", Input).value = "clockSize"
        await pilot.pause(0.2)
        pane.table.move_cursor(row=pane.shown_names.index("clockSize"))
        pane.table.focus()
        await pilot.press("enter")
        assert await wait_for(pilot, lambda: type(app.screen).__name__ == "EditValue")
        edit = app.screen.query_one(Input)
        edit.value = str(size + 2)
        shot(app, "settings-edit")
        await pilot.press("enter")
        assert await sdk_wait(lambda: sdk.get("settingsManager", "clockSize") == size + 2)
        assert await wait_for(pilot, lambda: str(pane.table.get_cell("clockSize", "value")) == str(size + 2))
        sdk.call("settingsManager", "save_clock_size", size)


# ── API explorer and palette ─────────────────────────────────────────────


@live
async def test_api_explorer(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="api")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        out = app.query_one("#api-out", RichLog)
        await pilot.click("#api-cmd")
        await pilot.press(*"get obdManager vin", "enter")
        vin = sdk.get("obdManager", "vin")
        assert await wait_for(pilot, lambda: any(vin in str(line.text) for line in out.lines))
        app.query_one("#api-cmd", Input).value = "call volumeController applyVolume 22"
        await pilot.press("enter")
        assert await sdk_wait(lambda: sdk.get("volumeController", "percent") == 22)
        shot(app, "api")


@live
async def test_palette_runs_a_command():
    app = OctaveConsole(HOST, PORT, start_tab="obd")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        await pilot.press("ctrl+p")
        await pilot.pause(0.3)
        await pilot.press(*"read trouble")
        await pilot.pause(0.5)
        shot(app, "palette")
        await pilot.press("enter")
        assert await wait_for(pilot, lambda: app.note == "reading codes")   # the call returned


@live
async def test_downloads_pane():
    app = OctaveConsole(HOST, PORT, start_tab="downloads")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        status = app.query_one("#dl-status")
        assert await wait_for(pilot, lambda: "active" in str(status.render()) or "not available" in str(status.render()))
        assert app.query_one("#dl-results", DataTable).row_count == len(app.query_one("#pane-downloads").results.rows)
        shot(app, "downloads")
