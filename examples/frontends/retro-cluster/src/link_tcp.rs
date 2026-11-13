//! The link of the program: one TCP connection to OCTAVE carrying JSON
//! lines. A background thread connects, sends what `opening()` lists, folds
//! the answers and events into `State`, and reconnects when the app goes away.

use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::net::{SocketAddr, TcpStream, ToSocketAddrs};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use serde_json::{Value, json};

use super::{State, LinkState, art_dots, bars_msg, call_msg, demand_msg, handle_line, opening};

/// What the UI asks the link to do.
pub enum Cmd {
    Call(&'static str, &'static str, Vec<Value>),
    /// The PIDs the current page shows (obdManager.setParameterDemand).
    Demand(Vec<String>),
    /// Whether the spectrum bars are on screen (audioAnalyzer.setDemand).
    Bars(bool),
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

    pub fn bars(&self, wanted: bool) {
        let _ = self.tx.send(Cmd::Bars(wanted));
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
    let mut bars = false;
    loop {
        let stream = resolve(&address).and_then(|a| TcpStream::connect_timeout(&a, Duration::from_secs(1)).ok());
        let Some(mut stream) = stream else {
            // Keep the demand current while offline, so the reconnect sends the latest
            match rx.recv_timeout(Duration::from_millis(1000)) {
                Ok(Cmd::Demand(ids)) => demand = ids,
                Ok(Cmd::Bars(on)) => bars = on,
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

        let mut art_fetched = i64::MIN;
        let mut next_id = 0u64;
        let mut send = |stream: &mut TcpStream, mut msg: Value, what: String| -> bool {
            next_id += 1;
            msg["id"] = json!(next_id);
            pending.lock().unwrap().insert(next_id, what);
            let mut line = msg.to_string();
            line.push('\n');
            stream.write_all(line.as_bytes()).is_ok()
        };

        let mut ok = true;
        for (msg, what) in opening(token.as_deref(), &demand, bars) {
            ok &= send(&mut stream, msg, what);
        }

        while ok && alive.load(Ordering::Relaxed) {
            match rx.recv_timeout(Duration::from_millis(200)) {
                Ok(Cmd::Call(object, method, args)) => {
                    let (msg, what) = call_msg(object, method, &args);
                    ok = send(&mut stream, msg, what);
                }
                Ok(Cmd::Demand(ids)) => {
                    demand = ids;
                    ok = send(&mut stream, demand_msg(&demand), "demand".into());
                }
                Ok(Cmd::Bars(on)) => {
                    bars = on;
                    ok = send(&mut stream, bars_msg(on), "bars".into());
                }
                Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => {
                    let _ = stream.shutdown(std::net::Shutdown::Both);
                    return;
                }
            }
            // A new cover: fetch it off this thread
            let want = state.lock().unwrap().art_revision;
            if want >= 0 && want != art_fetched {
                art_fetched = want;
                let (address, token, state, repaint) = (address.clone(), token.clone(), state.clone(), repaint.clone());
                thread::spawn(move || {
                    let art = fetch_art(&address, token.as_deref());
                    let mut s = state.lock().unwrap();
                    if s.art_revision == want {
                        s.art = art.map(|dots| (want, dots));
                    }
                    drop(s);
                    repaint();
                });
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

/// GET /art from the service: the current cover, scaled to ART_DOTS square
/// luminance. None for no cover (404), not paired (401) or anything that
/// won't decode. A paired cluster sends its token as a Bearer header
/// (protocol 2), never in the URL.
fn fetch_art(address: &str, token: Option<&str>) -> Option<Vec<u8>> {
    use std::io::Read;
    let addr = resolve(address)?;
    let mut stream = TcpStream::connect_timeout(&addr, Duration::from_secs(2)).ok()?;
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok()?;
    let auth = token.map(|t| format!("Authorization: Bearer {t}\r\n")).unwrap_or_default();
    write!(stream, "GET /art HTTP/1.1\r\nHost: {address}\r\n{auth}Connection: close\r\n\r\n").ok()?;
    let mut reply = Vec::new();
    stream.read_to_end(&mut reply).ok()?;
    let split = reply.windows(4).position(|w| w == b"\r\n\r\n")?;
    let head = String::from_utf8_lossy(&reply[..split]);
    if !head.starts_with("HTTP/1.1 200") {
        return None;
    }
    art_dots(&reply[split + 4..])
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
        let mut s = state.lock().unwrap();
        handle_line(&mut s, &mut pending.lock().unwrap(), &line);
        drop(s);
        repaint();
    }
    alive.store(false, Ordering::Relaxed);
    repaint();
}
