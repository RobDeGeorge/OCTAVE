//! OCTAVE Retro Cluster: an experimental second frontend for OCTAVE, in
//! Rust + egui, styled as a 1980s vacuum-fluorescent instrument cluster.
//! It is a separate program that talks to the C++ backend over the service
//! API (docs/api/), the same objects the QML UI binds to.
//!
//!     ./build/octave --service          (or --headless, or --dev --service)
//!     cargo run --release -- [--host H] [--port P] [--fullscreen] [--theme N]
//!
//! build_web.sh builds the same cluster for a browser (wasm32): a page OCTAVE
//! serves, talking to the service over the page's WebSocket (link_web.rs).

#![allow(clippy::too_many_arguments)]

mod link;
mod vfd;

// std's own, except in a browser, where std has no clock
use web_time::{Duration, Instant};

use eframe::egui::{self, Color32, Key, Painter, Pos2, Rect, Sense, pos2, vec2};
use serde_json::json;

use link::{Link, LinkState, State};
use vfd::{PALETTES, Palette};

/// The layout is drawn on a 1280x800 design canvas (the head unit's
/// screen) and scaled to fit the window.
const DESIGN: egui::Vec2 = egui::Vec2::new(1280.0, 800.0);

/// A window taller than wide (a phone held upright) gets this canvas: the
/// same parts, each at its own place and size (`Draw::place`).
const DESIGN_TALL: egui::Vec2 = egui::Vec2::new(800.0, 1600.0);

const DRIVE_IDS: [&str; 11] = [
    "SPEED",
    "RPM",
    "COOLANT_TEMP",
    "OIL_TEMP",
    "INTAKE_TEMP",
    "FUEL_LEVEL",
    "CONTROL_MODULE_VOLTAGE",
    "ENGINE_LOAD",
    "THROTTLE_POS",
    "MAF",
    "INTAKE_PRESSURE",
];

/// Live data: columns and rows of readings on one page.
fn diag_grid(tall: bool) -> (usize, usize) {
    if tall { (1, 12) } else { (3, 15) }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Page {
    Drive,
    Diag,
    Codes,
    Trips,
    Audio,
}

/// Page-local state the draw code reads and changes.
#[derive(Default)]
struct PageState {
    diag_page: usize,
    /// CLEAR CODES was pressed once; a second press within 4 s clears
    clear_armed: Option<Instant>,
    trip_index: usize,
    /// The trip whose detail was last asked for
    detail_asked: Option<i64>,
}

struct Args {
    address: String,
    token: Option<String>,
    #[cfg_attr(target_arch = "wasm32", allow(dead_code))]
    fullscreen: bool,
    theme: usize,
    page: Page,
}

#[cfg(not(target_arch = "wasm32"))]
fn parse_args() -> Args {
    let mut a = Args { address: String::new(), token: std::env::var("OCTAVE_TOKEN").ok().filter(|t| !t.is_empty()),
                       fullscreen: false, theme: 0, page: Page::Drive };
    let mut pair_code: Option<String> = None;
    // OCTAVE sets OCTAVE_HOST / OCTAVE_PORT / OCTAVE_TOKEN when it runs the cluster as the screen; the flags win
    let env = |k: &str| std::env::var(k).ok().filter(|v| !v.is_empty());
    let mut host = env("OCTAVE_HOST").unwrap_or_else(|| "127.0.0.1".to_string());
    let mut port = env("OCTAVE_PORT").and_then(|p| p.parse().ok()).unwrap_or(9876u16);
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "--host" => host = it.next().unwrap_or(host),
            "--port" => port = it.next().and_then(|p| p.parse().ok()).unwrap_or(port),
            "--token" => a.token = it.next(),
            "--pair" => pair_code = it.next(),
            "--fullscreen" => a.fullscreen = true,
            "--page" => {
                a.page = page_named(it.next().as_deref());
            }
            "--theme" => a.theme = it.next().and_then(|t| t.parse().ok()).unwrap_or(0) % PALETTES.len(),
            "-h" | "--help" => {
                println!("retro-cluster [--host 127.0.0.1] [--port 9876] [--token T] [--fullscreen] [--theme 0|1|2] [--page drive|diag|dtc|trips|audio]");
                println!("              [--pair CODE]   pair with the code in OCTAVE's Settings > Device > Service, print the token");
                println!("Over the network the cluster needs a token (--token, or OCTAVE_TOKEN); on the head unit itself it doesn't.");
                println!("OCTAVE_HOST, OCTAVE_PORT and OCTAVE_TOKEN (set when OCTAVE runs it as the screen) are the defaults.");
                println!("keys: Space play/pause  Left/Right prev/next  +/- volume  M mute");
                println!("      Tab page  T theme  F11 fullscreen  Ctrl+Q quit");
                std::process::exit(0);
            }
            other => eprintln!("retro-cluster: ignoring unknown argument {other}"),
        }
    }
    a.address = format!("{host}:{port}");
    if let Some(code) = pair_code {
        match link::pair(&a.address, &code) {
            Ok(token) => {
                println!("{token}");
                eprintln!("retro-cluster: paired; run with --token <that> (or OCTAVE_TOKEN=<that>)");
                std::process::exit(0);
            }
            Err(e) => {
                eprintln!("retro-cluster: pairing failed: {e}");
                std::process::exit(1);
            }
        }
    }
    a
}

fn page_named(name: Option<&str>) -> Page {
    match name {
        Some("diag") => Page::Diag,
        Some("dtc") => Page::Codes,
        Some("trips") => Page::Trips,
        Some("audio") => Page::Audio,
        _ => Page::Drive,
    }
}

/// The browser build: the cluster on the page's canvas (web/index.html).
/// ?theme=0|1|2 and ?page=drive|diag|dtc|trips|audio are the flags a page has.
#[cfg(target_arch = "wasm32")]
fn main() {
    use wasm_bindgen::JsCast as _;
    let window = web_sys::window().expect("a page");
    let document = window.document().expect("a document");
    let query = window.location().search().ok().and_then(|q| web_sys::UrlSearchParams::new_with_str(&q).ok());
    let param = |k: &str| query.as_ref().and_then(|q| q.get(k));
    let args = Args {
        address: String::new(),
        token: None,
        fullscreen: false,
        theme: param("theme").and_then(|t| t.parse::<usize>().ok()).unwrap_or(0) % PALETTES.len(),
        page: page_named(param("page").as_deref()),
    };
    let canvas = document.get_element_by_id("cluster").and_then(|e| e.dyn_into::<web_sys::HtmlCanvasElement>().ok());
    wasm_bindgen_futures::spawn_local(async move {
        let started = match canvas {
            Some(canvas) => eframe::WebRunner::new()
                .start(canvas, eframe::WebOptions::default(), Box::new(move |cc| Ok(Box::new(Cluster::new(cc, args)))))
                .await
                .map_err(|e| e.as_string().unwrap_or_else(|| format!("{e:?}"))),
            None => Err("the page has no canvas".to_string()),
        };
        // web/index.html's loading line: gone once the cluster draws, else it says why not
        if let Some(loading) = document.get_element_by_id("loading") {
            match started {
                Ok(()) => loading.remove(),
                Err(e) => {
                    loading.set_class_name("failed");
                    loading.set_text_content(Some(&format!("Retro Cluster could not start: {e}")));
                }
            }
        }
    });
}

#[cfg(not(target_arch = "wasm32"))]
fn main() -> eframe::Result {
    let args = parse_args();
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("OCTAVE Retro Cluster")
            .with_app_id("octave-retro-cluster")
            .with_inner_size(DESIGN)
            .with_min_inner_size([640.0, 400.0])
            .with_fullscreen(args.fullscreen),
        ..Default::default()
    };
    let result = eframe::run_native(
        "OCTAVE Retro Cluster",
        options,
        Box::new(move |cc| Ok(Box::new(Cluster::new(cc, args)))),
    );
    // Give the link thread a moment to withdraw this cluster's OBD demand
    std::thread::sleep(Duration::from_millis(150));
    result
}

struct Cluster {
    link: Link,
    page: Page,
    theme: usize,
    started: Instant,
    last_frame: Instant,
    rpm_smooth: f64,
    speed_smooth: f64,
    pages: PageState,
    sent_demand: Option<Vec<String>>,
    sent_bars: Option<bool>,
    peaks: Vec<f32>,
    /// The window is taller than wide
    tall: bool,
    #[cfg_attr(target_arch = "wasm32", allow(dead_code))]
    fullscreen: bool,
}

impl Cluster {
    fn new(cc: &eframe::CreationContext<'_>, args: Args) -> Self {
        let ctx = cc.egui_ctx.clone();
        let link = Link::start(args.address, args.token.clone(), move || ctx.request_repaint());
        Cluster {
            link,
            page: args.page,
            theme: args.theme,
            started: Instant::now(),
            last_frame: Instant::now(),
            rpm_smooth: 0.0,
            speed_smooth: 0.0,
            pages: PageState::default(),
            sent_demand: None,
            sent_bars: None,
            peaks: Vec::new(),
            tall: false,
            fullscreen: args.fullscreen,
        }
    }

    fn diag_ids(&self, s: &State) -> Vec<String> {
        s.catalog_order.iter().filter(|id| s.is_supported(id)).cloned().collect()
    }

    fn update_demand(&mut self, s: &State) {
        let want: Vec<String> = match self.page {
            Page::Drive => DRIVE_IDS.iter().map(|s| s.to_string()).collect(),
            Page::Diag => {
                let all = self.diag_ids(s);
                let (cols, rows) = diag_grid(self.tall);
                let per = cols * rows;
                all.into_iter().skip(self.pages.diag_page * per).take(per).collect()
            }
            // Trouble codes and trips poll nothing; keep the speed for the header
            Page::Codes | Page::Trips | Page::Audio => vec!["SPEED".to_string(), "RPM".to_string()],
        };
        // The analyzer runs only while some frontend shows its bars
        let bars = self.page == Page::Audio;
        if self.sent_bars != Some(bars) {
            self.link.bars(bars);
            self.sent_bars = Some(bars);
        }
        if self.sent_demand.as_ref() != Some(&want) {
            self.link.demand(want.clone());
            self.sent_demand = Some(want);
        }
    }

    fn keys(&mut self, ui: &egui::Ui) {
        let (vol, page) = (self.link.state.lock().unwrap().volume, self.page);
        #[cfg(not(target_arch = "wasm32"))]
        let mut quit = false;
        ui.input(|i| {
            if i.key_pressed(Key::Space) {
                self.link.call("nowPlaying", "togglePlay", vec![]);
            }
            if i.key_pressed(Key::ArrowRight) {
                self.link.call("nowPlaying", "next", vec![]);
            }
            if i.key_pressed(Key::ArrowLeft) {
                self.link.call("nowPlaying", "previous", vec![]);
            }
            if i.key_pressed(Key::Plus) || i.key_pressed(Key::Equals) || i.key_pressed(Key::ArrowUp) {
                self.link.call("volumeController", "applyVolume", vec![json!((vol + 5).min(100))]);
            }
            if i.key_pressed(Key::Minus) || i.key_pressed(Key::ArrowDown) {
                self.link.call("volumeController", "applyVolume", vec![json!((vol - 5).max(0))]);
            }
            if i.key_pressed(Key::M) {
                self.link.call("volumeController", "toggleMute", vec![]);
            }
            if i.key_pressed(Key::Tab) {
                self.page = match page {
                    Page::Drive => Page::Diag,
                    Page::Diag => Page::Codes,
                    Page::Codes => Page::Trips,
                    Page::Trips => Page::Audio,
                    Page::Audio => Page::Drive,
                };
            }
            if i.key_pressed(Key::T) {
                self.theme = (self.theme + 1) % PALETTES.len();
            }
            // a page has no window of its own to resize or close
            #[cfg(not(target_arch = "wasm32"))]
            if i.key_pressed(Key::F11) {
                self.fullscreen = !self.fullscreen;
                ui.ctx().send_viewport_cmd(egui::ViewportCommand::Fullscreen(self.fullscreen));
            }
            #[cfg(not(target_arch = "wasm32"))]
            if i.modifiers.ctrl && i.key_pressed(Key::Q) {
                quit = true;
            }
        });
        #[cfg(not(target_arch = "wasm32"))]
        if quit {
            ui.ctx().send_viewport_cmd(egui::ViewportCommand::Close);
        }
    }
}

/// Maps the design canvas onto the window, letterboxed.
#[derive(Clone, Copy)]
struct Canvas {
    origin: Pos2,
    scale: f32,
}

impl Canvas {
    fn fit(screen: Rect, design: egui::Vec2) -> Canvas {
        let scale = (screen.width() / design.x).min(screen.height() / design.y);
        let size = design * scale;
        Canvas { origin: screen.center() - size / 2.0, scale }
    }
    /// A canvas that draws design point `src` at this one's `dst`, `k` times the size.
    fn sub(&self, src: (f32, f32), dst: (f32, f32), k: f32) -> Canvas {
        Canvas { origin: self.origin + vec2(dst.0 - src.0 * k, dst.1 - src.1 * k) * self.scale, scale: self.scale * k }
    }
    fn r(&self, x: f32, y: f32, w: f32, h: f32) -> Rect {
        Rect::from_min_size(self.p(x, y), vec2(w, h) * self.scale)
    }
    fn p(&self, x: f32, y: f32) -> Pos2 {
        self.origin + vec2(x, y) * self.scale
    }
    fn s(&self, v: f32) -> f32 {
        v * self.scale
    }
}

impl eframe::App for Cluster {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let now = Instant::now();
        let dt = now.duration_since(self.last_frame).as_secs_f64().min(0.2);
        self.last_frame = now;
        let t = self.started.elapsed().as_secs_f64();

        self.keys(ui);
        let screen = ui.max_rect();
        self.tall = screen.height() > screen.width();
        let design = if self.tall { DESIGN_TALL } else { DESIGN };

        let state_handle = self.link.state.clone();
        let s = state_handle.lock().unwrap();
        self.update_demand(&s);

        let pal = PALETTES[self.theme];
        let painter = ui.painter().clone();
        painter.rect_filled(screen, 0.0, Color32::BLACK);
        let cv = Canvas::fit(screen, design);
        painter.rect_filled(cv.r(0.0, 0.0, design.x, design.y), 0.0, pal.bg);

        // The bulb check: every segment sweeps up and back for 1.6 s at start
        let self_test = (t < 1.6).then(|| (std::f64::consts::PI * t / 1.6).sin());
        let online = s.link == LinkState::Online;

        let k = 1.0 - (-dt * 10.0).exp();
        let rpm_target = self_test.map(|f| f * 8000.0).or(s.display("RPM")).unwrap_or(0.0);
        self.rpm_smooth += (rpm_target - self.rpm_smooth) * k;
        let speed_target = s.display("SPEED").unwrap_or(0.0);
        self.speed_smooth += (speed_target - self.speed_smooth) * k;

        let mut ctx = Draw { ui, painter: &painter, cv, base: cv, tall: self.tall, pal, s: &s, t, self_test, online };
        let mut actions: Vec<Action> = Vec::new();
        ctx.header(self.page, &mut actions);
        match self.page {
            Page::Drive => {
                ctx.tach(self.rpm_smooth);
                ctx.gauges(self.speed_smooth);
            }
            Page::Diag => {
                let ids = self.diag_ids(&s);
                ctx.diag(&ids, &mut self.pages.diag_page, &mut actions);
            }
            Page::Codes => ctx.codes(&mut self.pages, &mut actions),
            Page::Trips => ctx.trips(&mut self.pages, &mut actions),
            Page::Audio => ctx.audio(&mut self.peaks, dt as f32),
        }
        ctx.deck(&mut actions);
        if !online && self_test.is_none() {
            ctx.no_signal();
        }
        vfd::scanlines(&painter, cv.r(0.0, 0.0, design.x, design.y), cv.s(3.0).max(2.0));
        let volume = s.volume;
        drop(s);

        for a in actions {
            match a {
                Action::Page(p) => self.page = p,
                Action::Theme => self.theme = (self.theme + 1) % PALETTES.len(),
                Action::Call(o, m, args) => self.link.call(o, m, args),
                Action::Volume(d) => self.link.call("volumeController", "applyVolume", vec![json!((volume + d).clamp(0, 100))]),
            }
        }
        ui.ctx().request_repaint_after(Duration::from_millis(33));
    }
}

/// HH:MM on this device's clock.
#[cfg(not(target_arch = "wasm32"))]
fn local_time() -> String {
    chrono::Local::now().format("%H:%M").to_string()
}

#[cfg(target_arch = "wasm32")]
fn local_time() -> String {
    let now = js_sys::Date::new_0();
    format!("{:02}:{:02}", now.get_hours(), now.get_minutes())
}

enum Action {
    Page(Page),
    Theme,
    Call(&'static str, &'static str, Vec<serde_json::Value>),
    Volume(i64),
}

struct Draw<'a> {
    ui: &'a mut egui::Ui,
    painter: &'a Painter,
    /// Where the part being drawn goes (`place`)
    cv: Canvas,
    /// The whole window's canvas
    base: Canvas,
    tall: bool,
    pal: Palette,
    s: &'a State,
    t: f64,
    self_test: Option<f64>,
    online: bool,
}

impl Draw<'_> {
    /// Where the next part is drawn. Every part is written in the wide
    /// layout's coordinates; in an upright window the part whose corner is
    /// `src` there goes to `dst` on the tall canvas, `k` times the size.
    fn place(&mut self, src: (f32, f32), dst: (f32, f32), k: f32) {
        self.cv = if self.tall { self.base.sub(src, dst, k) } else { self.base };
    }

    /// A body page's frame (the area between the header and the deck): its
    /// width and height in the page's own units. Upright it is `k` times the size.
    fn body(&mut self, k: f32) -> (f32, f32) {
        self.place((30.0, 78.0), (10.0, 170.0), k);
        let (w, h) = if self.tall { (780.0 / k, 954.0 / k) } else { (1220.0, 492.0) };
        vfd::window(self.painter, self.cv.r(30.0, 78.0, w, h), &self.pal);
        (w, h)
    }

    /// Text broken every `per` characters, `dy` apart.
    fn lines(&self, x: f32, y: f32, p: f32, text: &str, per: usize, dy: f32, color: Color32) {
        let chars: Vec<char> = text.chars().collect();
        for (i, line) in chars.chunks(per).enumerate() {
            self.label(x, y + i as f32 * dy, p, &line.iter().collect::<String>(), color);
        }
    }

    /// A dot-matrix label at design coordinates, pitch in design units.
    fn label(&self, x: f32, y: f32, p: f32, text: &str, color: Color32) {
        vfd::text(self.painter, self.cv.p(x, y), self.cv.s(p), text, color, None);
    }
    fn label_right(&self, x: f32, y: f32, p: f32, text: &str, color: Color32) {
        vfd::text_right(self.painter, self.cv.p(x, y), self.cv.s(p), text, color);
    }
    fn dim(&self) -> Color32 {
        self.pal.lit.gamma_multiply(0.55)
    }

    /// A key at design coordinates; true when tapped.
    fn key(&mut self, id: &str, x: f32, y: f32, w: f32, h: f32, label: &str, active: bool) -> bool {
        let r = self.cv.r(x, y, w, h);
        let resp = self.ui.interact(r, egui::Id::unique(("key", id)), Sense::click());
        vfd::key(self.painter, r, label, active, resp.is_pointer_button_down_on(), &self.pal);
        resp.clicked()
    }

    /// The display value of `id`, or the self-test sweep across its range.
    fn value(&self, id: &str) -> Option<f64> {
        let info = self.s.catalog.get(id);
        if let Some(f) = self.self_test {
            let (min, max) = info.map(|i| (i.min, i.max)).unwrap_or((0.0, 100.0));
            return Some(min + (max - min) * f);
        }
        self.s.display(id)
    }

    fn frac(&self, id: &str) -> Option<f64> {
        let v = self.value(id)?;
        let info = self.s.catalog.get(id)?;
        Some(((v - info.min) / (info.max - info.min)).clamp(0.0, 1.0))
    }

    fn unit(&self, id: &str) -> String {
        self.s.catalog.get(id).map(|i| i.unit.clone()).unwrap_or_default()
    }

    fn header(&mut self, page: Page, actions: &mut Vec<Action>) {
        let pal = self.pal;
        // Upright: name, lamps and clock in one row, the tabs larger in a row of their own
        self.place((36.0, 12.0), (24.0, 12.0), 1.0);
        self.label(36.0, 16.0, 4.0, "OCTAVE", pal.lit);
        self.label(38.0, 50.0, 1.7, "RETRO CLUSTER", self.dim());

        let tabs = [
            ("DRIVE", Page::Drive, 84.0),
            ("DIAG", Page::Diag, 72.0),
            ("DTC", Page::Codes, 64.0),
            ("TRIPS", Page::Trips, 80.0),
            ("AUDIO", Page::Audio, 80.0),
        ];
        self.place((196.0, 12.0), (30.0, 80.0), 1.5);
        let mut x = 196.0;
        for (name, p, w) in tabs {
            if self.key(name, x, 12.0, w, 46.0, name, page == p) {
                actions.push(Action::Page(p));
            }
            x += w + 6.0;
        }
        if self.key("theme", x, 12.0, 84.0, 46.0, pal.name, false) {
            actions.push(Action::Theme);
        }

        let test = self.self_test.is_some();
        let rx = self.s.last_rx.is_some_and(|at| at.elapsed() < Duration::from_millis(120));
        let lamps: [(&str, bool, Color32); 5] = [
            ("LINK", self.online || test, pal.lit),
            ("RX", rx || test, pal.lit),
            ("OBD", self.s.obd_connected || test, pal.lit),
            ("REC", self.s.recording || test, pal.hot),
            ("SHIFT", (self.s.shift_active && self.s.shift_lit) || test, pal.hot),
        ];
        self.place((760.0, 12.0), (250.0, 12.0), 1.0);
        let mut x = 760.0;
        for (name, on, color) in lamps {
            let w = 14.0 + name.len() as f32 * 11.0;
            vfd::lamp(self.painter, self.cv.r(x, 20.0, w, 30.0), name, on, color, &pal);
            x += w + 6.0;
        }

        // The clock: the app's own string (clock.time, in the user's 12/24 h
        // format); local time only against a backend too old to send it
        let text = self.s.clock.clone().filter(|c| !c.is_empty()).unwrap_or_else(local_time);
        let digits: String = text.chars().filter(|c| c.is_ascii_digit()).take(4).collect();
        self.place((1104.0, 12.0), (620.0, 12.0), 1.0);
        let r = self.cv.r(1104.0, 14.0, 132.0, 42.0);
        let cw = r.width() / 4.0;
        for (i, c) in format!("{digits:>4}").chars().enumerate() {
            let x0 = r.min.x + i as f32 * cw + if i >= 2 { cw * 0.18 } else { 0.0 };
            vfd::seg_digit(self.painter, Rect::from_min_size(pos2(x0, r.min.y), vec2(cw * 0.86, r.height())), c, false, &pal, pal.lit);
        }
        let colon_on = (self.t * 2.0) as i64 % 2 == 0;
        let cx = r.min.x + cw * 2.05;
        for dy in [0.32, 0.68] {
            self.painter.circle_filled(pos2(cx, r.min.y + r.height() * dy), self.cv.s(2.6), if colon_on { pal.lit } else { pal.ghost });
        }
        if text.ends_with("AM") || text.ends_with("PM") {
            self.label(1242.0, 16.0, 1.4, &text[text.len() - 2..], pal.lit);
        }
    }

    /// The sweep tachometer: a row of segments that grow taller toward the
    /// redline, like a mid-80s digital dash.
    fn tach(&mut self, rpm: f64) {
        let pal = self.pal;
        let max = self.s.catalog.get("RPM").map(|i| i.max).unwrap_or(8000.0).max(1000.0);
        // Upright: half the segments, each nearly twice the size
        self.place((30.0, 78.0), (16.0, 170.0), 1.2);
        let (w, n) = if self.tall { (640.0f32, 36usize) } else { (1220.0, 72) };
        let frame = self.cv.r(30.0, 78.0, w, 150.0);
        vfd::window(self.painter, frame, &pal);
        let (x0, x1, base) = (56.0f32, 30.0 + w - 26.0, 186.0f32);
        let pitch = (x1 - x0) / n as f32;
        let shift = self.s.shift_active && self.s.shift_lit;
        let lit_n = ((rpm / max).clamp(0.0, 1.0) * n as f64).round() as usize;
        for i in 0..n {
            let f = (i as f32 + 0.5) / n as f32;
            let h = 26.0 + 66.0 * f.powf(1.6);
            let r = self.cv.r(x0 + i as f32 * pitch, base - h, pitch * 0.66, h);
            let color = if shift || f > 0.85 {
                pal.hot
            } else if f > 0.7 {
                pal.warn
            } else {
                pal.lit
            };
            if i < lit_n {
                self.painter.rect_filled(r.expand(self.cv.s(2.0)), 2.0, color.gamma_multiply(0.14));
                self.painter.rect_filled(r, 1.0, color);
            } else {
                self.painter.rect_filled(r, 1.0, pal.ghost);
            }
        }
        // Scale: one mark per 1000 rpm
        let marks = (max / 1000.0).round() as usize;
        for m in 0..=marks {
            let x = x0 + (x1 - x0) * m as f32 / marks as f32;
            let c = if m as f64 * 1000.0 >= max * 0.85 { pal.hot } else { self.dim() };
            vfd::text_center(self.painter, self.cv.p(x, 204.0), self.cv.s(2.0), &m.to_string(), c);
        }
        self.label(56.0, 92.0, 1.8, "RPM X1000", self.dim());
        let rpm_text = if self.online || self.self_test.is_some() { format!("{:>5.0}", rpm) } else { "  ---".into() };
        self.label_right(x1, 92.0, 1.8, &rpm_text, pal.lit);
    }

    fn gauges(&mut self, speed: f64) {
        let pal = self.pal;
        // Left: four ladders. Upright: the speed first, the ladders and the trip computer side by side under it
        self.place((30.0, 248.0), (17.0, 760.0), 1.13);
        let left = self.cv.r(30.0, 248.0, 330.0, 322.0);
        vfd::window(self.painter, left, &pal);
        let oil = if self.s.is_supported("OIL_TEMP") { "OIL_TEMP" } else { "INTAKE_TEMP" };
        let ladders: [(&str, bool); 4] = [("COOLANT_TEMP", false), (oil, false), ("FUEL_LEVEL", true), ("CONTROL_MODULE_VOLTAGE", true)];
        for (i, (id, low_bad)) in ladders.into_iter().enumerate() {
            let x = 50.0 + i as f32 * 78.0;
            let info = self.s.catalog.get(id);
            let name = info.map(|i| if i.short_title.is_empty() { i.title.clone() } else { i.short_title.clone() }).unwrap_or_default();
            vfd::text_center(self.painter, self.cv.p(x + 26.0, 270.0), self.cv.s(2.0), &name, self.dim());
            let supported = self.s.is_supported(id);
            let frac = if supported { self.frac(id) } else { None };
            let color_at = |f: f64| {
                if low_bad {
                    if f < 0.12 { pal.hot } else if f < 0.25 { pal.warn } else { pal.lit }
                } else if f > 0.88 {
                    pal.hot
                } else if f > 0.75 {
                    pal.warn
                } else {
                    pal.lit
                }
            };
            vfd::ladder(self.painter, self.cv.r(x, 288.0, 52.0, 230.0), 18, frac, color_at, &pal);
            let text = match (supported, self.value(id)) {
                (false, _) => "N/A".to_string(),
                (true, None) => "--".to_string(),
                (true, Some(v)) => format!("{:.*}", info.map(|i| i.decimals.min(1)).unwrap_or(0), v),
            };
            vfd::text_center(self.painter, self.cv.p(x + 26.0, 534.0), self.cv.s(2.0), &text, pal.lit);
            vfd::text_center(self.painter, self.cv.p(x + 26.0, 554.0), self.cv.s(1.8), &self.unit(id), self.dim());
        }

        // Centre: the big speed readout
        self.place((380.0, 248.0), (88.0, 362.0), 1.2);
        let mid = self.cv.r(380.0, 248.0, 520.0, 322.0);
        vfd::window(self.painter, mid, &pal);
        let speed_v = if self.self_test.is_some() { Some(888.0) } else if self.online { self.s.display("SPEED").map(|_| speed.round()) } else { None };
        vfd::seg_number(self.painter, self.cv.r(404.0, 270.0, 400.0, 210.0), 3, speed_v, 0, &pal, pal.lit);
        let unit = self.unit("SPEED");
        self.label(818.0, 456.0, 2.6, &unit.to_uppercase(), self.dim());
        // Trip timer (tripRecorder's own 1 s tick)
        let e = self.s.trip_elapsed_s;
        let trip = if self.s.recording { format!("TRIP {:02}:{:02}:{:02}", e / 3600, (e / 60) % 60, e % 60) } else { "TRIP --:--:--".into() };
        self.label(420.0, 512.0, 2.6, &trip, if self.s.recording { pal.lit } else { pal.ghost });
        let vehicle = match (self.s.vehicle_year, self.s.vehicle_make.as_str()) {
            (y, m) if y > 0 && !m.is_empty() => format!("{y} {m}"),
            (_, m) if !m.is_empty() => m.to_string(),
            _ => "NO VEHICLE".into(),
        };
        self.label(420.0, 542.0, 2.0, &vehicle, self.dim());

        // Right: the trip computer
        self.place((920.0, 248.0), (410.0, 760.0), 1.13);
        let right = self.cv.r(920.0, 248.0, 330.0, 322.0);
        vfd::window(self.painter, right, &pal);
        let rows = ["ENGINE_LOAD", "THROTTLE_POS", "MAF", "INTAKE_PRESSURE"];
        for (i, id) in rows.into_iter().enumerate() {
            let y = 266.0 + i as f32 * 76.0;
            let info = self.s.catalog.get(id);
            // Titles come from the backend's catalog; offline there are none
            let name = info.map(|i| i.title.to_uppercase()).unwrap_or_default();
            self.label(944.0, y, 2.0, &name, self.dim());
            let v = if self.s.is_supported(id) { self.value(id) } else { None };
            let dec = info.map(|i| i.decimals.min(1)).unwrap_or(0);
            vfd::seg_number(self.painter, self.cv.r(944.0, y + 18.0, 190.0, 46.0), 5, v, dec, &pal, pal.lit);
            self.label(1150.0, y + 44.0, 2.0, &self.unit(id), self.dim());
            vfd::strip(self.painter, self.cv.r(1150.0, y + 22.0, 80.0, 12.0), 8, self.frac(id).unwrap_or(0.0), pal.lit, &pal);
        }
    }

    fn diag(&mut self, ids: &[String], page: &mut usize, actions: &mut Vec<Action>) {
        let pal = self.pal;
        // Upright: one column, nearly twice the size
        let (w, _) = self.body(1.9);
        let tall = self.tall;
        let (cols, rows) = diag_grid(tall);
        let per = cols * rows;
        let pages = ids.len().div_ceil(per).max(1);
        *page = (*page).min(pages - 1);
        self.label(52.0, 94.0, 2.2, "LIVE DATA", pal.lit);
        let summary = format!("{} PIDS  PAGE {}/{}", ids.len(), *page + 1, pages);
        let (sx, sy) = if tall { (52.0, 122.0) } else { (240.0, 96.0) };
        self.label(sx, sy, 1.8, &summary, self.dim());

        let xr = 30.0 + w;
        if self.key("pgup", xr - 210.0, 86.0, 90.0, 36.0, "▲", *page > 0) && *page > 0 {
            *page -= 1;
        }
        if self.key("pgdn", xr - 108.0, 86.0, 90.0, 36.0, "▼", *page + 1 < pages) && *page + 1 < pages {
            *page += 1;
        }

        let col_w = (w - 40.0) / cols as f32;
        let y0 = if tall { 156.0 } else { 140.0 };
        for (n, id) in ids.iter().skip(*page * per).take(per).enumerate() {
            let (col, row) = (n / rows, n % rows);
            let x = 52.0 + col as f32 * col_w;
            let y = y0 + row as f32 * 27.0;
            let info = self.s.catalog.get(id);
            let name: String = info.map(|i| i.title.clone()).unwrap_or_else(|| id.clone()).chars().take(19).collect();
            let value = self.s.display(id);
            self.label(x, y, 1.9, &name, if value.is_some() { self.dim() } else { pal.ghost });
            let text = match (value, info) {
                (Some(v), Some(i)) => format!("{:.*} {}", i.decimals, v, i.unit),
                (Some(v), None) => format!("{v}"),
                (None, _) => "--".into(),
            };
            self.label_right(x + col_w - 30.0, y, 1.9, &text, if value.is_some() { pal.lit } else { pal.ghost });
        }

        let s = self.s;
        let proto = s.protocol.map(|p| p.to_string()).unwrap_or_else(|| "?".into());
        let mut footer = format!("OCTAVE {}  PROTOCOL {proto}  @ {}", s.app_version, s.address);
        if s.headless {
            footer.push_str("  HEADLESS");
        }
        footer.push_str(&format!("   OBD: {}", s.obd_status));
        if !s.vin.is_empty() {
            footer.push_str(&format!("  VIN {}", s.vin));
        }
        let footer: String = footer.chars().take(if tall { 132 } else { 118 }).collect();
        // (line length, the footer's first line, the warning's)
        let (per_line, fy, wy) = if tall { (44, 524.0, 488.0) } else { (118, 548.0, 530.0) };
        self.lines(52.0, fy, 1.4, &footer, per_line, 16.0, self.dim());
        if let Some(p) = s.protocol.filter(|p| !link::PROTOCOL_VERSIONS.contains(p)) {
            self.lines(52.0, wy, 1.4, &format!("WARNING: UNKNOWN PROTOCOL {p}; THIS CLUSTER KNOWS 1 TO 3"), per_line, 16.0, pal.hot);
        } else if !s.trusted {
            self.lines(52.0, wy, 1.4, "NOT PAIRED: RUN RETRO-CLUSTER --PAIR CODE (SETTINGS > DEVICE > SERVICE)", per_line, 16.0, pal.hot);
        }
        let _ = actions;
    }

    /// Trouble codes: the check-engine lamp, the stored codes, the freeze
    /// frame, and a two-press clear.
    fn codes(&mut self, ps: &mut PageState, actions: &mut Vec<Action>) {
        let pal = self.pal;
        let s = self.s;
        // Upright: the keys and the freeze frame under the list, side by side
        self.body(1.3);
        let tall = self.tall;

        let mil = s.mil || self.self_test.is_some();
        let blink = (self.t * 1.5) as i64 % 2 == 0;
        vfd::lamp(self.painter, self.cv.r(56.0, 100.0, 330.0, 70.0), "CHECK ENGINE", mil && blink, pal.warn, &pal);
        self.label(420.0, 104.0, 2.0, "STORED CODES", self.dim());
        vfd::seg_number(self.painter, self.cv.r(420.0, 124.0, 90.0, 48.0), 2, self.online.then_some(s.dtcs.len() as f64), 0, &pal, pal.lit);

        if s.dtcs.is_empty() {
            let msg = if self.online { "NO CODES STORED" } else { "--" };
            self.label(56.0, 220.0, 4.0, msg, pal.lit);
        }
        for (i, d) in s.dtcs.iter().take(6).enumerate() {
            let y = 200.0 + i as f32 * 52.0;
            self.label(56.0, y, 4.5, &d.code, pal.warn);
            let desc = if d.description.is_empty() { "NO DESCRIPTION FROM THE ECU" } else { &d.description };
            let desc: String = desc.chars().take(if tall { 30 } else { 52 }).collect();
            self.label(240.0, y + 10.0, 2.0, &desc, self.dim());
        }
        if s.dtcs.len() > 6 {
            self.label(56.0, 516.0, 2.0, &format!("+{} MORE", s.dtcs.len() - 6), self.dim());
        }

        // Keys on the right; the freeze frame under them (upright: beside them)
        let (kx, ky, fx, fy, fr) = if tall { (56.0, 540.0, 410.0, 540.0, 606.0) } else { (900.0, 100.0, 900.0, 404.0, 1226.0) };
        if self.key("read", kx, ky, 330.0, 64.0, "READ CODES", false) {
            actions.push(Action::Call("obdManager", "read_dtc", vec![]));
        }
        if self.key("freeze", kx, ky + 80.0, 330.0, 64.0, "FREEZE FRAME", !s.freeze.is_empty()) {
            actions.push(Action::Call("obdManager", "read_freeze_frame", vec![]));
        }
        let armed = ps.clear_armed.is_some_and(|at| at.elapsed() < Duration::from_secs(4));
        if !armed {
            ps.clear_armed = None;
        }
        let clear_label = if armed { "PRESS AGAIN TO CLEAR" } else { "CLEAR CODES" };
        if self.key("clear", kx, ky + 160.0, 330.0, 64.0, clear_label, armed) {
            if armed {
                actions.push(Action::Call("obdManager", "clear_dtc", vec![]));
                ps.clear_armed = None;
            } else {
                ps.clear_armed = Some(Instant::now());
            }
        }
        if armed {
            self.label(kx, ky + 234.0, 1.8, "ERASES CODES + FREEZE FRAME", pal.hot);
        }
        if let Some((ok, msg, at)) = &s.dtc_result
            && at.elapsed() < Duration::from_secs(10)
        {
            let text: String = format!("{} {}", if *ok { "CLEARED." } else { "CLEAR FAILED:" }, msg).chars().take(30).collect();
            self.label(kx, ky + 260.0, 1.8, &text, if *ok { pal.lit } else { pal.hot });
        }
        self.label(fx, fy, 2.0, "FREEZE FRAME", self.dim());
        if s.freeze.is_empty() {
            self.label(fx, fy + 26.0, 2.6, "NONE", pal.ghost);
        }
        // The first frame's code, then its readings in the user's units
        if let Some(d) = s.freeze.first() {
            self.label(fx, fy + 26.0, 2.6, &d.code, pal.lit);
            let preferred = ["RPM", "SPEED", "COOLANT_TEMP", "ENGINE_LOAD", "MAF", "INTAKE_PRESSURE"];
            let mut shown: Vec<&(String, f64)> = preferred.iter().filter_map(|id| d.values.iter().find(|(k, _)| k == id)).collect();
            shown.extend(d.values.iter().filter(|(k, _)| !preferred.contains(&k.as_str())));
            for (i, (id, v)) in shown.iter().take(4).enumerate() {
                let info = s.catalog.get(id);
                let name = info.map(|i| if i.short_title.is_empty() { i.title.clone() } else { i.short_title.clone() }).unwrap_or_else(|| id.clone());
                let (val, unit) = info.map(|i| (v * i.scale + i.offset, i.unit.clone())).unwrap_or((*v, String::new()));
                let y = fy + 58.0 + i as f32 * 22.0;
                self.label(fx, y, 1.8, &name.to_uppercase(), self.dim());
                self.label_right(fr, y, 1.8, &format!("{:.*} {}", info.map(|i| i.decimals.min(1)).unwrap_or(0), val, unit), pal.lit);
            }
        }
    }

    /// The black box: recorded drives, and one drive's summary.
    fn trips(&mut self, ps: &mut PageState, actions: &mut Vec<Action>) {
        let pal = self.pal;
        let s = self.s;
        // Upright: a shorter list, the drive's summary under it
        self.body(1.2);
        // (rows listed, the arrows' x, the lower arrow's y, the summary's x, its shift down, its right edge)
        let (rows, ax, ay, x, dy, xr) = if self.tall { (7usize, 612.0, 338.0, 56.0, 300.0, 660.0) } else { (11usize, 620.0, 470.0, 720.0, 0.0, 1226.0) };
        self.label(52.0, 94.0, 2.2, "BLACK BOX", pal.lit);
        self.label(240.0, 96.0, 1.8, &format!("{} DRIVES", s.trips.len()), self.dim());

        // Newest first
        let trips: Vec<&link::Trip> = s.trips.iter().rev().collect();
        if trips.is_empty() {
            self.label(56.0, 150.0, 3.0, if self.online { "NO DRIVES RECORDED" } else { "--" }, pal.ghost);
            return;
        }
        ps.trip_index = ps.trip_index.min(trips.len() - 1);
        let first = ps.trip_index.saturating_sub(rows - 1);
        for (n, t) in trips.iter().enumerate().skip(first).take(rows) {
            let y = 132.0 + (n - first) as f32 * 38.0;
            let r = self.cv.r(46.0, y - 6.0, 560.0, 34.0);
            let selected = n == ps.trip_index;
            if selected {
                self.painter.rect_filled(r, 4.0, pal.ghost);
            }
            if self.ui.interact(r, egui::Id::unique(("trip", t.id)), Sense::click()).clicked() {
                ps.trip_index = n;
            }
            let when = t.start_iso.replace('T', " ");
            let when: String = when.chars().take(16).collect();
            let dur = t.duration_s as i64;
            let dur = format!("{}:{:02}:{:02}", dur / 3600, (dur / 60) % 60, dur % 60);
            let c = if selected { pal.lit } else { self.dim() };
            self.label(56.0, y, 2.2, &when, c);
            self.label(290.0, y, 2.2, &dur, c);
            if t.open {
                self.label(420.0, y, 2.2, "REC", pal.hot);
            } else {
                self.label(420.0, y, 2.2, &t.vehicle.to_uppercase().chars().take(14).collect::<String>(), c);
            }
        }
        if self.key("tup", ax, 132.0, 64.0, 60.0, "▲", ps.trip_index > 0) && ps.trip_index > 0 {
            ps.trip_index -= 1;
        }
        if self.key("tdn", ax, ay, 64.0, 60.0, "▼", ps.trip_index + 1 < trips.len()) && ps.trip_index + 1 < trips.len() {
            ps.trip_index += 1;
        }

        let trip = trips[ps.trip_index];
        if ps.detail_asked != Some(trip.id) {
            ps.detail_asked = Some(trip.id);
            actions.push(Action::Call("tripRecorder", "requestDetail", vec![json!(trip.id)]));
        }
        self.label(x, 132.0 + dy, 2.0, "SUMMARY", self.dim());
        let Some(d) = s.trip_details.get(&trip.id) else {
            self.label(x, 170.0 + dy, 2.4, "READING...", pal.ghost);
            return;
        };
        // The summary is in native units; show the user's
        let conv = |id: &str, v: Option<f64>| -> Option<(f64, String)> {
            let v = v?;
            Some(match s.catalog.get(id) {
                Some(i) => (v * i.scale + i.offset, i.unit.clone()),
                None => (v, String::new()),
            })
        };
        let f = |k: &str| d[k].as_f64();
        let fmt = |v: Option<(f64, String)>, dec: usize| v.map(|(v, u)| format!("{v:.dec$} {u}")).unwrap_or_else(|| "--".into());
        let miles = f("distanceMiles");
        let rows: Vec<(&str, String)> = vec![
            ("DURATION", { let x = f("durationS").unwrap_or(0.0) as i64; format!("{}:{:02}:{:02}", x / 3600, (x / 60) % 60, x % 60) }),
            ("DISTANCE", miles.map(|m| format!("{m:.1} MI")).unwrap_or_else(|| "--".into())),
            ("MAX SPEED", fmt(conv("SPEED", f("maxSpeed")), 0)),
            ("MAX RPM", fmt(conv("RPM", f("maxRpm")), 0)),
            ("MAX COOLANT", fmt(conv("COOLANT_TEMP", f("maxCoolant")), 0)),
            ("VOLTAGE", match (f("minVoltage"), f("maxVoltage")) { (Some(a), Some(b)) => format!("{a:.1}-{b:.1} V"), _ => "--".into() }),
            ("SAMPLES", trip.samples.to_string()),
            ("EVENTS", d["eventCount"].as_i64().unwrap_or(0).to_string()),
        ];
        for (i, (k, v)) in rows.iter().enumerate() {
            let y = 164.0 + dy + i as f32 * 34.0;
            self.label(x, y, 2.0, k, self.dim());
            self.label_right(xr, y, 2.4, &v.to_uppercase(), pal.lit);
        }
        if let Some(ev) = d["events"].as_array().and_then(|e| e.first()) {
            let text = format!("{} {}", ev["kind"].as_str().unwrap_or(""), ev["detail"].as_str().unwrap_or(""));
            let text: String = text.to_uppercase().chars().take(40).collect();
            self.label(x, 444.0 + dy, 1.8, &text, pal.warn);
        }
        if self.key("export", x, 470.0 + dy, 240.0, 60.0, "EXPORT", false) {
            actions.push(Action::Call("tripRecorder", "exportTrip", vec![json!(trip.id)]));
        }
        if trip.open && self.key("endtrip", x + 256.0, 470.0 + dy, 250.0, 60.0, "END TRIP", true) {
            actions.push(Action::Call("tripRecorder", "endTripNow", vec![]));
        }
        if let Some(path) = &s.export_path {
            let name = path.rsplit('/').next().unwrap_or(path);
            let text: String = format!("SAVED {name}").chars().take(60).collect();
            self.label(x, 540.0 + dy, 1.4, &text, self.dim());
        }
    }

    /// The car-stereo display: the cover large, and a spectrum analyzer
    /// with falling peak dots, from audioAnalyzer.levels.
    fn audio(&mut self, peaks: &mut Vec<f32>, dt: f32) {
        let pal = self.pal;
        let s = self.s;
        // Upright: the cover over the analyzer
        self.body(1.0);
        let tall = self.tall;

        let art = s.art.as_ref().filter(|(rev, _)| *rev == s.art_revision).map(|(_, d)| d.as_slice());
        vfd::dither_art(self.painter, self.cv.r(if tall { 200.0 } else { 56.0 }, 104.0, 440.0, 440.0), link::ART_DOTS as usize, art, &pal);

        // 48 columns from the 96 bands (the louder of each pair), 8 segments each
        let cols = 48usize;
        let levels: Vec<f32> = if let Some(f) = self.self_test {
            (0..cols).map(|_| (f * 8.0) as f32).collect()
        } else if s.levels.is_empty() {
            vec![0.0; cols]
        } else {
            let per = (s.levels.len() / cols).max(1);
            (0..cols).map(|c| s.levels.iter().skip(c * per).take(per).copied().max().unwrap_or(0) as f32).collect()
        };
        peaks.resize(cols, 0.0);
        let (x0, x1, top, bottom) = if tall { (56.0f32, 784.0f32, 580.0f32, 900.0f32) } else { (530.0, 1226.0, 120.0, 440.0) };
        let pitch = (x1 - x0) / cols as f32;
        let seg_h = (bottom - top) / 8.0;
        for (c, &level) in levels.iter().enumerate() {
            // Peaks fall at two segments a second after holding
            peaks[c] = if level >= peaks[c] { level } else { (peaks[c] - dt * 2.0).max(level) };
            for seg in 0..8 {
                let y = bottom - (seg as f32 + 1.0) * seg_h;
                let r = self.cv.r(x0 + c as f32 * pitch, y + seg_h * 0.18, pitch * 0.72, seg_h * 0.72);
                let color = if seg >= 7 { pal.hot } else if seg >= 5 { pal.warn } else { pal.lit };
                let lit = (seg as f32) < level;
                let peak = peaks[c] >= 1.0 && seg == (peaks[c].ceil() as usize).saturating_sub(1).min(7) && !lit;
                if lit || peak {
                    self.painter.rect_filled(r.expand(self.cv.s(1.5)), 2.0, color.gamma_multiply(0.14));
                    self.painter.rect_filled(r, 1.0, color);
                } else {
                    self.painter.rect_filled(r, 1.0, pal.ghost);
                }
            }
        }
        let freqs = ["60", "250", "1K", "4K", "16K"];
        for (i, f) in freqs.iter().enumerate() {
            let x = x0 + (x1 - x0) * (i as f32 + 0.5) / freqs.len() as f32;
            vfd::text_center(self.painter, self.cv.p(x, bottom + 18.0), self.cv.s(1.8), f, self.dim());
        }
        let np = &s.now;
        let title: String = if np.has_track { np.title.chars().take(26).collect() } else { "NO TRACK".into() };
        self.label(x0, bottom + 44.0, 3.2, &title, pal.lit);
        let sub: String = [np.artist.as_str(), np.album.as_str()]
            .iter()
            .filter(|t| !t.is_empty())
            .copied()
            .collect::<Vec<_>>()
            .join("  -  ")
            .chars()
            .take(52)
            .collect();
        self.label(x0, bottom + 80.0, 2.0, &sub, self.dim());
        if s.levels.is_empty() && self.online && self.self_test.is_none() {
            self.label(x0, top - 24.0, 1.6, "NO LEVELS FROM THE ANALYZER (NOTHING PLAYING?)", pal.ghost);
        }
    }

    /// The tape deck: what's playing, the transport keys and the volume.
    fn deck(&mut self, actions: &mut Vec<Action>) {
        let pal = self.pal;
        let s = self.s;
        let np = &s.now;
        // Upright: the display in one window, the keys larger in another under it
        let tall = self.tall;
        self.place((30.0, 594.0), (15.0, 1140.0), 1.0);
        let frame = self.cv.r(30.0, 594.0, if tall { 770.0 } else { 1220.0 }, 186.0);
        vfd::window(self.painter, frame, &pal);

        let line = if self.online && !s.trusted {
            // Over the network, protocol 2: nothing is answered until paired
            "NOT PAIRED  -  RUN RETRO-CLUSTER --PAIR CODE  (OCTAVE: SETTINGS > DEVICE > SERVICE)".into()
        } else if np.has_track {
            let mut parts = vec![np.title.clone()];
            if !np.artist.is_empty() {
                parts.push(np.artist.clone());
            }
            if !np.album.is_empty() {
                parts.push(np.album.clone());
            }
            format!("♪ {}", parts.join("  -  "))
        } else if self.online {
            "NO TRACK  -  PICK ONE IN OCTAVE".into()
        } else {
            String::new()
        };
        // The cover, as the display would show it: a dithered dot matrix
        let art = s.art.as_ref().filter(|(rev, _)| *rev == s.art_revision).map(|(_, dots)| dots.as_slice());
        vfd::dither_art(self.painter, self.cv.r(48.0, 606.0, 160.0, 160.0), link::ART_DOTS as usize, art, &pal);

        let x0 = 226.0;
        let p = self.cv.s(3.6);
        let cells = 26;
        vfd::marquee(self.painter, self.cv.p(x0, 614.0), p, cells, &line, (self.t * 2.4) as f32, pal.lit, pal.ghost);

        let source = match np.source.as_str() {
            "phone" => "PHONE",
            "local" => "LOCAL",
            "" => "----",
            other => other,
        };
        let status = format!("SRC {}   {}   {}", source.to_uppercase(), if np.playing { "▶ PLAY" } else { "‖ PAUSE" }, if np.shuffled { "SHUFFLE" } else { "" });
        self.label(x0, 652.0, 1.8, &status, self.dim());

        // The tape: tap to seek
        let tape = self.cv.r(x0, 680.0, 786.0 - x0, 22.0);
        let pos = np.position_now();
        let frac = if np.duration_ms > 0 { pos as f64 / np.duration_ms as f64 } else { 0.0 };
        vfd::strip(self.painter, tape, 48, frac, pal.lit, &pal);
        let resp = self.ui.interact(tape.expand(self.cv.s(8.0)), egui::Id::unique("tape"), Sense::click());
        if resp.clicked()
            && np.can_seek
            && np.duration_ms > 0
            && let Some(at) = resp.interact_pointer_pos()
        {
            let f = ((at.x - tape.min.x) / tape.width()).clamp(0.0, 1.0) as f64;
            actions.push(Action::Call("nowPlaying", "seek", vec![json!((f * np.duration_ms as f64) as i64)]));
        }
        let mmss = |ms: i64| format!("{}:{:02}", ms / 60000, (ms / 1000) % 60);
        self.label(x0, 716.0, 3.0, &mmss(pos), pal.lit);
        let remain = if np.duration_ms > 0 { format!("-{}", mmss(np.duration_ms - pos)) } else { "-:--".into() };
        self.label_right(786.0, 716.0, 3.0, &remain, self.dim());

        // Transport keys
        self.place((808.0, 598.0), (65.0, 1330.0), 1.55);
        if tall {
            vfd::window(self.painter, self.cv.r(808.0, 598.0, 432.0, 174.0), &pal);
        }
        let (kx, ky, kw, kh) = (820.0, 610.0, 96.0, 62.0);
        if self.key("prev", kx, ky, kw, kh, "◀◀", false) {
            actions.push(Action::Call("nowPlaying", "previous", vec![]));
        }
        if self.key("play", kx + 104.0, ky, kw, kh, if np.playing { "‖" } else { "▶" }, np.playing) {
            actions.push(Action::Call("nowPlaying", "togglePlay", vec![]));
        }
        if self.key("next", kx + 208.0, ky, kw, kh, "▶▶", false) {
            actions.push(Action::Call("nowPlaying", "next", vec![]));
        }
        if self.key("shuf", kx + 312.0, ky, kw, kh, "SHUF", np.shuffled) && np.can_shuffle {
            actions.push(Action::Call("nowPlaying", "toggleShuffle", vec![]));
        }

        // Volume
        let vy = 690.0;
        if self.key("vol-", kx, vy, 70.0, 70.0, "-", false) {
            actions.push(Action::Volume(-5));
        }
        let muted = s.effectively_muted;
        let vol_color = if muted { pal.ghost } else { pal.lit };
        vfd::strip(self.painter, self.cv.r(kx + 82.0, vy + 8.0, 150.0, 22.0), 16, s.volume as f64 / 100.0, vol_color, &pal);
        let vol_text = if muted { "MUTE".to_string() } else { format!("VOL {:>3}", s.volume) };
        self.label(kx + 84.0, vy + 44.0, 2.2, &vol_text, if muted { pal.hot } else { self.dim() });
        if self.key("vol+", kx + 244.0, vy, 70.0, 70.0, "+", false) {
            actions.push(Action::Volume(5));
        }
        if self.key("mute", kx + 326.0, vy, 82.0, 70.0, "MUTE", s.muted) {
            actions.push(Action::Call("volumeController", "toggleMute", vec![]));
        }
    }

    fn no_signal(&mut self) {
        let pal = self.pal;
        self.place((290.0, 300.0), (50.0, 600.0), 1.0);
        let r = self.cv.r(290.0, 300.0, 700.0, 190.0);
        self.painter.rect_filled(r.expand(self.cv.s(14.0)), 12.0, pal.bg.gamma_multiply(0.92));
        vfd::window(self.painter, r, &pal);
        let blink = (self.t * 1.6) as i64 % 2 == 0;
        vfd::text_center(self.painter, self.cv.p(640.0, 350.0), self.cv.s(6.0), "NO SIGNAL", if blink { pal.hot } else { pal.ghost });
        let wait = format!("WAITING FOR OCTAVE AT {}", self.s.address);
        vfd::text_center(self.painter, self.cv.p(640.0, 412.0), self.cv.s(2.2), &wait, pal.lit);
        vfd::text_center(self.painter, self.cv.p(640.0, 446.0), self.cv.s(1.8), "RUN  OCTAVE --SERVICE  OR  OCTAVE --HEADLESS", self.dim());
    }
}
