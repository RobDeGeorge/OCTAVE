# Car profiles and social sharing ("Strava for cars")

**Status:** parked: a long-term idea scoped on 2026-10-17, well after v1.0. No work started.
**Last updated:** 2026-10-17

## What it is

Let owners sync their car's OBD history to an online profile, and let other people browse it, follow cars and compare stats. Think of each car as an account: its build, its trips, its long-term health, its dashboards.

Ideas for what a profile shows:
- **The car:** make, model, engine, transmission, mods, photos, the 3D model (`frontend` Jeep drivetrain work), mileage.
- **Trips** (like Strava activities): distance, duration, elevation, peak coolant and oil temps, time in 4-low, fuel used. Road trips and trail runs.
- **Long-term health:** fuel trims, coolant, battery voltage and similar trends over months. It also builds a "what's normal for this engine" baseline across owners.
- **Comparisons:** "your 4.0 I6 runs 10°F cooler than the average TJ."
- **Dashboards:** browse and install other people's dashboards.
- **Follows and a feed of new trips and builds.**

Prior art worth studying (mostly dead, so learn why): Fuelly (fuel-log community, still alive), Torque Pro web log uploads, Automatic, Dash, Hum.

## Why it's parked

- v1.0, the Python sunset (`TODO/python-sunset-after-v1.md`) and the backend split (`TODO/backend-service-split.md`) come first.
- OCTAVE has **no trip or history recording today**. `ObdManager` exposes live values plus a few counters (`distanceWithMIL`, `distanceSinceCodesCleared`, odometer among the multi-value PIDs at 0x61+), but nothing is persisted over time. Local history is the foundation for everything here.
- There's **no GPS source** on the Jeep box yet. Location-based trip features depend on either a GPS module or the phone GPS link in `TODO/octave-companion-app.md`.
- Running an online service (hosting, storage, accounts, moderation, costs) is a different kind of commitment from shipping a GPL head unit. Decide that deliberately.

## Privacy rules (non-negotiable, design these in from day one)

Public driving and location data can hurt people. Strava's public heatmap famously exposed military bases, and drive histories reveal home, work and overnight parking. Driving behaviour (speed, hard braking) is exactly what insurers and lawyers want.

1. **Off by default.** Nothing leaves the car until the owner opts in. Opt-in is **per field category** (stats, health trends, trips, location, dashboards).
2. **Stats-only sharing is the default** when sharing is on: aggregates and summaries, not raw streams.
3. **Location:** privacy zones (hide trip starts and ends within a radius of home and other saved places, on by default), a delay before trips are published, and an option to share only distance and elevation with no map.
4. **Speed and driving behaviour:** never public by default. No speed traces, no hard-braking or acceleration events, unless the owner explicitly turns them on.
5. **Never shown publicly:** VIN, plates, exact home location.
6. **Real deletion:** "delete everything" removes the data from the server and from any federated copies we control. Data export is available any time.
7. **Local-first:** history lives on the car box first. Syncing is a copy, not where the data actually lives.

## Architecture direction

**Federated, not one central site.** Make each car (or owner) an **ActivityPub** actor, so cars show up on Mastodon and the rest of the fediverse and can be followed from there. Anyone can self-host the sync server, or join a community instance. That fits OCTAVE's open-source ethos, avoids one big service we have to run and pay for, and keeps owners in control.

Pieces:
- **On the car:** a local history recorder (time-series of the PIDs, trip start/stop detection, a size cap or downsampling for old data) plus a sync client. The sync client would use the WebSocket/JSON gateway from `TODO/backend-service-split.md`.
- **Server:** a self-hostable "OCTAVE hub". It stores synced summaries, serves profile pages and speaks ActivityPub. Separate repo, its own licence decision (OCTAVE itself is GPL-3.0+).
- **Dashboard sharing** reuses `DashboardManager`'s existing `exportDashboard` / `importDashboard*` and `importSpec` (sanitized, data-only, ids regenerated). The gallery just hosts those JSON files.

## Recommended order of operations

1. **Local trip and history recording on the box.** It's useful on its own: a trip-history page and health-trend graphs in OCTAVE, no network. Wiki pages and settings keys for retention.
2. **Shareable car cards and trip summaries** exported as an image or file. Still no server. Something to post anywhere.
3. **Dashboard sharing gallery** (static hosting of exported dashboard JSON is enough to start).
4. **Self-hostable sync server** for history and profiles, with the privacy rules above enforced on both the client and the server.
5. **Federation:** ActivityPub actors, follows, a feed, cross-car comparisons.

Steps 1–3 deliver most of the fun with none of the hosting burden. Re-evaluate before step 4.

Delete this file when step 3 ships and steps 4–5 have been split into their own plans (or dropped).
