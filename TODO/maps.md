# Maps (OCTAVE's own map, fed by any GPS source)

**Goal:** OCTAVE knows where the car is from whichever GPS source is live (a USB dongle, the phone, our pod, the device itself) and shows it on its own map with accurate landmarks, recorded trips and an offline region file.
**Status:** deferred: researched 2026-11-24, nothing built. No location code exists in `src/`, and no Qt kit on rhea has Qt Location or Qt Positioning installed (only the system `qt6-positioning` package).
**Last updated:** 2026-11-24

## Why it's parked

It is a new subsystem (a manager, a page, a data pipeline) and the Home / dashboards rework is in flight. Step 1 is small and useful by itself (position in the black box), so that is the place to start whenever this is picked up.

## What it is and is not

- **Phone mirroring stays the main navigation.** Google Maps and Waze through the mirror, Android Auto and AirPlay give business data and live traffic nothing open matches. This plan does not replace that.
- **The local map is the companion to it:** the car's own position, every recorded trip drawn on a map, landmarks and terrain, and a map that works with no phone and no signal.
- **Rob's priority is accurate mapping and accurate landmarks** (2026-11-24). That is a matter of data, not of renderer: see "Data" below.
- **No turn-by-turn in this plan.** It is listed last as an open question.

## Decisions made (2026-11-24)

- **One manager for position, many sources.** `locationManager` holds the current fix and picks the best live source, the way `nowPlaying` picks between the phone and local music. The map, the black box, the car finder and "where you parked" read the manager and never a source.
- **Position is ordinary parameters.** Latitude, longitude, GPS speed, heading, altitude and accuracy go into `paramStore` and `ParameterCatalog`, so the black box records them and dashboards show them with no special case. This replaces "a location column per sample" in `octave-companion-app.md` step 4.
- **The map page is written against Qt Location's map item** (`MapView`, `Plugin`, `MapQuickItem`, `MapPolyline`). Tiles come from a swappable plugin, so the page survives a change of renderer.
- **Online tiles are for viewing only.** The OSM Foundation's tile policy prohibits "download an area for offline use" against its servers and blocks apps with no User-Agent of their own. Offline comes from a region file, never from scraping.

## GPS sources

| Source | How it arrives | Where it comes from |
|---|---|---|
| Simulator | `src/sim/carsim` drives a made-up route | New; needed first, so `--sim`, the tests and the browser demo have a moving position |
| USB GPS dongle | NMEA text on a serial port (`/dev/ttyACM0` class) | New; works on the Pi with no other hardware. Qt Positioning's `nmea` plugin reads it, or `QNmeaPositionInfoSource` over our own `QSerialPort` |
| The device itself | The system's location service through Qt Positioning | For OCTAVE on an Android phone or tablet; needs the location permission in the manifest |
| Phone | Sent by the companion app | `octave-companion-app.md` step 4: needs an installed app, the mirroring server cannot read location |
| Pod | Over the pod bus through the OBD adapter | `lora-car-finder.md`, `diy-obd-adapter.md` step 7. If the pod's GNSS channel carries NMEA sentences, it shares the dongle's parser |

An iPhone over AirPlay gives no location; there the dongle or the pod is the only source.

Picking a source: the freshest fix with the best reported accuracy wins, a source with no fix for a few seconds is dropped, and a setting can pin one. Expected order when all are live: pod, dongle, phone.

## Map renderers (what was found)

All of these sit behind the same Qt Location map item except where noted.

| Renderer | For | Against |
|---|---|---|
| Qt Location's OSM plugin | Built into Qt, raster tiles drawn by Qt Quick on any graphics backend, disk cache, an offline tile directory | Raster only; dark mode is a tint; the default map types point at services that stamp "API key required" (disable the provider list, set a custom host) |
| MapLibre Native Qt (`maplibre/maplibre-native-qt`) | GPU vector maps, real themed styles, reads MBTiles and PMTiles region files | Last stable release 3.0.0 (October 2024, Qt 6.5 to 6.7); 4.0 unreleased with an unstable API; QML path is OpenGL only; open bugs: crash on teardown and on resize, a leak in the OpenGL render path, a Windows process that never exits |
| Our own plugin reading MBTiles | Offline raster on every platform with the SQLite already in `third_party/sqlite/`; QGroundControl does this | Raster region files are large (a state at street zoom runs to gigabytes) |
| `rinigus/mapbox-gl-qml` | A second QML binding to MapLibre, released July 2026, tested on Qt 6.10 | Not a Qt Location plugin (its own map item); proven on Sailfish and desktop Linux only |

Ruled out: KDE Marble (KDE dependencies), OsmAnd core and CoMaps / Organic Maps (whole apps, not libraries), Navit (own GTK / SDL interface), Tangram ES (dead since January 2024), mapsforge VTM (Java), maplibre-rs (experimental), and the keyed commercial SDKs (not usable as the default of a GPL app; Google's terms forbid drawing or caching its tiles in another app).

For reference, StradaOS (`lordnordic-debug/StradaOS`, GPL-3.0, Android) does all of it by hand in about 5,000 lines of Java: raster tiles from a fallback list of free servers, a mapsforge `.map` reader that draws its own tiles, Photon / Nominatim search, OSRM routing with an offline A*, and voice guidance. The architecture is a good checklist; the code is tied to Android's `Canvas`.

## Data (what decides accuracy)

- **Every open option draws OpenStreetMap.** Roads, towns, parks, trails, peaks and water are good in the US; shops, restaurants and fuel are patchier outside cities.
- **Freshness.** A region file is as current as the day it was cut. Protomaps builds the world daily and `pmtiles extract --bbox=...` cuts a region into one file, so an "update maps" action can stay within days of OSM.
- **What the tile set keeps and what the style shows.** A landmark dropped from the tile schema, or present but not styled, is invisible. Start from a detailed style and remove, not from a minimal one.
- **Overture Maps places** is the open way to fill OSM's business and landmark gap: tens of millions of places under permissive licences (CDLA Permissive 2.0, Apache 2.0 for the Foursquare records), GeoParquet, monthly releases, a confidence score per record. A second landmark layer and a search source.
- **Search is separate from the picture.** By name online: Photon or Nominatim (their usage policies apply). Offline: a local index (SQLite FTS) built from the region's OSM and Overture data.

## Steps

1. **`locationManager` with the simulator and USB dongle sources, position in the black box.** `src/managers/locationmanager.{h,cpp}` per CLAUDE.md's "adding a new manager" (member of `AppCore`, `expose("locationManager", ...)`, `octave.scope` declarations: position is at least as sensitive as vehicle data). Source interface plus the simulator source (`src/sim/`) and the NMEA serial source. Parameter rows in `parametercatalog` and feeding `paramStore`; the trip recorder then stores them. Add `Positioning` to the Qt components in `CMakeLists.txt`, the module to the aqt kits in `BUILD.md` and CI, and a QtTest fed with recorded NMEA. Settings: preferred source, serial port. Regenerate `docs/api/`, update the wiki. **Effort:** L. **Needs:** desktop, then a USB GPS dongle (u-blox 7 / 8 class, about $15) on the Pi in the Jeep.
2. **Map page on Qt Location, position and recorded trips.** `frontend/MapPage.qml` with `MapView`, the car arrow, follow / free pan, and a trip drawn as a `MapPolyline` from Trip History. OSM plugin with an OCTAVE User-Agent, a custom tile host and the provider list disabled. Add `Location` to CMake, `qtlocation` to the kits, and the geoservices plugin to the AppImage, the Windows installer and the APK. Lint with the 6.7.3 `qmllint`. A map dashboard widget (catalog entry + url) once the page works. **Effort:** L. **Needs:** desktop, Pi, S22.
3. **Device and phone sources.** Android's own location through Qt Positioning (manifest permission, runtime request). The phone source lands with `octave-companion-app.md` step 4, which then feeds `locationManager` and not a manager of its own. **Effort:** M each. **Needs:** S22.
4. **Offline region file on the Pi.** Try the MapLibre plugin on the Orange Pi (Linux, OpenGL: the binding's best-supported case) with a state-sized PMTiles or MBTiles extract; measure frame rate and memory beside Quick 3D. If it misbehaves, write the MBTiles raster plugin instead. Then a region download / update action from a host we are allowed to pull from. **Effort:** M spike, then L. **Needs:** Pi.
5. **Landmarks and search.** A tool under `tools/` that cuts Overture places and OSM names for a region into a SQLite file; a landmark layer on the map with a confidence cut-off; search by name over that file, with Photon online when connected. **Effort:** L. **Needs:** desktop.
6. **Pod source.** With `diy-obd-adapter.md` step 7 / `lora-car-finder.md`: the pod's GNSS into `locationManager`. **Effort:** M on the OCTAVE side. **Needs:** the pod and adapter boards.

## Open questions

- **Browser demo.** Whether Qt Location builds for WebAssembly is unconfirmed, and MapLibre's wasm preset needs Qt Location disabled. Guard the map behind a CMake check (as `hasQuick3D` does) and leave it out of the demo until proven.
- **Windows and macOS with MapLibre.** Its QML path needs Qt forced to OpenGL; those platforms stay on the raster plugin until MapLibre Qt 4.0 is released.
- **Directions without a phone.** If wanted, evaluate OSM Scout Server (`rinigus/osmscout-server`, GPL-3: offline tiles, search and routing as a local service; Pure Maps is the QML app built on it) or libosmscout (active, runs on a Pi in about 1 GB, `janbar/osmin` is a GPL-3 Qt 6 navigator on it) before Valhalla. Not planned.
- **Who hosts region files.** Protomaps' daily build is free to extract from; whether OCTAVE points users at it directly or mirrors per-state files is undecided.
- **Privacy.** Position in the black box makes trip files location history. Decide whether recording position is on by default, and keep it out of anything exported or shared without saying so.

## Cross-references

- `octave-companion-app.md` step 4 (phone GPS) and step 9 (where you parked): both read or feed `locationManager` once it exists.
- `diy-obd-adapter.md` step 7 and `lora-car-finder.md`: the pod's GNSS is a source here; the car finder's parked position can be shown on the map.
- `traffic-stop-mode.md`: its location beacon can take the car's fix from `locationManager`.
- `home-dashboard.md` / `dashboards-roadmap.md`: the map widget goes through the widget catalog like any other.

## Recommended order

1, then 2: together they give position in every recorded trip and a map to see it on, with only a dongle. 3 with the companion app. 4 before 5, since the landmark layer needs the offline file's conventions. 6 when the boards exist.

Delete this file (and its row in `TODO/README.md`) when done.
