"""Pilot tests for the panes that bring the console to control parity:
Library, Dashboards, Car, Phone, System, and the OBD / Live / Trips /
Downloads / Settings additions. Same setup as test_console.py (a throwaway
OCTAVE with the simulated car on OCTAVE_PORT). Nothing here connects an
adapter, starts mirroring, opens a serial port or toggles a hardware enable;
what these tests change they put back."""

import json

import pytest
from test_console import HOST, PORT, SIZE, linked, live, sdk_wait, shot, wait_for
from textual.widgets import DataTable, OptionList, RichLog, TextArea

from octave_console.app import OctaveConsole
from octave_console.link import Client
from octave_console.panes_car import check_flags
from octave_console.panes_dash import changes, summarize
from octave_console.panes_media import halfblock, spectrum


@pytest.fixture
def sdk():
    with Client(HOST, PORT) as client:
        yield client


def screen_name(app):
    return type(app.screen).__name__


async def pick(pilot, app, value):
    """In a Pick dialog, choose the entry whose value is `value`."""
    assert await wait_for(pilot, lambda: screen_name(app) == "Pick")
    values = [v for v, _ in app.screen.choices]
    assert value in values, values
    app.screen.query_one(OptionList).highlighted = values.index(value)
    await pilot.press("enter")


async def edit_text(pilot, app, text):
    assert await wait_for(pilot, lambda: screen_name(app) == "EditText")
    app.screen.query_one(TextArea).text = text
    await pilot.press("ctrl+s")


# ── offline helpers ──────────────────────────────────────────────────────


def test_helpers():
    assert spectrum([0, 8, 4, 2], 4) == " █▄▂"
    assert spectrum([], 10) == ""
    assert check_flags([{"rpmLow": 1000, "rpmHigh": 2000}]) == [{"rpmLow": 1000, "rpmHigh": 2000}]
    for bad in ({"a": 1}, [1], [{"rpmLow": 3000, "rpmHigh": 2000}]):
        with pytest.raises(ValueError):
            check_flags(bad)
    before = {"label": "x", "gridColumns": 4, "cells": [{}, {}]}
    after = {"label": "x", "gridColumns": 4, "cells": [{}]}
    assert changes(before, after) == ["1 cell(s) dropped (overlap, off the grid, or bad)"]
    assert "grid 4 x 2" in summarize({"label": "x", "gridColumns": 4, "gridRows": 2, "cells": []})


def test_halfblock():
    import io

    from PIL import Image
    img = Image.new("RGB", (4, 4), (255, 0, 0))
    for x in range(4):
        for y in range(2, 4):
            img.putpixel((x, y), (0, 0, 255))   # bottom half blue
    buf = io.BytesIO()
    img.save(buf, "PNG")
    art = halfblock(buf.getvalue(), cols=2, rows=2)
    assert art.plain == "▀▀\n▀▀"
    styles = {str(span.style) for span in art.spans}
    assert "#ff0000 on #ff0000" in styles and "#0000ff on #0000ff" in styles
    assert halfblock(b"not an image") is None


# ── Library ──────────────────────────────────────────────────────────────


@live
async def test_library_playlists_move_and_play(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="library")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-library")
        assert await wait_for(pilot, lambda: len(pane.songs) >= 2)
        first = pane.songs[0]["file"]
        # the highlighted song's cover, from GET /art?song=, as half blocks (when the test tracks have covers)
        songs = app.query_one("#lib-songs", DataTable)
        songs.focus()
        songs.move_cursor(row=0)
        if await wait_for(pilot, lambda: pane.covers.get(pane.cover_query) is not None, timeout=5):
            assert "▀" in str(app.query_one("#lib-cover").render())
            shot(app, "library-cover")
        home = sdk.call("mediaManager", "get_song_playlist_name", first)
        # a new playlist
        await pilot.press("n")
        assert await wait_for(pilot, lambda: screen_name(app) == "EditValue")
        await pilot.press(*"pilot-pl", "enter")
        assert await sdk_wait(lambda: "pilot-pl" in sdk.call("mediaManager", "get_playlist_names"))
        assert await wait_for(pilot, lambda: "pilot-pl" in pane.playlists)
        # move the first song into it and back
        app.query_one("#lib-songs", DataTable).focus()
        app.query_one("#lib-songs", DataTable).move_cursor(row=0)
        await pilot.press("v")
        await pick(pilot, app, "pilot-pl")
        assert await sdk_wait(lambda: sdk.call("mediaManager", "get_song_playlist_name", first) == "pilot-pl")
        shot(app, "library")
        sdk.call("mediaManager", "move_song_to_playlist", first, home)
        # delete the playlist (asks first)
        table = app.query_one("#lib-playlists", DataTable)
        assert await wait_for(pilot, lambda: "pilot-pl" in pane.playlists)
        table.focus()
        table.move_cursor(row=pane.playlists.index("pilot-pl"))
        await pilot.press("D")
        assert await wait_for(pilot, lambda: screen_name(app) == "Confirm")
        await pilot.press("y")
        assert await sdk_wait(lambda: "pilot-pl" not in sdk.call("mediaManager", "get_playlist_names"))
        # Enter on a song plays it
        songs = app.query_one("#lib-songs", DataTable)
        songs.focus()
        songs.move_cursor(row=1)
        target = pane.songs[1]
        await pilot.press("enter")
        assert await sdk_wait(lambda: sdk.call("mediaManager", "get_current_file") == target["file"])
        sdk.call("nowPlaying", "togglePlay") if sdk.get("nowPlaying", "playing") else None
        # sort cycles title -> artist
        await pilot.press("s")
        assert pane.sort == 1 and not pane.reverse


# ── Dashboards ───────────────────────────────────────────────────────────


@live
async def test_dashboards_duplicate_edit_import_delete(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="dash")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-dash")
        assert await wait_for(pilot, lambda: len(pane.rows) >= 1)

        def ids():
            return [d["id"] for d in sdk.get("dashboardManager", "dashboards")]
        before = set(ids())
        table = app.query_one("#dash-list", DataTable)
        table.focus()
        table.move_cursor(row=[d["id"] for d in pane.rows].index("minimal"))
        # duplicate
        await pilot.press("d")
        assert await wait_for(pilot, lambda: screen_name(app) == "EditValue")
        app.screen.query_one("Input").value = "Pilot Copy"
        await pilot.press("enter")
        assert await sdk_wait(lambda: len(set(ids()) - before) == 1)
        copy_id = (set(ids()) - before).pop()
        # edit it as JSON: rename, and add an overlapping cell that normalizedSpec drops
        assert await wait_for(pilot, lambda: copy_id in [d["id"] for d in pane.rows])
        table.move_cursor(row=[d["id"] for d in pane.rows].index(copy_id))
        await pilot.press("e")
        assert await wait_for(pilot, lambda: screen_name(app) == "EditText")
        spec = json.loads(app.screen.query_one(TextArea).text)
        n_cells = len(spec["cells"])
        spec["label"] = "Pilot Edited"
        spec["cells"].append(dict(spec["cells"][0]))   # same place: an overlap
        shot(app, "dash-edit")
        await edit_text(pilot, app, json.dumps(spec))
        assert await sdk_wait(lambda: sdk.call("dashboardManager", "loadDashboard", copy_id).get("label") == "Pilot Edited")
        assert len(sdk.call("dashboardManager", "loadDashboard", copy_id)["cells"]) == n_cells
        # bad JSON is refused without a save
        await pilot.press("e")
        await edit_text(pilot, app, "{ not json")
        await pilot.pause(0.5)
        assert sdk.call("dashboardManager", "loadDashboard", copy_id).get("label") == "Pilot Edited"
        # import from text
        await pilot.press("i")
        spec["label"] = "Pilot Import"
        await edit_text(pilot, app, json.dumps(spec))
        assert await sdk_wait(lambda: "Pilot Import" in [d["label"] for d in sdk.get("dashboardManager", "dashboards")])
        shot(app, "dashboards")
        # delete both (asks first)
        for label in ("Pilot Import", "Pilot Edited"):
            assert await wait_for(pilot, lambda label=label: label in [d["label"] for d in pane.rows])
            table.move_cursor(row=[d["label"] for d in pane.rows].index(label))
            await pilot.press("x")
            assert await wait_for(pilot, lambda: screen_name(app) == "Confirm")
            await pilot.press("y")
            assert await sdk_wait(lambda label=label: label not in [d["label"] for d in sdk.get("dashboardManager", "dashboards")])
        assert set(ids()) == before


# ── Car ──────────────────────────────────────────────────────────────────


@live
async def test_car_shift_flags_and_gesture_mapping(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="car")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-car")
        saved = sdk.get("shiftLightManager", "flags")
        try:
            sdk.call("shiftLightManager", "setFlags", [])
            assert await wait_for(pilot, lambda: pane.flags == [])
            app.query_one("#shift-flags", DataTable).focus()
            await pilot.press("a")
            assert await sdk_wait(lambda: len(sdk.get("shiftLightManager", "flags")) == 1)
            assert await wait_for(pilot, lambda: len(pane.flags) == 1)
            # Enter edits the flag as JSON
            await pilot.press("enter")
            flag = dict(pane.flags[0], rpmLow=4800)
            await edit_text(pilot, app, json.dumps(flag))
            assert await sdk_wait(lambda: sdk.get("shiftLightManager", "flags")[0].get("rpmLow") == 4800)
            # a bad list is refused
            await pilot.press("E")
            await edit_text(pilot, app, json.dumps([{"rpmLow": 9000, "rpmHigh": 1000}]))
            await pilot.pause(0.5)
            assert sdk.get("shiftLightManager", "flags")[0].get("rpmLow") == 4800
            shot(app, "car")
            await pilot.press("x")
            assert await wait_for(pilot, lambda: screen_name(app) == "Confirm")
            await pilot.press("y")
            assert await sdk_wait(lambda: sdk.get("shiftLightManager", "flags") == [])
        finally:
            sdk.call("shiftLightManager", "setFlags", saved or [])
        # gesture mapping: pick an action for the first gesture, then put it back
        gestures = app.query_one("#gestures", DataTable)
        assert await wait_for(pilot, lambda: gestures.row_count > 0)
        gesture = sdk.get("gestureSensor", "gestures")[0]
        was = sdk.call("gestureSensor", "getGestureAction", gesture)
        other = next(a["key"] for a in sdk.get("gestureSensor", "actions") if a["key"] != was)
        gestures.focus()
        gestures.move_cursor(row=0)
        await pilot.press("enter")
        await pick(pilot, app, other)
        assert await sdk_wait(lambda: sdk.call("gestureSensor", "getGestureAction", gesture) == other)
        sdk.call("gestureSensor", "setGestureAction", gesture, was)


# ── Phone and System (readouts only: no phone, no network, no power) ───────


@live
async def test_phone_and_system_readouts():
    app = OctaveConsole(HOST, PORT, start_tab="phone")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        assert await wait_for(pilot, lambda: "Session" in str(app.query_one("#phone-state").render())
                              and app.state_of("phoneMirrorManager", "sessionState") is not None)
        assert "Session  " + app.state_of("phoneMirrorManager", "sessionState") in str(app.query_one("#phone-state").render())
        shot(app, "phone")
        await pilot.press("9")
        version = None
        assert await wait_for(pilot, lambda: app.state_of("diagnosticsManager", "appVersion"))
        version = app.state_of("diagnosticsManager", "appVersion")
        assert version in str(app.query_one("#sys-diag").render())
        assert await wait_for(pilot, lambda: len(app.query_one("#sys-log", RichLog).lines) > 0)
        assert await wait_for(pilot, lambda: "Commit" in str(app.query_one("#sys-about").render()))
        await pilot.click("#lic-LICENSE")
        assert await wait_for(pilot, lambda: screen_name(app) == "EditText")
        assert len(app.screen.query_one(TextArea).text) > 200
        shot(app, "licence")
        await pilot.press("escape")
        await pilot.press("9")
        shot(app, "system")


# ── additions to the first panes ─────────────────────────────────────────


@live
async def test_obd_diag_mode_and_live_home_cards(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="obd")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-obd")
        await pilot.click("#dtc-diag")
        assert await sdk_wait(lambda: sdk.call("obdManager", "is_diagnostic_mode") is True)
        assert await wait_for(pilot, lambda: pane.diag_mode)
        await pilot.click("#dtc-status")
        assert await wait_for(pilot, lambda: app.note == "reading status")
        await pilot.click("#dtc-diag")
        assert await sdk_wait(lambda: sdk.call("obdManager", "is_diagnostic_mode") is False)

        await pilot.press("1")
        live_pane = app.query_one("#pane-live")
        assert await wait_for(pilot, lambda: live_pane.order)
        home = sdk.get("settingsManager", "homeOBDParameters")
        pid = next(p for p in live_pane.order if p not in home)
        live_pane.table.move_cursor(row=live_pane.order.index(pid))
        live_pane.table.focus()
        try:
            if len(home) >= sdk.get("settingsManager", "maxHomeObdParameters"):
                sdk.call("settingsManager", "remove_home_obd_parameter", home[-1])
            await pilot.press("h")
            assert await sdk_wait(lambda: pid in sdk.get("settingsManager", "homeOBDParameters"))
            assert await wait_for(pilot, lambda: live_pane.home_cell(pid) != "")
            await pilot.press("h")
            assert await sdk_wait(lambda: pid not in sdk.get("settingsManager", "homeOBDParameters"))
        finally:
            sdk.call("settingsManager", "save_home_obd_parameters", home)


@live
async def test_trip_chart_download_target_and_theme(sdk):
    app = OctaveConsole(HOST, PORT, start_tab="trips")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        trips = app.query_one("#pane-trips")
        assert await wait_for(pilot, lambda: trips.ids)
        app.query_one("#trip-list", DataTable).focus()
        await pilot.press("c")
        await pick(pilot, app, "RPM")
        assert await wait_for(pilot, lambda: "Engine RPM" in str(app.query_one("#trip-chart-label").render()), timeout=10)
        shot(app, "trip-chart")

        await pilot.press("5")
        target = sdk.call("downloadManager", "get_download_playlist")
        names = sdk.call("mediaManager", "get_movable_playlist_names")
        other = next(n for n in names + [""] if n != target)
        await pilot.press("t")
        await pick(pilot, app, other)
        assert await sdk_wait(lambda: sdk.call("downloadManager", "get_download_playlist") == other)
        sdk.call("downloadManager", "set_download_playlist", target)

        await pilot.press("0")
        theme = sdk.get("settingsManager", "themeSetting")
        names = sdk.get("themeCatalog", "themeNames") if "themeCatalog" in sdk.server["objects"] else []
        if names:
            other = next(n for n in names if n != theme)
            await pilot.press("T")
            assert await wait_for(pilot, lambda: screen_name(app) == "Pick")
            shot(app, "theme-pick")
            await pick(pilot, app, other)
            assert await sdk_wait(lambda: sdk.get("settingsManager", "themeSetting") == other)
            sdk.call("settingsManager", "save_theme_setting", theme)


# ── svc/4 additions: presets, settings import / export, the folder picker, IMU ─


@live
async def test_settings_presets_import_export_and_folder(sdk):
    if "apply_settings" not in {m["name"] for m in sdk.describe("settingsManager")["methods"]}:
        pytest.skip("this OCTAVE predates apply_settings / presets / the folder browser")
    app = OctaveConsole(HOST, PORT, start_tab="settings")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-settings")
        assert await wait_for(pilot, lambda: "nowPlayingPresets" in app.state.get("settingsManager", {}))
        keys = set().union(*(p["settings"] for p in sdk.get("settingsManager", "nowPlayingPresets")))
        saved = {k: sdk.get("settingsManager", k) for k in keys | {"clockSize"}}
        try:
            # a Now Playing preset
            preset = sdk.get("settingsManager", "nowPlayingPresets")[1]
            pane.table.focus()
            await pilot.press("p")
            await pick(pilot, app, preset["label"])
            assert await sdk_wait(lambda: all(sdk.get("settingsManager", k) == v for k, v in preset["settings"].items()))
            # export shows every setting as JSON
            await pilot.press("o")
            assert await wait_for(pilot, lambda: screen_name(app) == "EditText")
            exported = json.loads(app.screen.query_one(TextArea).text)
            assert exported["clockSize"] == sdk.get("settingsManager", "clockSize") and "serviceEnabled" not in exported
            await pilot.press("escape")
            # import applies what is pasted, and shows what was refused
            await pilot.press("i")
            await edit_text(pilot, app, json.dumps({"clockSize": saved["clockSize"] + 1, "noSuchKey": 1}))
            assert await sdk_wait(lambda: sdk.get("settingsManager", "clockSize") == saved["clockSize"] + 1)
            assert await wait_for(pilot, lambda: screen_name(app) == "EditText")
            assert "noSuchKey" in app.screen.query_one(TextArea).text
            shot(app, "settings-refused")
            await pilot.press("escape")
        finally:
            sdk.call("settingsManager", "apply_settings", saved)
        # the folder picker: open, go up, come back, use the same folder
        folder = sdk.get("settingsManager", "mediaFolder")
        await pilot.press("f")
        assert await wait_for(pilot, lambda: screen_name(app) == "FolderPick")
        picker = app.screen
        assert await wait_for(pilot, lambda: picker.path == folder)
        assert any(label.endswith("/") or label.startswith("..") for _, label in picker.entries)
        shot(app, "folder-pick")
        # up a level (a parent folder, the list of places, or a refusal), then back to the folder
        await pilot.press("backspace")
        await wait_for(pilot, lambda: picker.path != folder or "may" in str(picker.query_one("#folder-info").render()))
        if picker.path != folder:
            name = folder.rstrip("/").rsplit("/", 1)[-1]

            def entry():
                return next((i for i, (path, label) in enumerate(picker.entries)
                             if path == folder or label == name + "/"), None)
            assert await wait_for(pilot, lambda: entry() is not None)
            picker.query_one(OptionList).highlighted = entry()
            await pilot.press("enter")
            assert await wait_for(pilot, lambda: picker.path == folder)
        await pilot.press("s")
        assert await wait_for(pilot, lambda: screen_name(app) != "FolderPick")
        assert sdk.get("settingsManager", "mediaFolder") == folder


@live
async def test_imu_readings_and_tare(sdk):
    from test_console import sim
    try:
        sim("sim_imu", on=True)   # under --dev with the QML UI the simulated IMU is off until asked
    except OSError:
        pytest.skip("no dev actions on this service")
    app = OctaveConsole(HOST, PORT, start_tab="car")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        pane = app.query_one("#pane-car")
        if not await wait_for(pilot, lambda: "LATERAL_G" in pane.imu, timeout=6):
            pytest.skip("no IMU readings from this service")
        assert "Lateral" in str(app.query_one("#imu-state").render()) or "LATERAL" in str(app.query_one("#imu-state").render())
        await pilot.click("#imu-tare")
        assert await wait_for(pilot, lambda: app.note == "IMU tared")
        await pilot.click("#imu-reset")
        assert await wait_for(pilot, lambda: app.note == "IMU tare reset")
        shot(app, "car-imu")


@live
async def test_download_last_export(sdk, tmp_path):
    """GET /export/logs to a local file. The backend writes exports to its HOME's
    Downloads, so a new export is made only when OCTAVE_EXPORT_OK=1 says that
    HOME is a throwaway (check_all.sh sets it); otherwise only the 'nothing to
    download' path is checked, if nothing was exported yet."""
    import os
    import urllib.request
    exists = True
    try:
        urllib.request.urlopen(f"http://{HOST}:{PORT}/export/logs", timeout=3)
    except Exception:
        exists = False
    app = OctaveConsole(HOST, PORT, start_tab="system")
    async with app.run_test(size=SIZE) as pilot:
        await linked(pilot, app)
        if not exists and os.environ.get("OCTAVE_EXPORT_OK") != "1":
            await pilot.click("#diag-download")
            assert await wait_for(pilot, lambda: any("No logs export" in str(n.message) for n in app._notifications))
            return
        if not exists:
            sdk.call("diagnosticsManager", "exportLogs")
        await pilot.click("#diag-download")
        assert await wait_for(pilot, lambda: screen_name(app) == "EditValue")
        target = tmp_path / "logs-export"
        app.screen.query_one("Input").value = str(target)
        await pilot.press("enter")
        assert await wait_for(pilot, lambda: target.exists() and target.stat().st_size > 0)


def test_token_store(tmp_path):
    import os
    import stat

    from octave_console.link import TokenStore
    store = TokenStore(tmp_path / "sub" / "tokens.json")
    assert store.get("127.0.0.1:9876") is None
    store.put("127.0.0.1:9876", "ab" * 32)
    store.put("10.0.0.5:9876", "cd" * 32)
    assert store.get("127.0.0.1:9876") == "ab" * 32
    assert stat.S_IMODE(os.stat(store.path).st_mode) == 0o600
    assert stat.S_IMODE(os.stat(store.path.parent).st_mode) == 0o700
    store.put("127.0.0.1:9876", None)   # forgotten (revoked)
    assert store.get("127.0.0.1:9876") is None and store.get("10.0.0.5:9876") == "cd" * 32


PAIR_HOST = __import__("os").environ.get("OCTAVE_PAIR_HOST")   # this machine's LAN address, OCTAVE on --service-bind 0.0.0.0


@pytest.mark.skipif(not PAIR_HOST, reason="set OCTAVE_PAIR_HOST (OCTAVE bound beyond loopback) to test pairing")
async def test_pairing_over_the_network(tmp_path):
    """Connected through the LAN address the console is untrusted: it shows the
    pair screen, takes the code (from the dev action sim_pairing_code, sent on
    loopback), stores the token 0600, and goes on as a trusted client."""
    import json
    import os
    import socket
    import stat
    with socket.create_connection(("127.0.0.1", PORT), timeout=3) as s:
        s.sendall(b'{"action":"sim_pairing_code"}\n')
        code = json.loads(s.makefile().readline())["result"]["code"]
    token_file = tmp_path / "tokens.json"
    app = OctaveConsole(PAIR_HOST, PORT, start_tab="live", token_file=str(token_file))
    async with app.run_test(size=SIZE) as pilot:
        assert await wait_for(pilot, lambda: screen_name(app) == "PairScreen")
        assert "NOT PAIRED" in str(app.query_one("#status").render())
        await pilot.pause(0.3)
        shot(app, "pair")
        await pilot.press(*str(code), "enter")
        assert await wait_for(pilot, lambda: app.connected and app.query_one("#pane-live").order, timeout=10)
        assert stat.S_IMODE(os.stat(token_file).st_mode) == 0o600
        token = json.loads(token_file.read_text())[f"{PAIR_HOST}:{PORT}"]
    with Client(PAIR_HOST, PORT, token=token) as paired:
        assert paired.server["auth"] == "trusted"
        paired.request("unpair")   # leave no paired device behind
