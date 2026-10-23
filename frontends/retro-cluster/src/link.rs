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

/// The protocol versions this client knows (docs/api/README.md): 2 added
/// pairing, which only matters over the network.
pub const PROTOCOL_VERSIONS: [i64; 2] = [1, 2];

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
                if ev["object"] == "clock" && ev["name"] == "timeChanged" {
                    let text = ev["args"][0].as_str().unwrap_or("").to_string();
                    self.clock = Some(text);
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
    /// `token`: a paired device's, sent with hello (None on this device)
    pub fn start(address: String, token: Option<String>, repaint: impl Fn() + Send + Sync + 'static) -> Link {
        let state = Arc::new(Mutex::new(State::new(address.clone())));
        let (tx, rx) = mpsc::channel();
        let shared = state.clone();
        thread::Builder::new()
            .name("octave-link".into())
            .spawn(move || run(address, token, shared, rx, Arc::new(repaint)))
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

/// Pair with the 6-digit code OCTAVE's Settings > Device > Service shows;
/// the token to pass as --token (or OCTAVE_TOKEN) from then on.
pub fn pair(address: &str, code: &str) -> Result<String, String> {
    let addr = resolve(address).ok_or_else(|| format!("cannot resolve {address}"))?;
    let mut stream = TcpStream::connect_timeout(&addr, Duration::from_secs(3)).map_err(|e| e.to_string())?;
    let code: String = code.chars().filter(|c| c.is_ascii_digit()).collect();
    let mut line = json!({"op": "pair", "code": code, "name": "retro-cluster", "id": 1}).to_string();
    line.push('\n');
    stream.write_all(line.as_bytes()).map_err(|e| e.to_string())?;
    let mut reply = String::new();
    BufReader::new(stream).read_line(&mut reply).map_err(|e| e.to_string())?;
    let r: Value = serde_json::from_str(&reply).map_err(|e| e.to_string())?;
    if r["ok"] == true {
        Ok(r["result"]["token"].as_str().unwrap_or_default().to_string())
    } else {
        Err(r["error"].as_str().unwrap_or("pairing failed").to_string())
    }
}

fn run(address: String, token: Option<String>, state: Arc<Mutex<State>>, rx: Receiver<Cmd>,
       repaint: Arc<dyn Fn() + Send + Sync>) {
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

        let hello = match &token {
            Some(t) => json!({"op": "hello", "token": t}),
            None => json!({"op": "hello"}),
        };
        let mut ok = send(&mut stream, hello, "hello".into());
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
        ok &= send(
            &mut stream,
            json!({"op": "subscribe", "object": "clock", "signals": ["timeChanged"]}),
            "subscribe clock".into(),
        );
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
        ("tripRecorder", vec!["recording", "currentTripElapsedS"], 0),
        // Every reading, merged into one event per 40 ms
        ("paramStore", vec![], 40),
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
            } else if what == "hello" {
                let r = &msg["result"];
                s.link = LinkState::Online;
                s.protocol = r["protocolVersion"].as_i64();
                s.app_version = r["app"]["version"].as_str().unwrap_or("").to_string();
                s.headless = r["app"]["headless"].as_bool().unwrap_or(false);
                s.trusted = r["auth"].as_str() != Some("untrusted");
                s.last_error = None;
            }
        }
        drop(s);
        repaint();
    }
    alive.store(false, Ordering::Relaxed);
    repaint();
}
