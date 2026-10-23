# Glass as the head unit's screen (kiosk), a deployment note

**Status:** a note, not enabled anywhere. Nothing in the repo installs these units; copy them by hand on a device you set up for it.

The usual OCTAVE is the C++ app with its QML UI. This note describes the other arrangement the open backend allows on a Raspberry Pi: `octave --headless` (every manager and the service, no QML engine, no window) plus Chromium in kiosk mode showing `frontends/glass` served by the app itself. The browser is the screen; OCTAVE is the engine behind it.

## What it trades

| | QML UI (default) | Glass in a kiosk browser |
|---|---|---|
| Rendering | Qt Quick scene graph on the GPU | Chromium: HTML, CSS, canvas; heavier on a Pi, see "Low power" below |
| Phone mirror and Android Auto | native decode into the UI | H.264 over the service decoded with WebCodecs (Chromium 94+); JPEG frames for Android Auto |
| Touch on the phone | native | `injectTouch` from the page, the same |
| Memory | one process | the app (about a fifth less without QML) plus Chromium (several hundred MB) |
| Startup | one unit | two units; the page reconnects by itself while the app comes up |
| Failure mode | the app restarts | if Chromium dies the app keeps playing music and recording; systemd restarts the browser |
| Album art, exports | in-process | over HTTP from the app (`/art`, `/export/...`), on loopback no pairing needed |
| Settings only the head unit can change | in the UI | the page has every setting; making pairing codes for *other* devices stays on the head unit (`servicePairing`), so with no QML UI pair other devices from the Pi's own page or keep `--service-bind` off |

**Fall back** to the normal app at any time: `systemctl --user disable --now glass-kiosk octave-headless` and start `octave` as before. Settings, playlists, trips and dashboards are the same files either way.

## Units (user session, `~/.config/systemd/user/`)

`octave-headless.service`:

```ini
[Unit]
Description=OCTAVE engine (headless) serving the glass frontend
After=network.target sound.target

[Service]
# --web-root serves the built page at http://127.0.0.1:9876/; keep --service-bind off unless the car's network is yours
ExecStart=%h/OCTAVE/build/octave --headless --web-root %h/OCTAVE/frontends/glass/dist
Restart=always
RestartSec=2
Environment=QT_QPA_PLATFORM=offscreen

[Install]
WantedBy=default.target
```

`glass-kiosk.service` (Chromium full screen on the head unit's display; the page is on loopback, so it is trusted without pairing):

```ini
[Unit]
Description=Glass frontend in Chromium kiosk
After=octave-headless.service graphical-session.target
Wants=octave-headless.service

[Service]
Environment=DISPLAY=:0
# --ozone-platform=wayland instead of DISPLAY on a Wayland session (labwc); drop --disable-gpu only if the GPU works
ExecStart=/usr/bin/chromium-browser --kiosk --noerrdialogs --disable-infobars --disable-session-crashed-bubble \
  --overscroll-history-navigation=0 --touch-events=enabled --autoplay-policy=no-user-gesture-required \
  --enable-features=WebCodecs --app=http://127.0.0.1:9876/
Restart=always
RestartSec=3

[Install]
WantedBy=graphical-session.target
```

Enable with `systemctl --user daemon-reload && systemctl --user enable --now octave-headless glass-kiosk`, and `loginctl enable-linger $USER` so the user session starts at boot. Build the page once with `cd frontends/glass && npm install && npm run build`.

## Low power

Chromium on a Pi pays most for blur. Glass has a low-power switch (the rail's leaf button, or `?power=low` in the URL, remembered in the browser) that turns the frosted panels flat, stops the backdrop's drifting blobs and drops transitions; put `--app=http://127.0.0.1:9876/?power=low` in the unit for a Pi 4. Numbers from the performance pass are in `frontends/glass/README.md`.

## Pointers

- The service, pairing and the Origin rule: `docs/api/README.md`, `wiki/writing-a-frontend.html`.
- Glass itself: `frontends/glass/README.md`.
- Kiosk-mode Chromium reads its flags from the command line only; `~/.config/chromium-flags.conf` applies to the desktop launcher, not to the unit.
