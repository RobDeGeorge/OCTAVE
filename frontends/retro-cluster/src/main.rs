//! OCTAVE Retro Cluster: an experimental second frontend for OCTAVE, in
//! Rust + egui, styled as a 1980s vacuum-fluorescent instrument cluster.
//! It is a separate program that talks to the C++ backend over the service
//! API (docs/api/), the same objects the QML UI binds to.
//!
//!     ./build/octave --service          (or --headless, or --dev --service)
//!     cargo run --release -- [--host H] [--port P] [--fullscreen] [--theme N]

#![allow(clippy::too_many_arguments)]

mod link;
mod vfd;

use std::time::{Duration, Instant};

use eframe::egui::{self, Color32, Key, Painter, Pos2, Rect, Sense, pos2, vec2};
use serde_json::json;

use link::{Link, LinkState, State};
use vfd::{PALETTES, Palette};

/// The layout is drawn on a 1280x800 design canvas (the head unit's
/// screen) and scaled to fit the window.
const DESIGN: egui::Vec2 = egui::Vec2::new(1280.0, 800.0);

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

const DIAG_COLS: usize = 3;
const DIAG_ROWS: usize = 15;

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
    fullscreen: bool,
    theme: usize,
    page: Page,
}

fn parse_args() -> Args {
    let mut a = Args { address: String::new(), token: std::env::var("OCTAVE_TOKEN").ok().filter(|t| !t.is_empty()),
                       fullscreen: false, theme: 0, page: Page::Drive };
    let mut pair_code: Option<String> = None;
    let (mut host, mut port) = ("127.0.0.1".to_string(), 9876u16);
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "--host" => host = it.next().unwrap_or(host),
            "--port" => port = it.next().and_then(|p| p.parse().ok()).unwrap_or(port),
            "--token" => a.token = it.next(),
            "--pair" => pair_code = it.next(),
            "--fullscreen" => a.fullscreen = true,
            "--page" => {
                a.page = match it.next().as_deref() {
                    Some("diag") => Page::Diag,
                    Some("dtc") => Page::Codes,
                    Some("trips") => Page::Trips,
                    Some("audio") => Page::Audio,
                    _ => Page::Drive,
                }
            }
            "--theme" => a.theme = it.next().and_then(|t| t.parse().ok()).unwrap_or(0) % PALETTES.len(),
            "-h" | "--help" => {
                println!("retro-cluster [--host 127.0.0.1] [--port 9876] [--token T] [--fullscreen] [--theme 0|1|2] [--page drive|diag|dtc|trips|audio]");
                println!("              [--pair CODE]   pair with the code in OCTAVE's Settings > Device > Service, print the token");
                println!("Over the network the cluster needs a token (--token, or OCTAVE_TOKEN); on the head unit itself it doesn't.");
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
                let per = DIAG_COLS * DIAG_ROWS;
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
            if i.key_pressed(Key::F11) {
                self.fullscreen = !self.fullscreen;
                ui.ctx().send_viewport_cmd(egui::ViewportCommand::Fullscreen(self.fullscreen));
            }
            if i.modifiers.ctrl && i.key_pressed(Key::Q) {
                quit = true;
            }
        });
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
    fn fit(screen: Rect) -> Canvas {
        let scale = (screen.width() / DESIGN.x).min(screen.height() / DESIGN.y);
        let size = DESIGN * scale;
        Canvas { origin: screen.center() - size / 2.0, scale }
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

        let state_handle = self.link.state.clone();
        let s = state_handle.lock().unwrap();
        self.update_demand(&s);

        let pal = PALETTES[self.theme];
        let screen = ui.max_rect();
        let painter = ui.painter().clone();
        painter.rect_filled(screen, 0.0, Color32::BLACK);
        let cv = Canvas::fit(screen);
        painter.rect_filled(cv.r(0.0, 0.0, DESIGN.x, DESIGN.y), 0.0, pal.bg);

        // The bulb check: every segment sweeps up and back for 1.6 s at start
        let self_test = (t < 1.6).then(|| (std::f64::consts::PI * t / 1.6).sin());
        let online = s.link == LinkState::Online;

        let k = 1.0 - (-dt * 10.0).exp();
        let rpm_target = self_test.map(|f| f * 8000.0).or(s.display("RPM")).unwrap_or(0.0);
        self.rpm_smooth += (rpm_target - self.rpm_smooth) * k;
        let speed_target = s.display("SPEED").unwrap_or(0.0);
        self.speed_smooth += (speed_target - self.speed_smooth) * k;

        let mut ctx = Draw { ui, painter: &painter, cv, pal, s: &s, t, self_test, online };
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
        vfd::scanlines(&painter, cv.r(0.0, 0.0, DESIGN.x, DESIGN.y), cv.s(3.0).max(2.0));
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

enum Action {
    Page(Page),
    Theme,
    Call(&'static str, &'static str, Vec<serde_json::Value>),
    Volume(i64),
}

struct Draw<'a> {
    ui: &'a mut egui::Ui,
    painter: &'a Painter,
    cv: Canvas,
    pal: Palette,
    s: &'a State,
    t: f64,
    self_test: Option<f64>,
    online: bool,
}

impl Draw<'_> {
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
        self.label(36.0, 16.0, 4.0, "OCTAVE", pal.lit);
        self.label(38.0, 50.0, 1.7, "RETRO CLUSTER", self.dim());

        let tabs = [
            ("DRIVE", Page::Drive, 84.0),
            ("DIAG", Page::Diag, 72.0),
            ("DTC", Page::Codes, 64.0),
            ("TRIPS", Page::Trips, 80.0),
            ("AUDIO", Page::Audio, 80.0),
        ];
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
        let mut x = 760.0;
        for (name, on, color) in lamps {
            let w = 14.0 + name.len() as f32 * 11.0;
            vfd::lamp(self.painter, self.cv.r(x, 20.0, w, 30.0), name, on, color, &pal);
            x += w + 6.0;
        }

        // The clock: the app's own string (clock.time, in the user's 12/24 h
        // format); local time only against a backend too old to send it
        let text = self.s.clock.clone().filter(|c| !c.is_empty()).unwrap_or_else(|| chrono::Local::now().format("%H:%M").to_string());
        let digits: String = text.chars().filter(|c| c.is_ascii_digit()).take(4).collect();
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
        let frame = self.cv.r(30.0, 78.0, 1220.0, 150.0);
        vfd::window(self.painter, frame, &pal);
        let n = 72usize;
        let (x0, x1, base) = (56.0f32, 1224.0f32, 186.0f32);
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
        self.label_right(1224.0, 92.0, 1.8, &rpm_text, pal.lit);
    }

    fn gauges(&mut self, speed: f64) {
        let pal = self.pal;
        // Left: four ladders
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
        let frame = self.cv.r(30.0, 78.0, 1220.0, 492.0);
        vfd::window(self.painter, frame, &pal);
        let per = DIAG_COLS * DIAG_ROWS;
        let pages = ids.len().div_ceil(per).max(1);
        *page = (*page).min(pages - 1);
        self.label(52.0, 94.0, 2.2, "LIVE DATA", pal.lit);
        let summary = format!("{} PIDS  PAGE {}/{}", ids.len(), *page + 1, pages);
        self.label(240.0, 96.0, 1.8, &summary, self.dim());

        if self.key("pgup", 1040.0, 86.0, 90.0, 36.0, "▲", *page > 0) && *page > 0 {
            *page -= 1;
        }
        if self.key("pgdn", 1142.0, 86.0, 90.0, 36.0, "▼", *page + 1 < pages) && *page + 1 < pages {
            *page += 1;
        }

        let col_w = 1180.0 / DIAG_COLS as f32;
        for (n, id) in ids.iter().skip(*page * per).take(per).enumerate() {
            let (col, row) = (n / DIAG_ROWS, n % DIAG_ROWS);
            let x = 52.0 + col as f32 * col_w;
            let y = 140.0 + row as f32 * 27.0;
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
        let footer: String = footer.chars().take(118).collect();
        self.label(52.0, 548.0, 1.4, &footer, self.dim());
        if let Some(p) = s.protocol.filter(|p| !link::PROTOCOL_VERSIONS.contains(p)) {
            self.label(52.0, 530.0, 1.4, &format!("WARNING: UNKNOWN PROTOCOL {p}; THIS CLUSTER KNOWS 1 AND 2"), pal.hot);
        } else if !s.trusted {
            self.label(52.0, 530.0, 1.4, "NOT PAIRED: RUN RETRO-CLUSTER --PAIR CODE (SETTINGS > DEVICE > SERVICE)", pal.hot);
        }
        let _ = actions;
    }

    /// Trouble codes: the check-engine lamp, the stored codes, the freeze
    /// frame, and a two-press clear.
    fn codes(&mut self, ps: &mut PageState, actions: &mut Vec<Action>) {
        let pal = self.pal;
        let s = self.s;
        let frame = self.cv.r(30.0, 78.0, 1220.0, 492.0);
        vfd::window(self.painter, frame, &pal);

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
            let desc: String = desc.chars().take(52).collect();
            self.label(240.0, y + 10.0, 2.0, &desc, self.dim());
        }
        if s.dtcs.len() > 6 {
            self.label(56.0, 516.0, 2.0, &format!("+{} MORE", s.dtcs.len() - 6), self.dim());
        }

        // Keys on the right
        let kx = 900.0;
        if self.key("read", kx, 100.0, 330.0, 64.0, "READ CODES", false) {
            actions.push(Action::Call("obdManager", "read_dtc", vec![]));
        }
        if self.key("freeze", kx, 180.0, 330.0, 64.0, "FREEZE FRAME", !s.freeze.is_empty()) {
            actions.push(Action::Call("obdManager", "read_freeze_frame", vec![]));
        }
        let armed = ps.clear_armed.is_some_and(|at| at.elapsed() < Duration::from_secs(4));
        if !armed {
            ps.clear_armed = None;
        }
        let clear_label = if armed { "PRESS AGAIN TO CLEAR" } else { "CLEAR CODES" };
        if self.key("clear", kx, 260.0, 330.0, 64.0, clear_label, armed) {
            if armed {
                actions.push(Action::Call("obdManager", "clear_dtc", vec![]));
                ps.clear_armed = None;
            } else {
                ps.clear_armed = Some(Instant::now());
            }
        }
        if armed {
            self.label(kx, 334.0, 1.8, "ERASES CODES + FREEZE FRAME", pal.hot);
        }
        if let Some((ok, msg, at)) = &s.dtc_result
            && at.elapsed() < Duration::from_secs(10)
        {
            let text: String = format!("{} {}", if *ok { "CLEARED." } else { "CLEAR FAILED:" }, msg).chars().take(30).collect();
            self.label(kx, 360.0, 1.8, &text, if *ok { pal.lit } else { pal.hot });
        }
        self.label(kx, 404.0, 2.0, "FREEZE FRAME", self.dim());
        if s.freeze.is_empty() {
            self.label(kx, 430.0, 2.6, "NONE", pal.ghost);
        }
        // The first frame's code, then its readings in the user's units
        if let Some(d) = s.freeze.first() {
            self.label(kx, 430.0, 2.6, &d.code, pal.lit);
            let preferred = ["RPM", "SPEED", "COOLANT_TEMP", "ENGINE_LOAD", "MAF", "INTAKE_PRESSURE"];
            let mut shown: Vec<&(String, f64)> = preferred.iter().filter_map(|id| d.values.iter().find(|(k, _)| k == id)).collect();
            shown.extend(d.values.iter().filter(|(k, _)| !preferred.contains(&k.as_str())));
            for (i, (id, v)) in shown.iter().take(4).enumerate() {
                let info = s.catalog.get(id);
                let name = info.map(|i| if i.short_title.is_empty() { i.title.clone() } else { i.short_title.clone() }).unwrap_or_else(|| id.clone());
                let (val, unit) = info.map(|i| (v * i.scale + i.offset, i.unit.clone())).unwrap_or((*v, String::new()));
                let y = 462.0 + i as f32 * 22.0;
                self.label(kx, y, 1.8, &name.to_uppercase(), self.dim());
                self.label_right(1226.0, y, 1.8, &format!("{:.*} {}", info.map(|i| i.decimals.min(1)).unwrap_or(0), val, unit), pal.lit);
            }
        }
    }

    /// The black box: recorded drives, and one drive's summary.
    fn trips(&mut self, ps: &mut PageState, actions: &mut Vec<Action>) {
        let pal = self.pal;
        let s = self.s;
        let frame = self.cv.r(30.0, 78.0, 1220.0, 492.0);
        vfd::window(self.painter, frame, &pal);
        self.label(52.0, 94.0, 2.2, "BLACK BOX", pal.lit);
        self.label(240.0, 96.0, 1.8, &format!("{} DRIVES", s.trips.len()), self.dim());

        // Newest first
        let trips: Vec<&link::Trip> = s.trips.iter().rev().collect();
        if trips.is_empty() {
            self.label(56.0, 150.0, 3.0, if self.online { "NO DRIVES RECORDED" } else { "--" }, pal.ghost);
            return;
        }
        ps.trip_index = ps.trip_index.min(trips.len() - 1);
        let rows = 11usize;
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
        if self.key("tup", 620.0, 132.0, 64.0, 60.0, "▲", ps.trip_index > 0) && ps.trip_index > 0 {
            ps.trip_index -= 1;
        }
        if self.key("tdn", 620.0, 470.0, 64.0, 60.0, "▼", ps.trip_index + 1 < trips.len()) && ps.trip_index + 1 < trips.len() {
            ps.trip_index += 1;
        }

        let trip = trips[ps.trip_index];
        if ps.detail_asked != Some(trip.id) {
            ps.detail_asked = Some(trip.id);
            actions.push(Action::Call("tripRecorder", "requestDetail", vec![json!(trip.id)]));
        }
        let x = 720.0;
        self.label(x, 132.0, 2.0, "SUMMARY", self.dim());
        let Some(d) = s.trip_details.get(&trip.id) else {
            self.label(x, 170.0, 2.4, "READING...", pal.ghost);
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
            let y = 164.0 + i as f32 * 34.0;
            self.label(x, y, 2.0, k, self.dim());
            self.label_right(1226.0, y, 2.4, &v.to_uppercase(), pal.lit);
        }
        if let Some(ev) = d["events"].as_array().and_then(|e| e.first()) {
            let text = format!("{} {}", ev["kind"].as_str().unwrap_or(""), ev["detail"].as_str().unwrap_or(""));
            let text: String = text.to_uppercase().chars().take(40).collect();
            self.label(x, 444.0, 1.8, &text, pal.warn);
        }
        if self.key("export", x, 470.0, 240.0, 60.0, "EXPORT", false) {
            actions.push(Action::Call("tripRecorder", "exportTrip", vec![json!(trip.id)]));
        }
        if trip.open && self.key("endtrip", x + 256.0, 470.0, 250.0, 60.0, "END TRIP", true) {
            actions.push(Action::Call("tripRecorder", "endTripNow", vec![]));
        }
        if let Some(path) = &s.export_path {
            let name = path.rsplit('/').next().unwrap_or(path);
            let text: String = format!("SAVED {name}").chars().take(60).collect();
            self.label(x, 540.0, 1.4, &text, self.dim());
        }
    }

    /// The car-stereo display: the cover large, and a spectrum analyzer
    /// with falling peak dots, from audioAnalyzer.levels.
    fn audio(&mut self, peaks: &mut Vec<f32>, dt: f32) {
        let pal = self.pal;
        let s = self.s;
        let frame = self.cv.r(30.0, 78.0, 1220.0, 492.0);
        vfd::window(self.painter, frame, &pal);

        let art = s.art.as_ref().filter(|(rev, _)| *rev == s.art_revision).map(|(_, d)| d.as_slice());
        vfd::dither_art(self.painter, self.cv.r(56.0, 104.0, 440.0, 440.0), link::ART_DOTS as usize, art, &pal);

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
        let (x0, x1, top, bottom) = (530.0f32, 1226.0f32, 120.0f32, 440.0f32);
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
            vfd::text_center(self.painter, self.cv.p(x, 458.0), self.cv.s(1.8), f, self.dim());
        }
        let np = &s.now;
        let title: String = if np.has_track { np.title.chars().take(26).collect() } else { "NO TRACK".into() };
        self.label(530.0, 484.0, 3.2, &title, pal.lit);
        let sub: String = [np.artist.as_str(), np.album.as_str()]
            .iter()
            .filter(|t| !t.is_empty())
            .copied()
            .collect::<Vec<_>>()
            .join("  -  ")
            .chars()
            .take(52)
            .collect();
        self.label(530.0, 520.0, 2.0, &sub, self.dim());
        if s.levels.is_empty() && self.online && self.self_test.is_none() {
            self.label(530.0, 96.0, 1.6, "NO LEVELS FROM THE ANALYZER (NOTHING PLAYING?)", pal.ghost);
        }
    }

    /// The tape deck: what's playing, the transport keys and the volume.
    fn deck(&mut self, actions: &mut Vec<Action>) {
        let pal = self.pal;
        let s = self.s;
        let np = &s.now;
        let frame = self.cv.r(30.0, 594.0, 1220.0, 186.0);
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
