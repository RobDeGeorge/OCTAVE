#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "font.h"

/* ── palette: flat, no gradients, the Out Run sunset ──────────────────── */
#define C_BG      12, 10, 20
#define C_PANEL   40, 36, 64
#define C_LINE    90, 60, 120
#define C_INK     240, 240, 200
#define C_DIM     120, 120, 130
#define C_PINK    240, 80, 140
#define C_ORANGE  255, 140, 60
#define C_YELLOW  255, 210, 70
#define C_CYAN    60, 190, 220
#define C_BLUE    30, 110, 190
#define C_GREEN   60, 200, 120
#define C_RED     200, 60, 60

static void fill(SDL_Renderer *r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect q = {x, y, w, h};
    SDL_RenderFillRect(r, &q);
}

static void box(SDL_Renderer *r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect q = {x, y, w, h};
    SDL_RenderDrawRect(r, &q);
}

/* ── layout: where everything sits, once per canvas shape ─────────────── */
/* Text in the span [x, x + w): at its left, its middle or its right. */
enum { LEFT, MID, RIGHT };
typedef struct { int x, y, w, scale, align; } Text;
/* A button: a fixed rectangle, hit-tested in ui_tap. */
typedef struct { int x, y, w, h; const char *label; int id; } Button;

enum { B_NONE, B_KEY_0, B_KEY_1, B_KEY_2, B_KEY_3, B_KEY_4, B_KEY_5, B_KEY_6, B_KEY_7, B_KEY_8, B_KEY_9, B_KEY_DEL, B_KEY_OK, B_TAB_DRIVE, B_TAB_MEDIA, B_TAB_DTC, B_PREV, B_PLAY, B_NEXT, B_VOL_DOWN, B_VOL_UP, B_MUTE, B_SHUFFLE,
       B_READ, B_CLEAR, B_YES, B_NO, B_OBD_CONNECT };

#define COUNT(a) (sizeof(a) / sizeof(a)[0])

typedef struct {
    int w, h;
    /* chrome: the bar at the top, the tabs at the bottom */
    int top_h;
    Text brand, brand2, link, car, clock;   /* the link and car lamps sit left of their text */
    int stripes;                            /* horizon lines behind everything */
    int tabs_y;                             /* the rule above the tabs */
    Button tabs[3];
    SDL_Rect offline;                       /* the NO SIGNAL box */
    Text note;                              /* 'TOKEN SAVED' after a pair */
    /* DRIVE */
    Text speed, unit, rpm_label, rpm_value;
    SDL_Rect rpm_bar, shift;
    Text row_label, row_value;              /* the first of the three small ladders... */
    SDL_Rect row_bar;
    int row_pitch;                          /* ...and how far down the next one is */
    Text obd_state;
    Button connect;
    /* MEDIA */
    SDL_Rect cover, text_clip;
    Text title, artist;                     /* a title wider than its span scrolls */
    SDL_Rect tape;
    int tape_n;
    Text pos, dur, shuffle, status, vol_label, vol_value;
    SDL_Rect vol_bar;
    int vol_n;
    Button media[7];
    /* DTC */
    Text dtc_title, dtc_hint, dtc_none, dtc_more, dtc_message;
    SDL_Rect lamp, rule;
    SDL_Rect tile;                          /* the first code; two columns */
    int tile_dx, tile_dy, tile_rows;
    Button dtc[2];
    SDL_Rect confirm;                       /* the YES / NO box */
    Button confirm_buttons[2];
    /* PAIR */
    Text pair_title, pair_sub, pair_error, pair_hint;
    SDL_Rect cell;                          /* the first of the six code cells */
    int cell_pitch, digit_dx, digit_dy, digit_scale;
    Button pad[12];
} Layout;

/* 320x200: wider than tall, the head unit */
static const Layout wide = {
    .w = UI_WIDE_W, .h = UI_WIDE_H,
    .top_h = 14,
    .brand = {4, 4, 36, 1, LEFT}, .brand2 = {50, 4, 36, 1, LEFT},
    .link = {104, 4, 54, 1, LEFT}, .car = {168, 4, 36, 1, LEFT}, .clock = {4, 4, 312, 1, RIGHT},
    .stripes = 6,
    .tabs_y = 178,
    .tabs = {{4, 182, 100, 16, "DRIVE", B_TAB_DRIVE}, {110, 182, 100, 16, "MEDIA", B_TAB_MEDIA}, {216, 182, 100, 16, "DTC", B_TAB_DTC}},
    .offline = {60, 80, 200, 30},
    .note = {4, 170, 312, 1, RIGHT},

    .speed = {16, 28, 153, 9, LEFT}, .unit = {184, 60, 34, 2, LEFT},
    .rpm_label = {16, 84, 288, 1, LEFT}, .rpm_value = {16, 84, 288, 1, RIGHT},
    .rpm_bar = {16, 92, 288, 12}, .shift = {16, 108, 288, 8},
    .row_label = {16, 126, 30, 1, LEFT}, .row_value = {166, 126, 40, 1, LEFT},
    .row_bar = {48, 124, 112, 8}, .row_pitch = 18,
    .obd_state = {212, 128, 100, 1, LEFT},
    .connect = {212, 150, 100, 18, "CONNECT", B_OBD_CONNECT},

    .cover = {16, 28, 72, 72}, .text_clip = {96, 26, 216, 40},
    .title = {96, 30, 216, 2, LEFT}, .artist = {96, 48, 216, 1, LEFT},
    .tape = {96, 64, 216, 6}, .tape_n = 36,
    .pos = {96, 74, 216, 1, LEFT}, .dur = {96, 74, 216, 1, RIGHT}, .shuffle = {96, 74, 216, 1, MID},
    .status = {96, 90, 216, 1, LEFT},
    .vol_label = {16, 110, 72, 1, LEFT}, .vol_value = {16, 120, 72, 3, LEFT},
    .vol_bar = {128, 160, 104, 10}, .vol_n = 20,
    .media = {
        {96, 128, 40, 22, "\x05", B_PREV}, {140, 128, 40, 22, "\x03", B_PLAY}, {184, 128, 40, 22, "\x06", B_NEXT}, {228, 128, 40, 22, "SHF", B_SHUFFLE},
        {96, 156, 28, 18, "-", B_VOL_DOWN}, {236, 156, 28, 18, "+", B_VOL_UP}, {272, 156, 44, 18, "MUTE", B_MUTE},
    },

    .dtc_title = {16, 26, 220, 2, LEFT},
    .lamp = {240, 24, 64, 16}, .rule = {16, 44, 288, 1},
    .dtc_hint = {16, 56, 288, 1, LEFT}, .dtc_none = {16, 56, 288, 2, LEFT},
    .tile = {16, 52, 140, 18}, .tile_dx = 148, .tile_dy = 22, .tile_rows = 4,
    .dtc_more = {16, 142, 288, 1, LEFT},
    .dtc_message = {220, 158, 1000, 1, LEFT},
    .dtc = {{12, 150, 96, 22, "READ", B_READ}, {116, 150, 96, 22, "CLEAR", B_CLEAR}},
    .confirm = {40, 60, 240, 80},
    .confirm_buttons = {{80, 100, 70, 22, "YES", B_YES}, {170, 100, 70, 22, "NO", B_NO}},

    .pair_title = {0, 22, 320, 2, MID}, .pair_sub = {0, 36, 320, 1, MID},
    .cell = {20, 60, 12, 16}, .cell_pitch = 14, .digit_dx = 4, .digit_dy = 5, .digit_scale = 1,
    .pair_error = {20, 90, 1000, 1, LEFT},
    .pad = {
        {118, 46, 26, 22, "1", B_KEY_1}, {148, 46, 26, 22, "2", B_KEY_2}, {178, 46, 26, 22, "3", B_KEY_3},
        {118, 72, 26, 22, "4", B_KEY_4}, {148, 72, 26, 22, "5", B_KEY_5}, {178, 72, 26, 22, "6", B_KEY_6},
        {118, 98, 26, 22, "7", B_KEY_7}, {148, 98, 26, 22, "8", B_KEY_8}, {178, 98, 26, 22, "9", B_KEY_9},
        {118, 124, 26, 22, "<", B_KEY_DEL}, {148, 124, 26, 22, "0", B_KEY_0}, {178, 124, 26, 22, "OK", B_KEY_OK},
    },
    .pair_hint = {20, 150, 280, 1, LEFT},
};

/* 200x320: taller than wide, a phone held upright. One column, read from
 * the top; what a finger presses is at least 20 canvas pixels high. */
static const Layout tall = {
    .w = UI_TALL_W, .h = UI_TALL_H,
    .top_h = 24,
    .brand = {4, 4, 36, 1, LEFT}, .brand2 = {50, 4, 36, 1, LEFT},
    .link = {12, 14, 54, 1, LEFT}, .car = {88, 14, 36, 1, LEFT}, .clock = {4, 4, 192, 1, RIGHT},
    .stripes = 10,
    .tabs_y = 288,
    .tabs = {{4, 292, 62, 24, "DRIVE", B_TAB_DRIVE}, {69, 292, 62, 24, "MEDIA", B_TAB_MEDIA}, {134, 292, 62, 24, "DTC", B_TAB_DTC}},
    .offline = {10, 140, 180, 30},
    .note = {4, 279, 192, 1, RIGHT},

    .speed = {15, 38, 170, 10, LEFT}, .unit = {16, 94, 168, 2, RIGHT},
    .rpm_label = {16, 118, 168, 1, LEFT}, .rpm_value = {16, 118, 168, 1, RIGHT},
    .rpm_bar = {16, 127, 168, 14}, .shift = {16, 146, 166, 10},
    .row_label = {16, 170, 30, 1, LEFT}, .row_value = {150, 170, 34, 1, RIGHT},
    .row_bar = {48, 168, 96, 10}, .row_pitch = 22,
    .obd_state = {16, 236, 168, 1, MID},
    .connect = {16, 246, 168, 26, "CONNECT", B_OBD_CONNECT},

    .cover = {52, 30, 96, 96}, .text_clip = {8, 132, 184, 24},
    .title = {8, 134, 184, 2, MID}, .artist = {16, 150, 168, 1, MID},
    .tape = {17, 160, 168, 6}, .tape_n = 28,
    .pos = {16, 170, 168, 1, LEFT}, .dur = {16, 170, 168, 1, RIGHT}, .shuffle = {16, 170, 168, 1, MID},
    .status = {16, 182, 168, 1, LEFT},
    .vol_label = {16, 182, 144, 1, RIGHT}, .vol_value = {16, 182, 168, 1, RIGHT},
    .vol_bar = {56, 232, 88, 10}, .vol_n = 15,
    .media = {
        {16, 192, 52, 28, "\x05", B_PREV}, {74, 192, 52, 28, "\x03", B_PLAY}, {132, 192, 52, 28, "\x06", B_NEXT}, {16, 254, 80, 20, "SHUFFLE", B_SHUFFLE},
        {16, 226, 34, 22, "-", B_VOL_DOWN}, {150, 226, 34, 22, "+", B_VOL_UP}, {104, 254, 80, 20, "MUTE", B_MUTE},
    },

    .dtc_title = {16, 32, 168, 2, LEFT},
    .lamp = {16, 48, 168, 14}, .rule = {16, 68, 168, 1},
    .dtc_hint = {16, 78, 168, 1, LEFT}, .dtc_none = {16, 78, 168, 2, LEFT},
    .tile = {16, 76, 80, 18}, .tile_dx = 88, .tile_dy = 22, .tile_rows = 5,
    .dtc_more = {16, 188, 168, 1, LEFT},
    .dtc_message = {16, 228, 168, 1, LEFT},
    .dtc = {{16, 250, 80, 24, "READ", B_READ}, {104, 250, 80, 24, "CLEAR", B_CLEAR}},
    .confirm = {10, 96, 180, 88},
    .confirm_buttons = {{24, 140, 70, 28, "YES", B_YES}, {106, 140, 70, 28, "NO", B_NO}},

    .pair_title = {0, 34, 200, 2, MID}, .pair_sub = {0, 50, 200, 1, MID},
    .cell = {24, 62, 22, 26}, .cell_pitch = 26, .digit_dx = 6, .digit_dy = 8, .digit_scale = 2,
    .pair_error = {16, 92, 168, 1, LEFT},
    .pad = {
        {16, 112, 52, 30, "1", B_KEY_1}, {74, 112, 52, 30, "2", B_KEY_2}, {132, 112, 52, 30, "3", B_KEY_3},
        {16, 148, 52, 30, "4", B_KEY_4}, {74, 148, 52, 30, "5", B_KEY_5}, {132, 148, 52, 30, "6", B_KEY_6},
        {16, 184, 52, 30, "7", B_KEY_7}, {74, 184, 52, 30, "8", B_KEY_8}, {132, 184, 52, 30, "9", B_KEY_9},
        {16, 220, 52, 30, "<", B_KEY_DEL}, {74, 220, 52, 30, "0", B_KEY_0}, {132, 220, 52, 30, "OK", B_KEY_OK},
    },
    .pair_hint = {16, 260, 168, 1, LEFT},
};

static const Layout *layout(const Ui *ui) { return ui->tall ? &tall : &wide; }

bool ui_set_tall(Ui *ui, bool is_tall)
{
    if (ui->tall == is_tall)
        return false;
    ui->tall = is_tall;
    return true;
}

void ui_size(const Ui *ui, int *w, int *h)
{
    *w = layout(ui)->w;
    *h = layout(ui)->h;
}

static void fill_rect(SDL_Renderer *r, const SDL_Rect *q, Uint8 cr, Uint8 cg, Uint8 cb)
{
    fill(r, q->x, q->y, q->w, q->h, cr, cg, cb);
}

/* Text in its span, in the current render colour. In the middle only
 * while it fits: longer text starts at the left edge. */
static void put(SDL_Renderer *r, const Text *t, const char *text)
{
    int x = t->x, tw = font_width(text, t->scale);
    if (t->align == RIGHT) x += t->w - tw;
    else if (t->align == MID && tw < t->w) x += (t->w - tw) / 2;
    font_draw(r, x, t->y, t->scale, text);
}

/* How many characters fit in the span. */
static int text_fits(const Text *t)
{
    int n = (t->w + t->scale) / (6 * t->scale);
    return n < 1 ? 1 : n;
}

/* Text from the left of its span, broken at spaces to fit it, starting at
 * `y`. Returns the y of the line below. */
static int put_lines(SDL_Renderer *r, const Text *t, int y, const char *text)
{
    int max = text_fits(t);
    while (*text) {
        int n = (int)strlen(text);
        if (n > max) {
            n = max;
            while (n > 0 && text[n] != ' ') n--;
            if (n == 0) n = max;   /* one long word: cut it */
        }
        char line[LINK_TEXT + 1];
        snprintf(line, sizeof line, "%.*s", n, text);
        font_draw(r, t->x, y, t->scale, line);
        y += 10 * t->scale;
        text += n;
        while (*text == ' ') text++;
    }
    return y;
}

static void draw_button(SDL_Renderer *r, const Button *b, bool on, bool big)
{
    if (on) {
        fill(r, b->x, b->y, b->w, b->h, C_YELLOW);
        SDL_SetRenderDrawColor(r, C_BG, 255);
    } else {
        fill(r, b->x, b->y, b->w, b->h, C_PANEL);
        box(r, b->x, b->y, b->w, b->h, C_LINE);
        SDL_SetRenderDrawColor(r, C_INK, 255);
    }
    int scale = big ? 2 : 1;
    font_draw_centered(r, b->x, b->w, b->y + (b->h - 5 * scale) / 2, scale, b->label);
}

static int hit(const Button *list, size_t n, int x, int y)
{
    for (size_t i = 0; i < n; i++)
        if (x >= list[i].x && x < list[i].x + list[i].w && y >= list[i].y && y < list[i].y + list[i].h)
            return list[i].id;
    return B_NONE;
}

/* ── shared chrome ────────────────────────────────────────────────────── */
static void draw_top(SDL_Renderer *r, const Layout *L, const Ui *ui, const State *s)
{
    fill(r, 0, 0, L->w, L->top_h, C_PANEL);
    fill(r, 0, L->top_h, L->w, 1, C_PINK);
    SDL_SetRenderDrawColor(r, C_YELLOW, 255);
    put(r, &L->brand, "OCTAVE");
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    put(r, &L->brand2, "ARCADE");
    /* link lamp */
    if (s->online) fill(r, L->link.x - 8, L->link.y + 1, 4, 4, C_GREEN);
    else if (ui->frame / 15 % 2) fill(r, L->link.x - 8, L->link.y + 1, 4, 4, C_RED);
    SDL_SetRenderDrawColor(r, C_INK, 255);
    put(r, &L->link, s->online ? "LINK" : "NO SIGNAL");
    /* car */
    if (s->online) {
        if (s->obd_connected) fill(r, L->car.x - 8, L->car.y + 1, 4, 4, C_CYAN);
        SDL_SetRenderDrawColor(r, s->obd_connected ? 240 : 120, s->obd_connected ? 240 : 120, s->obd_connected ? 200 : 130, 255);
        put(r, &L->car, s->obd_connected ? "CAR" : "NO CAR");
    }
    SDL_SetRenderDrawColor(r, C_INK, 255);
    if (s->clock[0])
        put(r, &L->clock, s->clock);
}

static void draw_tabs(SDL_Renderer *r, const Layout *L, const Ui *ui)
{
    fill(r, 0, L->tabs_y, L->w, 1, C_LINE);
    for (int i = 0; i < 3; i++)
        draw_button(r, &L->tabs[i], (int)ui->page == i, false);
}

/* A segmented bar: `n` blocks, `lit` of them on, the top `red_from` red. */
static void segments(SDL_Renderer *r, const SDL_Rect *q, int n, int lit, int red_from)
{
    int gap = 2, bw = (q->w - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        int bx = q->x + i * (bw + gap);
        if (i < lit) {
            if (i >= red_from) fill(r, bx, q->y, bw, q->h, C_RED);
            else if (i >= n * 2 / 3) fill(r, bx, q->y, bw, q->h, C_ORANGE);
            else fill(r, bx, q->y, bw, q->h, C_CYAN);
        } else {
            fill(r, bx, q->y, bw, q->h, C_PANEL);
        }
    }
}

static void mmss(char *out, size_t n, long ms)
{
    long sec = ms < 0 ? 0 : ms / 1000;
    if (sec > 359999) sec = 359999;   /* 99:59:59 at most, keeps the format in bounds */
    snprintf(out, n, "%ld:%02d", sec / 60, (int)(sec % 60));
}

/* ── DRIVE ────────────────────────────────────────────────────────────── */
static void draw_drive(SDL_Renderer *r, const Layout *L, const Ui *ui, const State *s)
{
    char buf[32];
    /* speed: huge digits */
    if (s->have_speed) snprintf(buf, sizeof buf, "%3.0f", s->speed);
    else snprintf(buf, sizeof buf, "---");
    SDL_SetRenderDrawColor(r, C_INK, 255);
    put(r, &L->speed, buf);
    SDL_SetRenderDrawColor(r, C_PINK, 255);
    put(r, &L->unit, "MPH");
    /* rpm ladder */
    int rpm_n = 24, rpm_lit = s->have_rpm ? (int)lround(s->rpm / 8000.0 * rpm_n) : 0;
    if (rpm_lit > rpm_n) rpm_lit = rpm_n;
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    put(r, &L->rpm_label, "RPM");
    if (s->have_rpm) {
        snprintf(buf, sizeof buf, "%4.0f", s->rpm);
        SDL_SetRenderDrawColor(r, C_INK, 255);
        put(r, &L->rpm_value, buf);
    }
    segments(r, &L->rpm_bar, rpm_n, rpm_lit, rpm_n * 5 / 6);
    if (s->shift_lit && ui->frame / 4 % 2) {
        fill_rect(r, &L->shift, C_YELLOW);
        SDL_SetRenderDrawColor(r, C_BG, 255);
        font_draw_centered(r, L->shift.x, L->shift.w, L->shift.y + (L->shift.h - 5) / 2, 1, "SHIFT");
    }
    /* coolant, fuel, volts: three small ladders */
    struct { const char *name; bool have; double v, lo, hi; const char *fmt; } rows[3] = {
        {"TEMP", s->have_coolant, s->coolant, 0, 130, "%3.0fC"},
        {"FUEL", s->have_fuel, s->fuel, 0, 100, "%3.0f%%"},
        {"VOLT", s->have_voltage, s->voltage, 10, 16, "%4.1fV"},
    };
    for (int i = 0; i < 3; i++) {
        Text label = L->row_label, value = L->row_value;
        SDL_Rect bar = L->row_bar;
        label.y += i * L->row_pitch;
        value.y += i * L->row_pitch;
        bar.y += i * L->row_pitch;
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        put(r, &label, rows[i].name);
        int n = 16, lit = 0;
        if (rows[i].have) {
            double f = (rows[i].v - rows[i].lo) / (rows[i].hi - rows[i].lo);
            if (f < 0) f = 0;
            if (f > 1) f = 1;
            lit = (int)lround(f * n);
            snprintf(buf, sizeof buf, rows[i].fmt, rows[i].v);
        } else {
            snprintf(buf, sizeof buf, "----");
        }
        segments(r, &bar, n, lit, i == 0 ? n * 6 / 7 : n + 1);
        SDL_SetRenderDrawColor(r, C_INK, 255);
        put(r, &value, buf);
    }
    if (!s->obd_connected && s->online) {
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        put(r, &L->obd_state, s->obd_state[0] ? s->obd_state : "NO CAR");
        draw_button(r, &L->connect, false, false);
    }
}

/* ── MEDIA ────────────────────────────────────────────────────────────── */
static void draw_media(SDL_Renderer *r, const Layout *L, Ui *ui, const State *s)
{
    /* cover: chunky pixels, 3 or 4 canvas pixels each */
    SDL_Rect dst = L->cover;
    if (ui->art.tex) {
        SDL_RenderCopy(r, ui->art.tex, NULL, &dst);
    } else {
        fill(r, dst.x, dst.y, dst.w, dst.h, C_PANEL);
        box(r, dst.x, dst.y, dst.w, dst.h, C_LINE);
        SDL_SetRenderDrawColor(r, C_PINK, 255);
        font_draw_centered(r, dst.x, dst.w, dst.y + (dst.h - 12) / 2, 2, "\x07");
    }
    box(r, dst.x - 2, dst.y - 2, dst.w + 4, dst.h + 4, C_PINK);
    /* text: title scrolls when long */
    char title[LINK_TEXT + 4], artist[LINK_TEXT];
    snprintf(title, sizeof title, "%s", s->has_track ? (s->title[0] ? s->title : "UNTITLED") : "NOTHING PLAYING");
    snprintf(artist, sizeof artist, "%s", s->has_track ? s->artist : "PICK A SONG");
    SDL_RenderSetClipRect(r, &L->text_clip);
    SDL_SetRenderDrawColor(r, C_INK, 255);
    int tw = font_width(title, L->title.scale);
    if (tw > L->title.w) {
        int span = tw - L->title.w + 24;
        int t = (int)(ui->frame / 2) % (span * 2);
        font_draw(r, L->title.x - (t < span ? t : span * 2 - t), L->title.y, L->title.scale, title);
    } else {
        put(r, &L->title, title);
    }
    SDL_SetRenderDrawColor(r, C_CYAN, 255);
    put(r, &L->artist, artist);
    SDL_RenderSetClipRect(r, NULL);
    /* progress tape */
    long pos = s->position_ms;
    if (s->playing) pos += (long)(SDL_GetTicks() - s->position_at);
    if (s->duration_ms > 0 && pos > s->duration_ms) pos = s->duration_ms;
    int n = L->tape_n, lit = s->duration_ms > 0 ? (int)((double)pos / (double)s->duration_ms * n) : 0;
    segments(r, &L->tape, n, lit, n + 1);
    char a[24], b[24];
    mmss(a, sizeof a, pos);
    mmss(b, sizeof b, s->duration_ms);
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    put(r, &L->pos, a);
    put(r, &L->dur, b);
    if (s->shuffled) {
        SDL_SetRenderDrawColor(r, C_YELLOW, 255);
        put(r, &L->shuffle, "SHUFFLE");
    }
    /* status line */
    SDL_SetRenderDrawColor(r, C_PINK, 255);
    put(r, &L->status, s->playing ? "PLAYING" : s->has_track ? "PAUSED" : "STOPPED");
    /* transport */
    for (size_t i = 0; i < COUNT(L->media); i++) {
        const Button *bt = &L->media[i];
        bool on = (bt->id == B_PLAY && s->playing) || (bt->id == B_MUTE && s->muted) || (bt->id == B_SHUFFLE && s->shuffled);
        if (bt->id == B_PLAY) {
            Button p = *bt;
            p.label = s->playing ? "\x04" : "\x03";
            draw_button(r, &p, on, true);
        } else {
            draw_button(r, bt, on, bt->id == B_PREV || bt->id == B_NEXT || bt->id == B_VOL_DOWN || bt->id == B_VOL_UP);
        }
    }
    /* volume ladder between - and + */
    int vn = L->vol_n;
    segments(r, &L->vol_bar, vn, s->muted ? 0 : (s->volume * vn + 50) / 100, vn + 1);
    char v[8];
    snprintf(v, sizeof v, "%d", s->volume);
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    put(r, &L->vol_label, "VOL");
    SDL_SetRenderDrawColor(r, C_INK, 255);
    put(r, &L->vol_value, v);
}

/* ── DTC ──────────────────────────────────────────────────────────────── */
static void draw_dtc(SDL_Renderer *r, const Layout *L, const Ui *ui, const State *s)
{
    SDL_SetRenderDrawColor(r, C_INK, 255);
    put(r, &L->dtc_title, "TROUBLE CODES");
    /* lamp */
    const SDL_Rect *lamp = &L->lamp;
    if (s->mil) {
        fill_rect(r, lamp, (ui->frame / 10 % 2) ? 255 : 200, (ui->frame / 10 % 2) ? 140 : 100, 60);
        SDL_SetRenderDrawColor(r, C_BG, 255);
    } else {
        box(r, lamp->x, lamp->y, lamp->w, lamp->h, C_LINE);
        SDL_SetRenderDrawColor(r, C_DIM, 255);
    }
    font_draw_centered(r, lamp->x, lamp->w, lamp->y + (lamp->h - 4) / 2, 1, s->mil ? "CHECK ENG" : "LAMP OFF");
    fill_rect(r, &L->rule, C_LINE);
    int shown = L->tile_rows * 2;
    if (!s->online || !s->obd_connected) {
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        put(r, &L->dtc_hint, "CONNECT TO THE CAR FIRST");
    } else if (!s->dtc_known) {
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        put(r, &L->dtc_hint, "PRESS READ");
    } else if (s->dtc_count == 0) {
        SDL_SetRenderDrawColor(r, C_GREEN, 255);
        put_lines(r, &L->dtc_none, L->dtc_none.y, "NO CODES STORED");
    } else {
        for (int i = 0; i < s->dtc_count && i < shown; i++) {
            int x = L->tile.x + (i % 2) * L->tile_dx, y = L->tile.y + (i / 2) * L->tile_dy;
            fill(r, x, y, L->tile.w, L->tile.h, C_PANEL);
            fill(r, x, y, 4, L->tile.h, C_RED);
            SDL_SetRenderDrawColor(r, C_INK, 255);
            font_draw(r, x + 10, y + 4, 2, s->dtc[i].code);
        }
        if (s->dtc_count > shown) {
            char more[24];
            snprintf(more, sizeof more, "+%d MORE", s->dtc_count - shown);
            SDL_SetRenderDrawColor(r, C_DIM, 255);
            put(r, &L->dtc_more, more);
        }
    }
    for (size_t i = 0; i < COUNT(L->dtc); i++)
        draw_button(r, &L->dtc[i], false, true);
    if (s->dtc_message[0] && SDL_GetTicks() - s->dtc_message_at < 4000) {
        SDL_SetRenderDrawColor(r, C_YELLOW, 255);
        put_lines(r, &L->dtc_message, L->dtc_message.y, s->dtc_message);
    }
    if (ui->confirm_clear) {
        const SDL_Rect *q = &L->confirm;
        fill_rect(r, q, C_BG);
        box(r, q->x, q->y, q->w, q->h, C_PINK);
        box(r, q->x + 2, q->y + 2, q->w - 4, q->h - 4, C_PINK);
        SDL_SetRenderDrawColor(r, C_INK, 255);
        font_draw_centered(r, q->x, q->w, q->y + 10, 1, "CLEAR ALL CODES AND");
        font_draw_centered(r, q->x, q->w, q->y + 20, 1, "RESET THE LAMP?");
        for (size_t i = 0; i < COUNT(L->confirm_buttons); i++)
            draw_button(r, &L->confirm_buttons[i], i == 0, true);
    }
}

/* ── PAIR (protocol 2, over the network) ───────────────────────────────── */
static void draw_pair(SDL_Renderer *r, const Layout *L, const Ui *ui, const State *s)
{
    SDL_SetRenderDrawColor(r, C_INK, 255);
    put(r, &L->pair_title, "PAIR WITH OCTAVE");
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    put(r, &L->pair_sub, "CODE FROM SETTINGS > SERVICE");
    /* the six cells */
    int typed = (int)strlen(ui->code);
    for (int i = 0; i < 6; i++) {
        int x = L->cell.x + i * L->cell_pitch;
        if (i < typed) {
            char d[2] = {ui->code[i], 0};
            fill(r, x, L->cell.y, L->cell.w, L->cell.h, C_YELLOW);
            SDL_SetRenderDrawColor(r, C_BG, 255);
            font_draw(r, x + L->digit_dx, L->cell.y + L->digit_dy, L->digit_scale, d);
        } else {
            fill(r, x, L->cell.y, L->cell.w, L->cell.h, C_PANEL);
        }
    }
    for (size_t i = 0; i < COUNT(L->pad); i++)
        draw_button(r, &L->pad[i], L->pad[i].id == B_KEY_OK && typed == 6, L->pad[i].id != B_KEY_OK);
    if (s->pair_error[0]) {
        SDL_SetRenderDrawColor(r, C_RED, 255);
        put_lines(r, &L->pair_error, L->pair_error.y, s->pair_error);
    }
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    int y = L->pair_hint.y;
#ifdef __EMSCRIPTEN__
    y = put_lines(r, &L->pair_hint, y, "TAP THE DIGITS, THEN OK");
    y = put_lines(r, &L->pair_hint, y, "THIS BROWSER KEEPS THE TOKEN IF IT CAN");
#else
    y = put_lines(r, &L->pair_hint, y, "KEYS 0-9, BACKSPACE, ENTER");
    y = put_lines(r, &L->pair_hint, y, ui->token_path[0] ? "TOKEN IS SAVED TO" : "RUN WITH --token-file TO KEEP IT");
#endif
    if (ui->token_path[0]) {
        /* the end of the path, as much as one line holds */
        size_t len = strlen(ui->token_path), fits = (size_t)text_fits(&L->pair_hint);
        font_draw(r, L->pair_hint.x, y, L->pair_hint.scale, ui->token_path + (len > fits ? len - fits : 0));
    }
}

void ui_draw(Ui *ui, SDL_Renderer *r, Link *l, const State *s)
{
    const Layout *L = layout(ui);
    ui->frame++;
    art_update(&ui->art, r, l, s);
    SDL_SetRenderDrawColor(r, C_BG, 255);
    SDL_RenderClear(r);
    /* the road: a few horizon stripes, so the screen is never flat black */
    for (int i = 0; i < L->stripes; i++)
        fill(r, 0, 16 + i * 27, L->w, 1, 24 + i * 3, 20 + i * 3, 36 + i * 4);
    draw_top(r, L, ui, s);
    if (s->online && s->untrusted) {
        draw_pair(r, L, ui, s);
        return;
    }
    if (ui->token_note[0] && ui->frame % 600 < 300) {
        SDL_SetRenderDrawColor(r, C_GREEN, 255);
        put(r, &L->note, ui->token_note);
    }
    switch (ui->page) {
    case PAGE_DRIVE: draw_drive(r, L, ui, s); break;
    case PAGE_MEDIA: draw_media(r, L, ui, s); break;
    default: draw_dtc(r, L, ui, s); break;
    }
    draw_tabs(r, L, ui);
    if (!s->online) {
        const SDL_Rect *q = &L->offline;
        fill_rect(r, q, C_BG);
        box(r, q->x, q->y, q->w, q->h, C_RED);
        SDL_SetRenderDrawColor(r, C_RED, 255);
        font_draw_centered(r, q->x, q->w, q->y + 6, 1, "NO SIGNAL FROM OCTAVE");
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        font_draw_centered(r, q->x, q->w, q->y + 16, 1, "RETRYING...");
    }
}

static void act(Ui *ui, Link *l, const State *s, int id)
{
    char args[32];
    if (id >= B_KEY_0 && id <= B_KEY_9) {
        size_t n = strlen(ui->code);
        if (n < 6) { ui->code[n] = (char)('0' + (id - B_KEY_0)); ui->code[n + 1] = 0; }
        return;
    }
    switch (id) {
    case B_KEY_DEL: { size_t n = strlen(ui->code); if (n) ui->code[n - 1] = 0; break; }
    case B_KEY_OK: if (strlen(ui->code) == 6) { link_pair(l, ui->code, "OCTAVE Arcade"); ui->code[0] = 0; } break;
    case B_TAB_DRIVE: ui->page = PAGE_DRIVE; ui->confirm_clear = false; break;
    case B_TAB_MEDIA: ui->page = PAGE_MEDIA; ui->confirm_clear = false; break;
    case B_TAB_DTC: ui->page = PAGE_DTC; break;
    case B_PREV: link_call(l, "nowPlaying", "previous", NULL); break;
    case B_PLAY: link_call(l, "nowPlaying", "togglePlay", NULL); break;
    case B_NEXT: link_call(l, "nowPlaying", "next", NULL); break;
    case B_SHUFFLE: link_call(l, "nowPlaying", "toggleShuffle", NULL); break;
    case B_VOL_DOWN:
    case B_VOL_UP: {
        int v = s->volume + (id == B_VOL_UP ? 5 : -5);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        snprintf(args, sizeof args, "[%d]", v);
        link_call(l, "volumeController", "applyVolume", args);
        break;
    }
    case B_MUTE: link_set(l, "volumeController", "muted", s->muted ? "false" : "true"); break;
    case B_READ: link_call(l, "obdManager", "read_dtc", NULL); break;
    case B_CLEAR: if (s->obd_connected) ui->confirm_clear = true; break;
    case B_YES: ui->confirm_clear = false; link_call(l, "obdManager", "clear_dtc", NULL); break;
    case B_NO: ui->confirm_clear = false; break;
    case B_OBD_CONNECT: link_call(l, "obdManager", "connect_obd", NULL); break;
    default: break;
    }
}

void ui_tap(Ui *ui, Link *l, const State *s, int x, int y)
{
    const Layout *L = layout(ui);
    int id = B_NONE;
    if (s->online && s->untrusted) {
        act(ui, l, s, hit(L->pad, COUNT(L->pad), x, y));
        return;
    }
    if (ui->confirm_clear) {
        id = hit(L->confirm_buttons, COUNT(L->confirm_buttons), x, y);
    } else {
        id = hit(L->tabs, COUNT(L->tabs), x, y);
        if (id == B_NONE && ui->page == PAGE_MEDIA) id = hit(L->media, COUNT(L->media), x, y);
        if (id == B_NONE && ui->page == PAGE_DTC) id = hit(L->dtc, COUNT(L->dtc), x, y);
        if (id == B_NONE && ui->page == PAGE_DRIVE && !s->obd_connected) id = hit(&L->connect, 1, x, y);
    }
    act(ui, l, s, id);
}

void ui_key(Ui *ui, Link *l, const State *s, SDL_Keycode key)
{
    if (s->online && s->untrusted) {
        if (key >= SDLK_0 && key <= SDLK_9) act(ui, l, s, B_KEY_0 + (int)(key - SDLK_0));
        else if (key >= SDLK_KP_1 && key <= SDLK_KP_9) act(ui, l, s, B_KEY_1 + (int)(key - SDLK_KP_1));
        else if (key == SDLK_KP_0) act(ui, l, s, B_KEY_0);
        else if (key == SDLK_BACKSPACE) act(ui, l, s, B_KEY_DEL);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) act(ui, l, s, B_KEY_OK);
        return;
    }
    switch (key) {
    case SDLK_1: act(ui, l, s, B_TAB_DRIVE); break;
    case SDLK_2: act(ui, l, s, B_TAB_MEDIA); break;
    case SDLK_3: act(ui, l, s, B_TAB_DTC); break;
    case SDLK_TAB: act(ui, l, s, ui->page == PAGE_DRIVE ? B_TAB_MEDIA : ui->page == PAGE_MEDIA ? B_TAB_DTC : B_TAB_DRIVE); break;
    case SDLK_SPACE: act(ui, l, s, B_PLAY); break;
    case SDLK_LEFT: act(ui, l, s, B_PREV); break;
    case SDLK_RIGHT: act(ui, l, s, B_NEXT); break;
    case SDLK_UP: case SDLK_PLUS: case SDLK_EQUALS: act(ui, l, s, B_VOL_UP); break;
    case SDLK_DOWN: case SDLK_MINUS: act(ui, l, s, B_VOL_DOWN); break;
    case SDLK_m: act(ui, l, s, B_MUTE); break;
    case SDLK_r: if (ui->page == PAGE_DTC) act(ui, l, s, B_READ); break;
    case SDLK_y: if (ui->confirm_clear) act(ui, l, s, B_YES); break;
    case SDLK_n: case SDLK_ESCAPE: if (ui->confirm_clear) act(ui, l, s, B_NO); break;
    default: break;
    }
}
