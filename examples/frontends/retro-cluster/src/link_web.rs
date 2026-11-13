//! The link of the browser build: the page's own WebSocket to the host that
//! served it, one JSON message per text frame. Only text frames are used, so
//! it also works over the stand-in OCTAVE's browser demo puts in place of
//! WebSocket (packaging/wasm/frontend-host.js), which carries nothing else.
//! Everything runs on the page's one thread, from the socket's callbacks.

use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;
use std::sync::{Arc, Mutex};

use serde_json::{Value, json};
use wasm_bindgen::JsCast;
use wasm_bindgen::prelude::*;
use wasm_bindgen_futures::JsFuture;
use web_sys::{MessageEvent, WebSocket};

use super::{LinkState, State, art_dots, bars_msg, call_msg, demand_msg, handle_line, opening};

/// Where a paired browser keeps its token (a page OCTAVE serves on the head
/// unit, and the demo, need none).
const TOKEN_KEY: &str = "retro-cluster.token";

pub struct Link {
    pub state: Arc<Mutex<State>>,
    inner: Rc<RefCell<Inner>>,
}

struct Inner {
    state: Arc<Mutex<State>>,
    url: String,
    token: Option<String>,
    repaint: Rc<dyn Fn()>,
    ws: Option<WebSocket>,
    open: bool,
    /// The socket's callbacks, kept for as long as it can call them
    handlers: Vec<Closure<dyn FnMut(JsValue)>>,
    next_id: u64,
    pending: HashMap<u64, String>,
    demand: Vec<String>,
    bars: bool,
    art_fetched: i64,
    stopped: bool,
}

impl Inner {
    fn send(&mut self, mut msg: Value, what: String) {
        let Some(ws) = self.ws.as_ref().filter(|_| self.open) else { return };
        self.next_id += 1;
        msg["id"] = json!(self.next_id);
        // send() throws unless the socket is open; its close event follows
        if ws.send_with_str(&msg.to_string()).is_ok() {
            self.pending.insert(self.next_id, what);
        }
    }
}

/// The service as this page sees it: the host that served the page. A page
/// without one (the demo's sandboxed frame, where the stand-in ignores the
/// address) still gets a well-formed URL.
fn service_url() -> (String, String) {
    let location = web_sys::window().map(|w| w.location());
    let secure = location.as_ref().and_then(|l| l.protocol().ok()).is_some_and(|p| p == "https:");
    let host = location.and_then(|l| l.host().ok()).filter(|h| !h.is_empty());
    let shown = host.clone().unwrap_or_else(|| "THIS PAGE".to_string());
    let host = host.unwrap_or_else(|| "127.0.0.1:9876".to_string());
    (format!("{}://{host}/", if secure { "wss" } else { "ws" }), shown)
}

/// Storage that throws (a private window, the demo's sandboxed frame) is no token.
fn stored_token() -> Option<String> {
    let storage = web_sys::window()?.local_storage().ok()??;
    storage.get_item(TOKEN_KEY).ok()?.filter(|t| !t.is_empty())
}

fn later(ms: i32, f: impl FnOnce() + 'static) {
    let cb = Closure::once_into_js(f);
    if let Some(w) = web_sys::window() {
        let _ = w.set_timeout_with_callback_and_timeout_and_arguments_0(cb.unchecked_ref(), ms);
    }
}

impl Link {
    /// `token`: a paired browser's, sent with hello (else the stored one, if any)
    pub fn start(_address: String, token: Option<String>, repaint: impl Fn() + 'static) -> Link {
        let (url, shown) = service_url();
        let state = Arc::new(Mutex::new(State::new(shown)));
        let inner = Rc::new(RefCell::new(Inner {
            state: state.clone(),
            url,
            token: token.or_else(stored_token),
            repaint: Rc::new(repaint),
            ws: None,
            open: false,
            handlers: Vec::new(),
            next_id: 0,
            pending: HashMap::new(),
            demand: Vec::new(),
            bars: false,
            art_fetched: i64::MIN,
            stopped: false,
        }));
        connect(&inner);
        Link { state, inner }
    }

    pub fn call(&self, object: &'static str, method: &'static str, args: Vec<Value>) {
        let (msg, what) = call_msg(object, method, &args);
        self.inner.borrow_mut().send(msg, what);
    }

    pub fn demand(&self, ids: Vec<String>) {
        let mut inner = self.inner.borrow_mut();
        let msg = demand_msg(&ids);
        inner.demand = ids;
        inner.send(msg, "demand".into());
    }

    pub fn bars(&self, wanted: bool) {
        let mut inner = self.inner.borrow_mut();
        inner.bars = wanted;
        inner.send(bars_msg(wanted), "bars".into());
    }
}

impl Drop for Link {
    fn drop(&mut self) {
        let mut inner = self.inner.borrow_mut();
        inner.stopped = true;
        // The app drops this cluster's polling with the connection
        if let Some(ws) = inner.ws.take() {
            let _ = ws.close();
        }
    }
}

fn connect(inner: &Rc<RefCell<Inner>>) {
    if inner.borrow().stopped {
        return;
    }
    let url = inner.borrow().url.clone();
    // a constructor that throws (a blocked socket) is a failed try, not a crash
    let Ok(ws) = WebSocket::new(&url) else {
        let again = inner.clone();
        later(1000, move || connect(&again));
        return;
    };
    let mut handlers: Vec<Closure<dyn FnMut(JsValue)>> = Vec::new();
    let mut on = |event: &str, f: Box<dyn FnMut(JsValue)>| {
        let c = Closure::wrap(f);
        let _ = ws.add_event_listener_with_callback(event, c.as_ref().unchecked_ref());
        handlers.push(c);
    };

    let me = inner.clone();
    on("open", Box::new(move |_| {
        let mut i = me.borrow_mut();
        i.open = true;
        for (msg, what) in opening(i.token.clone().as_deref(), &i.demand.clone(), i.bars) {
            i.send(msg, what);
        }
    }));

    let me = inner.clone();
    on("message", Box::new(move |ev| {
        // a binary media record is not used here
        let Some(text) = ev.dyn_into::<MessageEvent>().ok().and_then(|m| m.data().as_string()) else { return };
        let mut i = me.borrow_mut();
        let state = i.state.clone();
        let mut s = state.lock().unwrap();
        for line in text.lines() {
            handle_line(&mut s, &mut i.pending, line);
        }
        let want = s.art_revision;
        drop(s);
        if want >= 0 && want != i.art_fetched {
            i.art_fetched = want;
            let (state, token, repaint) = (state.clone(), i.token.clone(), i.repaint.clone());
            wasm_bindgen_futures::spawn_local(async move {
                let art = fetch_art(token).await;
                let mut s = state.lock().unwrap();
                if s.art_revision == want {
                    s.art = art.map(|dots| (want, dots));
                }
                drop(s);
                repaint();
            });
        }
        (i.repaint)();
    }));

    // error and close both end it; the second finds the socket already gone
    for event in ["close", "error"] {
        let (me, this) = (inner.clone(), ws.clone());
        on(event, Box::new(move |_| {
            let mut i = me.borrow_mut();
            if i.ws.as_ref() != Some(&this) {
                return;
            }
            let _ = this.close();
            i.ws = None;
            i.open = false;
            i.pending.clear();
            i.art_fetched = i64::MIN;
            {
                let mut s = i.state.lock().unwrap();
                s.link = LinkState::Searching;
                s.params.clear();
            }
            (i.repaint)();
            let again = me.clone();
            // the callbacks of this socket are let go by the next connect()
            later(1000, move || connect(&again));
        }));
    }

    let mut i = inner.borrow_mut();
    i.ws = Some(ws);
    i.open = false;
    i.handlers = handlers;
}

/// GET /art from the host that served the page: the current cover as
/// ART_DOTS square luminance. None for no cover (404), not paired (401), a
/// page with no service behind its address (the demo) or anything that won't
/// decode. The token goes as a Bearer header, never in the URL.
async fn fetch_art(token: Option<String>) -> Option<Vec<u8>> {
    let init = web_sys::RequestInit::new();
    if let Some(t) = token {
        let headers = web_sys::Headers::new().ok()?;
        headers.set("Authorization", &format!("Bearer {t}")).ok()?;
        init.set_headers(&headers);
    }
    let resp = JsFuture::from(web_sys::window()?.fetch_with_str_and_init("/art", &init)).await.ok()?;
    let resp: web_sys::Response = resp.dyn_into().ok()?;
    if !resp.ok() {
        return None;
    }
    let buf = JsFuture::from(resp.array_buffer().ok()?).await.ok()?;
    art_dots(&js_sys::Uint8Array::new(&buf).to_vec())
}
