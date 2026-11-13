//! The service-protocol client (wiki/utilities.html#service-protocol,
//! docs/api/): what the cluster subscribes to and how the events fold into
//! `State`. The connection itself is `link_tcp.rs` (a program: JSON lines
//! over TCP on a background thread) or `link_web.rs` (the browser build: the
//! page's WebSocket, one message per text frame). Either way the UI only
//! reads `State` and asks the `Link` for calls; it never waits on the socket.

use std::collections::HashMap;

use serde_json::{Value, json};
// std's Instant, except in a browser, where std has no clock
use web_time::Instant;

#[cfg(not(target_arch = "wasm32"))]
#[path = "link_tcp.rs"]
mod transport;
#[cfg(target_arch = "wasm32")]
#[path = "link_web.rs"]
mod transport;

pub use transport::Link;
#[cfg(not(target_arch = "wasm32"))]
pub use transport::pair;

/// The cover is drawn as a dot matrix this many dots square.
pub const ART_DOTS: u32 = 40;

/// The protocol versions this client knows (docs/api/README.md): 2 added
/// pairing, which only matters over the network; 3 only removed an object
/// this cluster never used.
pub const PROTOCOL_VERSIONS: [i64; 3] = [1, 2, 3];

/// The name this frontend registers its OBD polling demand under.
const DEMAND_CONSUMER: &str = "retro-cluster";

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum LinkState {
    Searching,
    Online,
}

/// One row of `parameterCatalog.displayParameters`: already in the units
/// the user picked, with the scale / offset that turns a native reading
/// into them.
#[derive(Clone, Debug)]
pub struct ParamInfo {
    pub title: String,
    pub short_title: String,
    pub unit: String,
    pub min: f64,
    pub max: f64,
    pub decimals: usize,
    pub scale: f64,
    pub offset: f64,
}

#[derive(Default, Clone)]
pub struct NowPlaying {
    pub source: String,
    pub playing: bool,
    pub has_track: bool,
    pub title: String,
    pub artist: String,
    pub album: String,
    pub duration_ms: i64,
    pub position_ms: i64,
    pub position_at: Option<Instant>,
    pub can_seek: bool,
    pub can_shuffle: bool,
    pub shuffled: bool,
}

impl NowPlaying {
    /// The position now, run forward from the last report while playing.
    pub fn position_now(&self) -> i64 {
        let mut p = self.position_ms;
        if self.playing
            && let Some(at) = self.position_at
        {
            p += at.elapsed().as_millis() as i64;
        }
        if self.duration_ms > 0 { p.clamp(0, self.duration_ms) } else { p.max(0) }
    }
}

/// One row of `tripRecorder.trips`.
#[derive(Clone, Debug)]
pub struct Trip {
    pub id: i64,
    pub start_iso: String,
    pub duration_s: f64,
    pub samples: i64,
    pub open: bool,
    pub vehicle: String,
}

/// A trouble code as `obdManager` reports it.
#[derive(Clone, Debug)]
pub struct Dtc {
    pub code: String,
    pub description: String,
    /// The freeze frame's readings for this code (native units), by PID
    pub values: Vec<(String, f64)>,
}

pub struct State {
    pub link: LinkState,
    pub address: String,
    pub protocol: Option<i64>,
    pub app_version: String,
    pub headless: bool,
    /// Native-unit readings from paramStore, by parameter id.
    pub params: HashMap<String, f64>,
    pub catalog: HashMap<String, ParamInfo>,
    /// Catalog ids in the backend's order.
    pub catalog_order: Vec<String>,
    pub now: NowPlaying,
    pub volume: i64,
    pub muted: bool,
    pub effectively_muted: bool,
    pub obd_connected: bool,
    pub obd_state: String,
    pub obd_status: String,
    pub supported: Vec<String>,
    pub vin: String,
    pub vehicle_make: String,
    pub vehicle_year: i64,
    pub shift_lit: bool,
    pub shift_active: bool,
    pub recording: bool,
    pub trip_elapsed_s: i64,
    pub clock: Option<String>,
    pub dtcs: Vec<Dtc>,
    pub mil: bool,
    pub freeze: Vec<Dtc>,
    /// The last clear: success, the app's message, when
    pub dtc_result: Option<(bool, String, Instant)>,
    pub trips: Vec<Trip>,
    /// `tripRecorder.requestDetail` answers, by trip id
    pub trip_details: HashMap<i64, Value>,
    pub export_path: Option<String>,
    /// nowPlaying.artRevision, and the cover fetched for it (GET /art) as
    /// ART_DOTS x ART_DOTS luminance; None when there is no cover
    pub art_revision: i64,
    pub art: Option<(i64, Vec<u8>)>,
    /// audioAnalyzer.levels: one bar per band, 0..8
    pub levels: Vec<u8>,
    pub last_error: Option<String>,
    /// False when the app wants this cluster to pair first (protocol 2 over
    /// the network): retro-cluster --pair CODE, then --token.
    pub trusted: bool,
    /// When the last event arrived, for the RX lamp.
    pub last_rx: Option<Instant>,
    pub dropped: u64,
}

impl State {
    fn new(address: String) -> Self {
        State {
            link: LinkState::Searching,
            address,
            protocol: None,
            app_version: String::new(),
            headless: false,
            params: HashMap::new(),
            catalog: HashMap::new(),
            catalog_order: Vec::new(),
            now: NowPlaying::default(),
            volume: 0,
            muted: false,
            effectively_muted: false,
            obd_connected: false,
            obd_state: String::new(),
            obd_status: String::new(),
            supported: Vec::new(),
            vin: String::new(),
            vehicle_make: String::new(),
            vehicle_year: 0,
            shift_lit: false,
            shift_active: false,
            recording: false,
            trip_elapsed_s: 0,
            clock: None,
            dtcs: Vec::new(),
            mil: false,
            freeze: Vec::new(),
            dtc_result: None,
            trips: Vec::new(),
            trip_details: HashMap::new(),
            export_path: None,
            art_revision: -1,
            art: None,
            levels: Vec::new(),
            last_error: None,
            trusted: true,
            last_rx: None,
            dropped: 0,
        }
    }

    /// A reading in display units, if the app has one.
    pub fn display(&self, id: &str) -> Option<f64> {
        let v = *self.params.get(id)?;
        Some(match self.catalog.get(id) {
            Some(info) => v * info.scale + info.offset,
            None => v,
        })
    }

    /// Whether the connected vehicle reports this PID (IMU and unknown
    /// vehicles count as supported, like the QML gate).
    pub fn is_supported(&self, id: &str) -> bool {
        self.supported.is_empty() || self.supported.iter().any(|s| s == id)
    }

    fn apply_event(&mut self, ev: &Value) {
        self.last_rx = Some(Instant::now());
        match ev["event"].as_str() {
            Some("params") => {
                if let Some(values) = ev["values"].as_object() {
                    for (id, v) in values {
                        match v.as_f64() {
                            Some(x) => {
                                self.params.insert(id.clone(), x);
                            }
                            None => {
                                self.params.remove(id);
                            }
                        }
                    }
                }
            }
            Some("property") => {
                let object = ev["object"].as_str().unwrap_or("");
                let name = ev["name"].as_str().unwrap_or("");
                self.apply_property(object, name, &ev["value"]);
            }
            Some("signal") => {
                let args = &ev["args"];
                match (ev["object"].as_str().unwrap_or(""), ev["name"].as_str().unwrap_or("")) {
                    ("clock", "timeChanged") => self.clock = Some(args[0].as_str().unwrap_or("").to_string()),
                    ("obdManager", "dtcCodesChanged") => self.dtcs = dtc_list(&args[0]),
                    ("obdManager", "freezeFrameChanged") => self.freeze = dtc_list(&args[0]),
                    ("obdManager", "milStatusChanged") => self.mil = args[0].as_bool().unwrap_or(false),
                    ("obdManager", "dtcClearResult") => {
                        let ok = args[0].as_bool().unwrap_or(false);
                        self.dtc_result = Some((ok, args[1].as_str().unwrap_or("").to_string(), Instant::now()));
                    }
                    ("tripRecorder", "detailReady") => {
                        if let Some(id) = args[0].as_i64() {
                            self.trip_details.insert(id, args[1].clone());
                        }
                    }
                    ("tripRecorder", "exportFinished") => self.export_path = args[1].as_str().map(String::from),
                    _ => {}
                }
            }
            Some("dropped") => self.dropped += ev["count"].as_u64().unwrap_or(0),
            _ => {}
        }
    }

    fn apply_property(&mut self, object: &str, name: &str, v: &Value) {
        let s = || v.as_str().unwrap_or("").to_string();
        let b = || v.as_bool().unwrap_or(false);
        let i = || v.as_i64().or_else(|| v.as_f64().map(|f| f as i64)).unwrap_or(0);
        let np = &mut self.now;
        match (object, name) {
            ("nowPlaying", "source") => np.source = s(),
            ("nowPlaying", "playing") => {
                // Re-anchor the clock so a pause doesn't jump
                np.position_ms = np.position_now();
                np.position_at = Some(Instant::now());
                np.playing = b();
            }
            ("nowPlaying", "hasTrack") => np.has_track = b(),
            ("nowPlaying", "title") => np.title = s(),
            ("nowPlaying", "artist") => np.artist = s(),
            ("nowPlaying", "album") => np.album = s(),
            ("nowPlaying", "durationMs") => np.duration_ms = i(),
            ("nowPlaying", "positionMs") => {
                np.position_ms = i();
                np.position_at = Some(Instant::now());
            }
            ("nowPlaying", "canSeek") => np.can_seek = b(),
            ("nowPlaying", "canShuffle") => np.can_shuffle = b(),
            ("nowPlaying", "shuffled") => np.shuffled = b(),
            ("nowPlaying", "artRevision") => self.art_revision = i(),
            ("volumeController", "percent") => self.volume = i(),
            ("volumeController", "muted") => self.muted = b(),
            ("volumeController", "effectivelyMuted") => self.effectively_muted = b(),
            ("obdManager", "connected") => self.obd_connected = b(),
            ("obdManager", "connectionState") => self.obd_state = s(),
            ("obdManager", "connectionStatus") => self.obd_status = s(),
            ("obdManager", "vehicleSupportedParameters") => {
                self.supported = string_list(v);
            }
            ("obdManager", "vin") => self.vin = s(),
            ("obdManager", "vehicleMake") => self.vehicle_make = s(),
            ("obdManager", "vehicleModelYear") => self.vehicle_year = i(),
            ("shiftLightManager", "lit") => self.shift_lit = b(),
            ("shiftLightManager", "active") => self.shift_active = b(),
            ("tripRecorder", "recording") => self.recording = b(),
            ("tripRecorder", "currentTripElapsedS") => self.trip_elapsed_s = i(),
            ("parameterCatalog", "displayParameters") => self.load_catalog(v),
            ("clock", "time") => self.clock = Some(s()),
            ("tripRecorder", "trips") => self.trips = trip_list(v),
            ("audioAnalyzer", "levels") => {
                self.levels = v.as_array().map(|a| a.iter().map(|x| x.as_u64().unwrap_or(0).min(8) as u8).collect()).unwrap_or_default();
            }
            _ => {}
        }
    }

    fn load_catalog(&mut self, rows: &Value) {
        let Some(rows) = rows.as_array() else { return };
        self.catalog.clear();
        self.catalog_order.clear();
        for row in rows {
            let Some(id) = row["id"].as_str() else { continue };
            let f = |k: &str, d: f64| row[k].as_f64().unwrap_or(d);
            let info = ParamInfo {
                title: row["title"].as_str().unwrap_or(id).to_string(),
                short_title: row["shortTitle"].as_str().unwrap_or("").to_string(),
                unit: row["unit"].as_str().unwrap_or("").to_string(),
                min: f("min", 0.0),
                max: f("max", 100.0),
                decimals: row["decimals"].as_u64().unwrap_or(0) as usize,
                scale: f("displayScale", 1.0),
                offset: f("displayOffset", 0.0),
            };
            self.catalog_order.push(id.to_string());
            self.catalog.insert(id.to_string(), info);
        }
    }

    /// The result of a call this client reads back, by "object.method".
    fn apply_result(&mut self, what: &str, v: &Value) {
        match what {
            "obdManager.get_dtc_codes" => self.dtcs = dtc_list(v),
            "obdManager.get_freeze_frame" => self.freeze = dtc_list(v),
            "obdManager.get_mil_status" => self.mil = v.as_bool().unwrap_or(false),
            _ => {}
        }
    }
}

fn dtc_list(v: &Value) -> Vec<Dtc> {
    v.as_array()
        .map(|a| {
            a.iter()
                .map(|d| Dtc {
                    code: d["code"].as_str().unwrap_or("").to_string(),
                    description: d["description"].as_str().unwrap_or("").to_string(),
                    values: d["values"]
                        .as_object()
                        .map(|m| m.iter().filter_map(|(k, v)| v.as_f64().map(|x| (k.clone(), x))).collect())
                        .unwrap_or_default(),
                })
                .collect()
        })
        .unwrap_or_default()
}

fn trip_list(v: &Value) -> Vec<Trip> {
    let Some(rows) = v.as_array() else { return Vec::new() };
    rows.iter()
        .filter_map(|t| {
            let vehicle = match (t["modelYear"].as_i64(), t["make"].as_str()) {
                (Some(y), Some(m)) if y > 0 && !m.is_empty() => format!("{y} {m}"),
                (_, Some(m)) => m.to_string(),
                _ => String::new(),
            };
            Some(Trip {
                id: t["id"].as_i64()?,
                start_iso: t["startIso"].as_str().unwrap_or("").to_string(),
                duration_s: t["durationS"].as_f64().unwrap_or(0.0),
                samples: t["sampleCount"].as_i64().unwrap_or(0),
                open: t["open"].as_bool().unwrap_or(false),
                vehicle,
            })
        })
        .collect()
}

fn string_list(v: &Value) -> Vec<String> {
    v.as_array()
        .map(|a| a.iter().filter_map(|x| x.as_str().map(String::from)).collect())
        .unwrap_or_default()
}

fn bars_msg(on: bool) -> Value {
    json!({"op": "call", "object": "audioAnalyzer", "method": "setDemand", "args": [DEMAND_CONSUMER, on]})
}

fn demand_msg(ids: &[String]) -> Value {
    json!({"op": "call", "object": "obdManager", "method": "setParameterDemand", "args": [DEMAND_CONSUMER, ids]})
}

/// (object, properties, throttleMs). Every subscription sends the current
/// value at once, so these double as the initial reads.
fn subscriptions() -> Vec<(&'static str, Vec<&'static str>, u64)> {
    vec![
        ("parameterCatalog", vec!["displayParameters"], 0),
        (
            "nowPlaying",
            vec!["source", "playing", "hasTrack", "title", "artist", "album", "durationMs", "canSeek", "canShuffle", "shuffled", "artRevision"],
            0,
        ),
        ("nowPlaying", vec!["positionMs"], 500),
        ("volumeController", vec!["percent", "muted", "effectivelyMuted"], 0),
        (
            "obdManager",
            vec!["connected", "connectionState", "connectionStatus", "vehicleSupportedParameters", "vin", "vehicleMake", "vehicleModelYear"],
            0,
        ),
        ("shiftLightManager", vec!["lit", "active"], 0),
        ("tripRecorder", vec!["recording", "currentTripElapsedS", "trips"], 0),
        ("audioAnalyzer", vec!["levels"], 40),
        // `time` is a property on newer backends; an older one refuses this
        // one subscription, and the timeChanged signal below still arrives
        ("clock", vec!["time"], 0),
        // Every reading, merged into one event per 40 ms
        ("paramStore", vec![], 40),
    ]
}

fn signal_subscriptions() -> Vec<(&'static str, Vec<&'static str>)> {
    vec![
        ("clock", vec!["timeChanged"]),
        ("obdManager", vec!["dtcCodesChanged", "milStatusChanged", "freezeFrameChanged", "dtcClearResult"]),
        ("tripRecorder", vec!["detailReady", "exportFinished"]),
    ]
}


/// What a new connection sends, in order: hello, the subscriptions (each
/// sends the current value at once), the trouble codes as they stand, then
/// the polling this cluster wants. Each with the name its answer goes by.
fn opening(token: Option<&str>, demand: &[String], bars: bool) -> Vec<(Value, String)> {
    let mut out = Vec::new();
    let hello = match token {
        Some(t) => json!({"op": "hello", "token": t}),
        None => json!({"op": "hello"}),
    };
    out.push((hello, "hello".to_string()));
    for (object, props, throttle) in subscriptions() {
        let mut msg = json!({"op": "subscribe", "object": object});
        if !props.is_empty() {
            msg["properties"] = json!(props);
        }
        if throttle > 0 {
            msg["throttleMs"] = json!(throttle);
        }
        out.push((msg, format!("subscribe {object}")));
    }
    for (object, signals) in signal_subscriptions() {
        out.push((json!({"op": "subscribe", "object": object, "signals": signals}), format!("subscribe {object}")));
    }
    // Signals only report changes: read the trouble codes as they stand
    for method in ["get_dtc_codes", "get_mil_status", "get_freeze_frame"] {
        out.push((json!({"op": "call", "object": "obdManager", "method": method, "args": []}), format!("obdManager.{method}")));
    }
    out.push((demand_msg(demand), "demand".to_string()));
    // A closed connection released it on the app's side: ask again
    if bars {
        out.push((bars_msg(true), "bars".to_string()));
    }
    out
}

fn call_msg(object: &str, method: &str, args: &[Value]) -> (Value, String) {
    (json!({"op": "call", "object": object, "method": method, "args": args}), format!("{object}.{method}"))
}

/// One message from the app: an event, or the answer to a request in `pending`.
fn handle_line(s: &mut State, pending: &mut HashMap<u64, String>, line: &str) {
    let Ok(msg) = serde_json::from_str::<Value>(line) else { return };
    if msg.get("event").is_some() {
        s.apply_event(&msg);
    } else if let Some(id) = msg["id"].as_u64() {
        let what = pending.remove(&id).unwrap_or_default();
        if msg["ok"] != true {
            s.last_error = Some(format!("{what}: {}", msg["error"].as_str().unwrap_or("error")));
        } else if what != "hello" {
            s.apply_result(&what, &msg["result"]);
        } else {
            let r = &msg["result"];
            s.link = LinkState::Online;
            s.protocol = r["protocolVersion"].as_i64();
            s.app_version = r["app"]["version"].as_str().unwrap_or("").to_string();
            s.headless = r["app"]["headless"].as_bool().unwrap_or(false);
            s.trusted = r["auth"].as_str() != Some("untrusted");
            s.last_error = None;
            // one line per connection: OCTAVE relays it into its log when the cluster is the screen
            eprintln!("retro-cluster: connected, OCTAVE {}, protocol {}, {}",
                      s.app_version, s.protocol.unwrap_or(0), if s.trusted { "trusted" } else { "untrusted" });
        }
    }
}

/// A cover (the bytes of GET /art) as ART_DOTS x ART_DOTS luminance; None
/// for anything that won't decode.
fn art_dots(bytes: &[u8]) -> Option<Vec<u8>> {
    let img = image::load_from_memory(bytes).ok()?;
    let small = img.resize_exact(ART_DOTS, ART_DOTS, image::imageops::FilterType::Triangle).to_luma8();
    Some(small.into_raw())
}
