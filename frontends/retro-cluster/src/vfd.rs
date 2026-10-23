//! Drawing primitives for a vacuum-fluorescent dash: a 5x7 dot-matrix font,
//! seven-segment digits, segmented bar graphs, lamps and hardware keys.
//! Everything is painted from shapes; there is no font file. Unlit dots and
//! segments are drawn faintly ("ghosts"), as a real VFD shows them.

#![allow(clippy::too_many_arguments)]

use eframe::egui::{Color32, CornerRadius, Painter, Pos2, Rect, Shape, Stroke, StrokeKind, pos2, vec2};

#[derive(Clone, Copy)]
pub struct Palette {
    pub name: &'static str,
    pub bg: Color32,
    pub glass: Color32,
    pub bezel: Color32,
    pub lit: Color32,
    pub ghost: Color32,
    pub warn: Color32,
    pub hot: Color32,
}

pub const PALETTES: [Palette; 3] = [
    Palette {
        name: "AMBER",
        bg: Color32::from_rgb(8, 6, 4),
        glass: Color32::from_rgb(16, 11, 5),
        bezel: Color32::from_rgb(44, 34, 22),
        lit: Color32::from_rgb(255, 176, 0),
        ghost: Color32::from_rgb(40, 27, 4),
        warn: Color32::from_rgb(255, 120, 20),
        hot: Color32::from_rgb(255, 52, 36),
    },
    Palette {
        name: "GREEN",
        bg: Color32::from_rgb(3, 8, 5),
        glass: Color32::from_rgb(6, 16, 9),
        bezel: Color32::from_rgb(24, 44, 30),
        lit: Color32::from_rgb(60, 255, 110),
        ghost: Color32::from_rgb(10, 40, 18),
        warn: Color32::from_rgb(220, 255, 60),
        hot: Color32::from_rgb(255, 60, 40),
    },
    Palette {
        name: "CYAN",
        bg: Color32::from_rgb(3, 7, 9),
        glass: Color32::from_rgb(5, 14, 17),
        bezel: Color32::from_rgb(22, 40, 46),
        lit: Color32::from_rgb(100, 245, 230),
        ghost: Color32::from_rgb(10, 38, 38),
        warn: Color32::from_rgb(255, 190, 60),
        hot: Color32::from_rgb(255, 64, 96),
    },
];

/// The glow halo around a lit element.
fn halo(c: Color32, alpha: u8) -> Color32 {
    Color32::from_rgba_unmultiplied(c.r(), c.g(), c.b(), alpha)
}

// ── 5x7 dot matrix ──────────────────────────────────────────────────────

/// Rows top to bottom, bit 4 = leftmost column.
fn glyph(c: char) -> [u8; 7] {
    match c {
        'A' => [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
        'B' => [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E],
        'C' => [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E],
        'D' => [0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C],
        'E' => [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F],
        'F' => [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10],
        'G' => [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F],
        'H' => [0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
        'I' => [0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E],
        'J' => [0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C],
        'K' => [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
        'L' => [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F],
        'M' => [0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11],
        'N' => [0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11],
        'O' => [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
        'P' => [0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10],
        'Q' => [0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D],
        'R' => [0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11],
        'S' => [0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E],
        'T' => [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
        'U' => [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
        'V' => [0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04],
        'W' => [0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A],
        'X' => [0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11],
        'Y' => [0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04],
        'Z' => [0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F],
        '0' => [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E],
        '1' => [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
        '2' => [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F],
        '3' => [0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E],
        '4' => [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02],
        '5' => [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
        '6' => [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E],
        '7' => [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
        '8' => [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E],
        '9' => [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C],
        ' ' => [0; 7],
        '.' => [0, 0, 0, 0, 0, 0x0C, 0x0C],
        ',' => [0, 0, 0, 0, 0x0C, 0x04, 0x08],
        ':' => [0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0],
        ';' => [0, 0x0C, 0x0C, 0, 0x0C, 0x04, 0x08],
        '-' => [0, 0, 0, 0x1F, 0, 0, 0],
        '/' => [0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10],
        '%' => [0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03],
        '°' => [0x0C, 0x12, 0x12, 0x0C, 0, 0, 0],
        '\'' => [0x0C, 0x04, 0x08, 0, 0, 0, 0],
        '"' => [0x0A, 0x0A, 0x0A, 0, 0, 0, 0],
        '!' => [0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04],
        '?' => [0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04],
        '(' => [0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02],
        ')' => [0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08],
        '[' => [0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E],
        ']' => [0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E],
        '+' => [0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0],
        '<' => [0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02],
        '>' => [0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08],
        '=' => [0, 0, 0x1F, 0, 0x1F, 0, 0],
        '_' => [0, 0, 0, 0, 0, 0, 0x1F],
        '#' => [0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A],
        '&' => [0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D],
        '*' => [0, 0x04, 0x15, 0x0E, 0x15, 0x04, 0],
        '|' => [0x04; 7],
        '~' => [0, 0, 0x08, 0x15, 0x02, 0, 0],
        '@' => [0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E],
        '$' => [0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04],
        // Transport icons, drawn in the same matrix
        '▶' => [0x10, 0x18, 0x1C, 0x1E, 0x1C, 0x18, 0x10],
        '◀' => [0x01, 0x03, 0x07, 0x0F, 0x07, 0x03, 0x01],
        '‖' => [0x1B; 7],
        '■' => [0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0],
        '♪' => [0x03, 0x05, 0x05, 0x04, 0x0C, 0x1C, 0x18],
        '▲' => [0x04, 0x04, 0x0E, 0x0E, 0x1F, 0x1F, 0],
        '▼' => [0, 0x1F, 0x1F, 0x0E, 0x0E, 0x04, 0x04],
        _ => glyph('?'),
    }
}

/// Uppercase, with the accented Latin letters folded to their base, so a
/// track title shows as close to itself as a 5x7 matrix allows.
pub fn fold(c: char) -> char {
    let c = c.to_uppercase().next().unwrap_or(c);
    match c {
        'À'..='Å' => 'A',
        'Ç' => 'C',
        'È'..='Ë' => 'E',
        'Ì'..='Ï' => 'I',
        'Ñ' => 'N',
        'Ò'..='Ö' | 'Ø' => 'O',
        'Ù'..='Ü' => 'U',
        'Ý' | 'Ÿ' => 'Y',
        '–' | '—' => '-',
        '‘' | '’' => '\'',
        '“' | '”' => '"',
        _ => c,
    }
}

/// Width of `n` characters at dot pitch `p` (5 dots + 1 gap each).
pub fn text_width(n: usize, p: f32) -> f32 {
    if n == 0 { 0.0 } else { n as f32 * 6.0 * p - p }
}

/// One line of dot-matrix text with its top-left at `at`, dot pitch `p`.
/// `ghost` draws the unlit dots of each cell as well.
pub fn text(painter: &Painter, at: Pos2, p: f32, s: &str, color: Color32, ghost: Option<Color32>) {
    let dot = p * 0.78;
    let mut x = at.x;
    for c in s.chars() {
        let rows = glyph(fold(c));
        for (r, bits) in rows.iter().enumerate() {
            for col in 0..5 {
                let lit = bits & (0x10 >> col) != 0;
                let min = pos2(x + col as f32 * p, at.y + r as f32 * p);
                let cell = Rect::from_min_size(min, vec2(dot, dot));
                if lit {
                    painter.rect_filled(cell.expand(p * 0.35), CornerRadius::same((p * 0.4) as u8), halo(color, 26));
                    painter.rect_filled(cell, CornerRadius::ZERO, color);
                } else if let Some(g) = ghost {
                    painter.rect_filled(cell, CornerRadius::ZERO, g);
                }
            }
        }
        x += 6.0 * p;
    }
}

/// Right-aligned dot-matrix text ending at `right_top`.
pub fn text_right(painter: &Painter, right_top: Pos2, p: f32, s: &str, color: Color32) {
    let w = text_width(s.chars().count(), p);
    text(painter, pos2(right_top.x - w, right_top.y), p, s, color, None);
}

/// Centred dot-matrix text around `center`.
pub fn text_center(painter: &Painter, center: Pos2, p: f32, s: &str, color: Color32) {
    let w = text_width(s.chars().count(), p);
    text(painter, pos2(center.x - w / 2.0, center.y - 3.5 * p), p, s, color, None);
}

/// A fixed window of `cells` characters scrolling `s` through it (a
/// marquee), with every unlit dot of the window ghosted.
pub fn marquee(painter: &Painter, at: Pos2, p: f32, cells: usize, s: &str, offset_cells: f32, color: Color32, ghost: Color32) {
    let chars: Vec<char> = s.chars().collect();
    let fits = chars.len() <= cells;
    let looped: Vec<char> = if fits {
        chars.clone()
    } else {
        chars.iter().copied().chain("     ".chars()).collect()
    };
    let start = if fits { 0.0 } else { offset_cells.rem_euclid(looped.len() as f32) };
    let whole = start.floor() as usize;
    // Scroll by whole dot columns, like a real matrix driver
    let shift_cols = ((start - start.floor()) * 6.0).floor() as usize;
    let window_cols = cells * 6 - 1;
    for col in 0..window_cols {
        let src_col = col + shift_cols;
        let (ci, cc) = (src_col / 6, src_col % 6);
        let ch = if fits {
            looped.get(ci).copied().unwrap_or(' ')
        } else {
            looped[(whole + ci) % looped.len()]
        };
        let rows = glyph(fold(ch));
        for (r, bits) in rows.iter().enumerate() {
            let lit = cc < 5 && bits & (0x10 >> cc) != 0;
            let min = pos2(at.x + col as f32 * p, at.y + r as f32 * p);
            let cell = Rect::from_min_size(min, vec2(p * 0.78, p * 0.78));
            if lit {
                painter.rect_filled(cell.expand(p * 0.35), CornerRadius::same((p * 0.4) as u8), halo(color, 26));
                painter.rect_filled(cell, CornerRadius::ZERO, color);
            } else {
                painter.rect_filled(cell, CornerRadius::ZERO, ghost);
            }
        }
    }
}

// ── seven segments ──────────────────────────────────────────────────────

/// Segment bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64.
fn segments(c: char) -> u8 {
    match c.to_ascii_uppercase() {
        '0' | 'O' => 0x3F,
        '1' | 'I' => 0x06,
        '2' => 0x5B,
        '3' => 0x4F,
        '4' => 0x66,
        '5' | 'S' => 0x6D,
        '6' => 0x7D,
        '7' => 0x07,
        '8' | 'B' => 0x7F,
        '9' => 0x6F,
        '-' => 0x40,
        'E' => 0x79,
        'R' => 0x50,
        'L' => 0x38,
        'P' => 0x73,
        'H' => 0x76,
        'C' => 0x39,
        'F' => 0x71,
        'N' => 0x54,
        'U' => 0x3E,
        'A' => 0x77,
        'D' => 0x5E,
        'T' => 0x78,
        '_' => 0x08,
        _ => 0,
    }
}

/// One seven-segment digit in `r`, slanted like a dash display. `dp` lights
/// the decimal point.
pub fn seg_digit(painter: &Painter, r: Rect, c: char, dp: bool, pal: &Palette, color: Color32) {
    let bits = segments(c);
    let (w, h) = (r.width(), r.height());
    let t = (w * 0.17).min(h * 0.1);
    let gap = t * 0.14;
    let skew = 0.1;
    let to = |x: f32, y: f32| pos2(r.min.x + x + (h / 2.0 - y) * skew, r.min.y + y);

    let horiz = |cx: f32, cy: f32, len: f32| -> Vec<Pos2> {
        let (l, ht) = (len / 2.0, t / 2.0);
        vec![
            to(cx - l, cy),
            to(cx - l + ht, cy - ht),
            to(cx + l - ht, cy - ht),
            to(cx + l, cy),
            to(cx + l - ht, cy + ht),
            to(cx - l + ht, cy + ht),
        ]
    };
    let vert = |cx: f32, cy: f32, len: f32| -> Vec<Pos2> {
        let (l, ht) = (len / 2.0, t / 2.0);
        vec![
            to(cx, cy - l),
            to(cx + ht, cy - l + ht),
            to(cx + ht, cy + l - ht),
            to(cx, cy + l),
            to(cx - ht, cy + l - ht),
            to(cx - ht, cy - l + ht),
        ]
    };
    let (left, right) = (t / 2.0, w - t / 2.0 - w * 0.08);
    let (top, mid, bottom) = (t / 2.0, h / 2.0, h - t / 2.0);
    let hl = right - left - gap * 2.0;
    let vl = mid - top - gap * 2.0;
    let hc = (left + right) / 2.0;
    let segs = [
        horiz(hc, top, hl),
        vert(right, (top + mid) / 2.0, vl),
        vert(right, (mid + bottom) / 2.0, vl),
        horiz(hc, bottom, hl),
        vert(left, (mid + bottom) / 2.0, vl),
        vert(left, (top + mid) / 2.0, vl),
        horiz(hc, mid, hl),
    ];
    for (i, pts) in segs.into_iter().enumerate() {
        if bits & (1 << i) != 0 {
            painter.add(Shape::convex_polygon(pts.clone(), halo(color, 30), Stroke::new(t * 0.9, halo(color, 34))));
            painter.add(Shape::convex_polygon(pts, color, Stroke::NONE));
        } else {
            painter.add(Shape::convex_polygon(pts, pal.ghost, Stroke::NONE));
        }
    }
    let dp_center = to(w - w * 0.02, bottom);
    painter.circle_filled(dp_center, t * 0.55, if dp { color } else { pal.ghost });
}

/// A right-aligned number in `digits` seven-segment cells, with ghost "8"s
/// in the unused cells. `None` shows dashes, as a sensor with no reading.
pub fn seg_number(painter: &Painter, r: Rect, digits: usize, value: Option<f64>, decimals: usize, pal: &Palette, color: Color32) {
    let text = match value {
        Some(v) => format!("{:.*}", decimals, v),
        None => "-".repeat(digits),
    };
    // Split into cells: a '.' lights the previous cell's point
    let mut cells: Vec<(char, bool)> = Vec::new();
    for ch in text.chars() {
        if ch == '.' {
            if let Some(last) = cells.last_mut() {
                last.1 = true;
            }
        } else {
            cells.push((ch, false));
        }
    }
    let overflow = cells.len() > digits;
    if overflow {
        cells = vec![('-', false); digits];
    }
    let cw = r.width() / digits as f32;
    let pad = digits - cells.len();
    for i in 0..digits {
        let cell = Rect::from_min_size(pos2(r.min.x + i as f32 * cw, r.min.y), vec2(cw * 0.92, r.height()));
        let (ch, dp) = if i < pad { (' ', false) } else { cells[i - pad] };
        seg_digit(painter, cell, ch, dp, pal, color);
    }
}

// ── bars, lamps, panels ─────────────────────────────────────────────────

/// A vertical ladder of `n` segments filled to `frac` (0..1) from the
/// bottom; `color_at` picks each segment's colour from its position (0..1).
pub fn ladder(painter: &Painter, r: Rect, n: usize, frac: Option<f64>, color_at: impl Fn(f64) -> Color32, pal: &Palette) {
    let pitch = r.height() / n as f32;
    let lit_n = frac.map(|f| (f.clamp(0.0, 1.0) * n as f64).round() as usize).unwrap_or(0);
    for i in 0..n {
        let y1 = r.max.y - i as f32 * pitch;
        let seg = Rect::from_min_max(pos2(r.min.x, y1 - pitch * 0.72), pos2(r.max.x, y1));
        let color = color_at((i as f64 + 0.5) / n as f64);
        if i < lit_n {
            painter.rect_filled(seg.expand(2.0), CornerRadius::same(2), halo(color, 30));
            painter.rect_filled(seg, CornerRadius::same(1), color);
        } else {
            painter.rect_filled(seg, CornerRadius::same(1), pal.ghost);
        }
    }
}

/// A horizontal row of `n` segments filled to `frac`.
pub fn strip(painter: &Painter, r: Rect, n: usize, frac: f64, color: Color32, pal: &Palette) {
    let pitch = r.width() / n as f32;
    let lit_n = (frac.clamp(0.0, 1.0) * n as f64).round() as usize;
    for i in 0..n {
        let x0 = r.min.x + i as f32 * pitch;
        let seg = Rect::from_min_max(pos2(x0, r.min.y), pos2(x0 + pitch * 0.7, r.max.y));
        if i < lit_n {
            painter.rect_filled(seg.expand(1.5), CornerRadius::same(2), halo(color, 30));
            painter.rect_filled(seg, CornerRadius::same(1), color);
        } else {
            painter.rect_filled(seg, CornerRadius::same(1), pal.ghost);
        }
    }
}

/// A tell-tale lamp: a rounded box with a dot-matrix legend.
pub fn lamp(painter: &Painter, r: Rect, label: &str, on: bool, color: Color32, pal: &Palette) {
    let n = label.chars().count() as f32;
    let p = (r.height() / 11.0).min(r.width() / (n * 6.0 + 4.0)).max(1.0);
    if on {
        painter.rect_filled(r.expand(3.0), CornerRadius::same(6), halo(color, 40));
        painter.rect_filled(r, CornerRadius::same(4), color);
        text_center(painter, r.center(), p, label, pal.bg);
    } else {
        painter.rect_stroke(r, CornerRadius::same(4), Stroke::new(1.5, pal.ghost), StrokeKind::Inside);
        text_center(painter, r.center(), p, label, pal.ghost);
    }
}

/// A recessed glass window with a bezel, the frame of every display.
pub fn window(painter: &Painter, r: Rect, pal: &Palette) {
    painter.rect_filled(r.expand(4.0), CornerRadius::same(10), pal.bezel);
    painter.rect_filled(r, CornerRadius::same(7), pal.glass);
    // A faint reflection across the top third of the glass
    let sheen = Rect::from_min_max(r.min + vec2(6.0, 4.0), pos2(r.max.x - 6.0, r.min.y + r.height() * 0.28));
    painter.rect_filled(sheen, CornerRadius::same(6), Color32::from_white_alpha(3));
}

/// A hardware push key. Returns nothing; the caller does the hit test.
pub fn key(painter: &Painter, r: Rect, label: &str, active: bool, pressed: bool, pal: &Palette) {
    let body = if pressed { r.translate(vec2(0.0, 2.0)) } else { r };
    if !pressed {
        painter.rect_filled(r.translate(vec2(0.0, 4.0)), CornerRadius::same(8), Color32::from_black_alpha(160));
    }
    painter.rect_filled(body, CornerRadius::same(8), pal.bezel);
    painter.rect_stroke(body, CornerRadius::same(8), Stroke::new(1.0, Color32::from_white_alpha(14)), StrokeKind::Inside);
    let n = label.chars().count() as f32;
    let p = (body.height() / 14.0).min(body.width() / (n * 6.0 + 8.0)).clamp(1.0, 4.0);
    let c = if active { pal.lit } else { halo(pal.lit, 150) };
    text_center(painter, body.center(), p, label, c);
    // The key's indicator LED
    let led = pos2(body.max.x - 10.0, body.min.y + 10.0);
    painter.circle_filled(led, 3.0, if active { pal.lit } else { pal.ghost });
}

/// CRT-style scanlines over the whole screen.
pub fn scanlines(painter: &Painter, r: Rect, spacing: f32) {
    let mut y = r.min.y;
    let stroke = Stroke::new(1.0, Color32::from_black_alpha(55));
    while y < r.max.y {
        painter.line_segment([pos2(r.min.x, y), pos2(r.max.x, y)], stroke);
        y += spacing;
    }
}
