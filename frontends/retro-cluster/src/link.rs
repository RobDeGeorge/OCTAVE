//! The service-protocol client: one TCP connection to OCTAVE carrying JSON
//! lines (wiki/utilities.html#service-protocol, docs/api/). A background
//! thread connects, subscribes to everything the cluster draws, folds the
//! events into `State`, and reconnects when the app goes away. The UI only
//! reads `State` and sends `Cmd`s; it never blocks on the socket.

use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::net::{SocketAddr, TcpStream, ToSocketAddrs};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use serde_json::{Value, json};

/// The protocol version this client was written against (docs/api/README.md).
pub const PROTOCOL_VERSION: i64 = 1;

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
    pub last_error: Option<String>,
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
            last_error: None,
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

/// What the UI asks the link to do.
pub enum Cmd {
    Call(&'static str, &'static str, Vec<Value>),
    /// The PIDs the current page shows (obdManager.setParameterDemand).
    Demand(Vec<String>),
}

pub struct Link {
    pub state: Arc<Mutex<State>>,
    tx: Sender<Cmd>,
}

impl Link {
    pub fn start(address: String, repaint: impl Fn() + Send + Sync + 'static) -> Link {
        let state = Arc::new(Mutex::new(State::new(address.clone())));
        let (tx, rx) = mpsc::channel();
        let shared = state.clone();
        thread::Builder::new()
            .name("octave-link".into())
            .spawn(move || run(address, shared, rx, Arc::new(repaint)))
            .expect("spawn link thread");
        Link { state, tx }
    }

    pub fn call(&self, object: &'static str, method: &'static str, args: Vec<Value>) {
        let _ = self.tx.send(Cmd::Call(object, method, args));
    }

    pub fn demand(&self, ids: Vec<String>) {
        let _ = self.tx.send(Cmd::Demand(ids));
    }
}

impl Drop for Link {
    fn drop(&mut self) {
        // Stop the polling this cluster asked for; the link thread sends it
        // before it notices the channel closed.
        let _ = self.tx.send(Cmd::Demand(Vec::new()));
    }
}

fn resolve(address: &str) -> Option<SocketAddr> {
    address.to_socket_addrs().ok()?.next()
}

fn run(address: String, state: Arc<Mutex<State>>, rx: Receiver<Cmd>, repaint: Arc<dyn Fn() + Send + Sync>) {
    let mut demand: Vec<String> = Vec::new();
    loop {
        let stream = resolve(&address).and_then(|a| TcpStream::connect_timeout(&a, Duration::from_secs(1)).ok());
        let Some(mut stream) = stream else {
            // Keep the demand current while offline, so the reconnect sends the latest
            match rx.recv_timeout(Duration::from_millis(1000)) {
                Ok(Cmd::Demand(ids)) => demand = ids,
                Ok(Cmd::Call(..)) | Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => return,
            }
            continue;
        };
        let _ = stream.set_nodelay(true);

        let alive = Arc::new(AtomicBool::new(true));
        let pending: Arc<Mutex<HashMap<u64, String>>> = Arc::default();
        {
            let reader = stream.try_clone().expect("clone socket");
            let (state, alive, pending, repaint) = (state.clone(), alive.clone(), pending.clone(), repaint.clone());
            thread::spawn(move || read_loop(reader, state, alive, pending, repaint));
        }

        let mut next_id = 0u64;
        let mut send = |stream: &mut TcpStream, mut msg: Value, what: String| -> bool {
            next_id += 1;
            msg["id"] = json!(next_id);
            pending.lock().unwrap().insert(next_id, what);
            let mut line = msg.to_string();
            line.push('\n');
            stream.write_all(line.as_bytes()).is_ok()
        };

        let mut ok = send(&mut stream, json!({"op": "hello"}), "hello".into());
        for (object, props, throttle) in subscriptions() {
            let mut msg = json!({"op": "subscribe", "object": object});
            if !props.is_empty() {
                msg["properties"] = json!(props);
            }
            if throttle > 0 {
                msg["throttleMs"] = json!(throttle);
            }
            ok &= send(&mut stream, msg, format!("subscribe {object}"));
        }
        for (object, signals) in signal_subscriptions() {
            let msg = json!({"op": "subscribe", "object": object, "signals": signals});
            ok &= send(&mut stream, msg, format!("subscribe {object}"));
        }
        // Signals only report changes: read the trouble codes as they stand
        for method in ["get_dtc_codes", "get_mil_status", "get_freeze_frame"] {
            let msg = json!({"op": "call", "object": "obdManager", "method": method, "args": []});
            ok &= send(&mut stream, msg, format!("obdManager.{method}"));
        }
        ok &= send(&mut stream, demand_msg(&demand), "demand".into());

        while ok && alive.load(Ordering::Relaxed) {
            match rx.recv_timeout(Duration::from_millis(200)) {
                Ok(Cmd::Call(object, method, args)) => {
                    let msg = json!({"op": "call", "object": object, "method": method, "args": args});
                    ok = send(&mut stream, msg, format!("{object}.{method}"));
                }
                Ok(Cmd::Demand(ids)) => {
                    demand = ids;
                    ok = send(&mut stream, demand_msg(&demand), "demand".into());
                }
                Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => {
                    let _ = stream.shutdown(std::net::Shutdown::Both);
                    return;
                }
            }
        }
        let _ = stream.shutdown(std::net::Shutdown::Both);
        {
            let mut s = state.lock().unwrap();
            s.link = LinkState::Searching;
            s.params.clear();
        }
        repaint();
        thread::sleep(Duration::from_millis(500));
    }
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
            vec!["source", "playing", "hasTrack", "title", "artist", "album", "durationMs", "canSeek", "canShuffle", "shuffled"],
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

fn read_loop(
    stream: TcpStream,
    state: Arc<Mutex<State>>,
    alive: Arc<AtomicBool>,
    pending: Arc<Mutex<HashMap<u64, String>>>,
    repaint: Arc<dyn Fn() + Send + Sync>,
) {
    let reader = BufReader::new(stream);
    for line in reader.lines() {
        let Ok(line) = line else { break };
        let Ok(msg) = serde_json::from_str::<Value>(&line) else { continue };
        let mut s = state.lock().unwrap();
        if msg.get("event").is_some() {
            s.apply_event(&msg);
        } else if let Some(id) = msg["id"].as_u64() {
            let what = pending.lock().unwrap().remove(&id).unwrap_or_default();
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
                s.last_error = None;
            }
        }
        drop(s);
        repaint();
    }
    alive.store(false, Ordering::Relaxed);
    repaint();
}
