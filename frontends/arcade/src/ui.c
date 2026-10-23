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

/* ── buttons: fixed rectangles, hit-tested in ui_tap ──────────────────── */
typedef struct { int x, y, w, h; const char *label; int id; } Button;

enum { B_NONE, B_TAB_DRIVE, B_TAB_MEDIA, B_TAB_DTC, B_PREV, B_PLAY, B_NEXT, B_VOL_DOWN, B_VOL_UP, B_MUTE, B_SHUFFLE,
       B_READ, B_CLEAR, B_YES, B_NO, B_OBD_CONNECT };

static const Button tabs[] = {
    {4, 182, 100, 16, "DRIVE", B_TAB_DRIVE}, {110, 182, 100, 16, "MEDIA", B_TAB_MEDIA}, {216, 182, 100, 16, "DTC", B_TAB_DTC},
};
static const Button media_buttons[] = {
    {96, 128, 40, 22, "\x05", B_PREV}, {140, 128, 40, 22, "\x03", B_PLAY}, {184, 128, 40, 22, "\x06", B_NEXT}, {228, 128, 40, 22, "SHF", B_SHUFFLE},
    {96, 156, 28, 18, "-", B_VOL_DOWN}, {236, 156, 28, 18, "+", B_VOL_UP}, {272, 156, 44, 18, "MUTE", B_MUTE},
};
static const Button dtc_buttons[] = {
    {12, 150, 96, 22, "READ", B_READ}, {116, 150, 96, 22, "CLEAR", B_CLEAR},
};
static const Button confirm_buttons[] = {
    {80, 100, 70, 22, "YES", B_YES}, {170, 100, 70, 22, "NO", B_NO},
};
static const Button drive_buttons[] = {
    {212, 150, 100, 18, "CONNECT", B_OBD_CONNECT},
};

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

static int hit(const Button *list, int n, int x, int y)
{
    for (int i = 0; i < n; i++)
        if (x >= list[i].x && x < list[i].x + list[i].w && y >= list[i].y && y < list[i].y + list[i].h)
            return list[i].id;
    return B_NONE;
}

/* ── shared chrome ────────────────────────────────────────────────────── */
static void draw_top(SDL_Renderer *r, const Ui *ui, const State *s)
{
    fill(r, 0, 0, UI_W, 14, C_PANEL);
    fill(r, 0, 14, UI_W, 1, C_PINK);
    SDL_SetRenderDrawColor(r, C_YELLOW, 255);
    font_draw(r, 4, 4, 1, "OCTAVE");
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    font_draw(r, 50, 4, 1, "ARCADE");
    /* link lamp */
    if (s->online) fill(r, 96, 5, 4, 4, C_GREEN);
    else if (ui->frame / 15 % 2) fill(r, 96, 5, 4, 4, C_RED);
    SDL_SetRenderDrawColor(r, C_INK, 255);
    font_draw(r, 104, 4, 1, s->online ? "LINK" : "NO SIGNAL");
    /* car */
    if (s->online) {
        if (s->obd_connected) fill(r, 160, 5, 4, 4, C_CYAN);
        SDL_SetRenderDrawColor(r, s->obd_connected ? 240 : 120, s->obd_connected ? 240 : 120, s->obd_connected ? 200 : 130, 255);
        font_draw(r, 168, 4, 1, s->obd_connected ? "CAR" : "NO CAR");
    }
    SDL_SetRenderDrawColor(r, C_INK, 255);
    if (s->clock[0])
        font_draw_right(r, UI_W - 4, 4, 1, s->clock);
}

static void draw_tabs(SDL_Renderer *r, const Ui *ui)
{
    fill(r, 0, 178, UI_W, 1, C_LINE);
    for (int i = 0; i < 3; i++)
        draw_button(r, &tabs[i], (int)ui->page == i, false);
}

/* A segmented bar: `n` blocks, `lit` of them on, the top `red_from` red. */
static void segments(SDL_Renderer *r, int x, int y, int w, int h, int n, int lit, int red_from)
{
    int gap = 2, bw = (w - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        int bx = x + i * (bw + gap);
        if (i < lit) {
            if (i >= red_from) fill(r, bx, y, bw, h, C_RED);
            else if (i >= n * 2 / 3) fill(r, bx, y, bw, h, C_ORANGE);
            else fill(r, bx, y, bw, h, C_CYAN);
        } else {
            fill(r, bx, y, bw, h, C_PANEL);
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
static void draw_drive(SDL_Renderer *r, const Ui *ui, const State *s)
{
    char buf[32];
    /* speed: huge digits */
    if (s->have_speed) snprintf(buf, sizeof buf, "%3.0f", s->speed);
    else snprintf(buf, sizeof buf, "---");
    SDL_SetRenderDrawColor(r, C_INK, 255);
    font_draw(r, 16, 28, 9, buf);
    SDL_SetRenderDrawColor(r, C_PINK, 255);
    font_draw(r, 184, 60, 2, "MPH");
    /* rpm ladder */
    int rpm_n = 24, rpm_lit = s->have_rpm ? (int)lround(s->rpm / 8000.0 * rpm_n) : 0;
    if (rpm_lit > rpm_n) rpm_lit = rpm_n;
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    font_draw(r, 16, 84, 1, "RPM");
    if (s->have_rpm) {
        snprintf(buf, sizeof buf, "%4.0f", s->rpm);
        SDL_SetRenderDrawColor(r, C_INK, 255);
        font_draw_right(r, 304, 84, 1, buf);
    }
    segments(r, 16, 92, 288, 12, rpm_n, rpm_lit, rpm_n * 5 / 6);
    if (s->shift_lit && ui->frame / 4 % 2) {
        fill(r, 16, 108, 288, 8, C_YELLOW);
        SDL_SetRenderDrawColor(r, C_BG, 255);
        font_draw_centered(r, 16, 288, 109, 1, "SHIFT");
    }
    /* coolant, fuel, volts: three small ladders */
    struct { const char *name; bool have; double v, lo, hi; const char *fmt; } rows[3] = {
        {"TEMP", s->have_coolant, s->coolant, 0, 130, "%3.0fC"},
        {"FUEL", s->have_fuel, s->fuel, 0, 100, "%3.0f%%"},
        {"VOLT", s->have_voltage, s->voltage, 10, 16, "%4.1fV"},
    };
    for (int i = 0; i < 3; i++) {
        int y = 124 + i * 18;
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        font_draw(r, 16, y + 2, 1, rows[i].name);
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
        segments(r, 48, y, 112, 8, n, lit, i == 0 ? n * 6 / 7 : n + 1);
        SDL_SetRenderDrawColor(r, C_INK, 255);
        font_draw(r, 166, y + 2, 1, buf);
    }
    if (!s->obd_connected && s->online) {
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        font_draw(r, 212, 128, 1, s->obd_state[0] ? s->obd_state : "NO CAR");
        draw_button(r, &drive_buttons[0], false, false);
    }
}

/* ── MEDIA ────────────────────────────────────────────────────────────── */
static void draw_media(SDL_Renderer *r, Ui *ui, const State *s)
{
    /* cover: 72x72 of chunky pixels */
    SDL_Rect dst = {16, 28, 72, 72};
    if (ui->art.tex) {
        SDL_RenderCopy(r, ui->art.tex, NULL, &dst);
    } else {
        fill(r, dst.x, dst.y, dst.w, dst.h, C_PANEL);
        box(r, dst.x, dst.y, dst.w, dst.h, C_LINE);
        SDL_SetRenderDrawColor(r, C_PINK, 255);
        font_draw_centered(r, dst.x, dst.w, dst.y + 30, 2, "\x07");
    }
    box(r, 14, 26, 76, 76, C_PINK);
    /* text: title scrolls when long */
    char title[LINK_TEXT + 4], artist[LINK_TEXT];
    snprintf(title, sizeof title, "%s", s->has_track ? (s->title[0] ? s->title : "UNTITLED") : "NOTHING PLAYING");
    snprintf(artist, sizeof artist, "%s", s->has_track ? s->artist : "PICK A SONG");
    int avail = 216;
    int tw = font_width(title, 2);
    int tx = 96;
    if (tw > avail) {
        int span = tw - avail + 24;
        int t = (int)(ui->frame / 2) % (span * 2);
        tx -= t < span ? t : span * 2 - t;
    }
    SDL_Rect clip = {96, 26, avail, 40};
    SDL_RenderSetClipRect(r, &clip);
    SDL_SetRenderDrawColor(r, C_INK, 255);
    font_draw(r, tx, 30, 2, title);
    SDL_SetRenderDrawColor(r, C_CYAN, 255);
    font_draw(r, 96, 48, 1, artist);
    SDL_RenderSetClipRect(r, NULL);
    /* progress tape */
    long pos = s->position_ms;
    if (s->playing) pos += (long)(SDL_GetTicks() - s->position_at);
    if (s->duration_ms > 0 && pos > s->duration_ms) pos = s->duration_ms;
    int n = 36, lit = s->duration_ms > 0 ? (int)((double)pos / (double)s->duration_ms * n) : 0;
    segments(r, 96, 64, 216, 6, n, lit, n + 1);
    char a[24], b[24];
    mmss(a, sizeof a, pos);
    mmss(b, sizeof b, s->duration_ms);
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    font_draw(r, 96, 74, 1, a);
    font_draw_right(r, 312, 74, 1, b);
    if (s->shuffled) {
        SDL_SetRenderDrawColor(r, C_YELLOW, 255);
        font_draw_centered(r, 96, 216, 74, 1, "SHUFFLE");
    }
    /* status line */
    SDL_SetRenderDrawColor(r, C_PINK, 255);
    font_draw(r, 96, 90, 1, s->playing ? "PLAYING" : s->has_track ? "PAUSED" : "STOPPED");
    /* transport */
    for (size_t i = 0; i < sizeof media_buttons / sizeof media_buttons[0]; i++) {
        const Button *bt = &media_buttons[i];
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
    int vn = 20;
    segments(r, 128, 160, 104, 10, vn, s->muted ? 0 : (s->volume * vn + 50) / 100, vn + 1);
    char v[8];
    snprintf(v, sizeof v, "%d", s->volume);
    SDL_SetRenderDrawColor(r, C_DIM, 255);
    font_draw(r, 16, 110, 1, "VOL");
    SDL_SetRenderDrawColor(r, C_INK, 255);
    font_draw(r, 16, 120, 3, v);
}

/* ── DTC ──────────────────────────────────────────────────────────────── */
static void draw_dtc(SDL_Renderer *r, const Ui *ui, const State *s)
{
    SDL_SetRenderDrawColor(r, C_INK, 255);
    font_draw(r, 16, 26, 2, "TROUBLE CODES");
    /* lamp */
    if (s->mil) {
        fill(r, 240, 24, 64, 16, (ui->frame / 10 % 2) ? 255 : 200, (ui->frame / 10 % 2) ? 140 : 100, 60);
        SDL_SetRenderDrawColor(r, C_BG, 255);
        font_draw_centered(r, 240, 64, 30, 1, "CHECK ENG");
    } else {
        box(r, 240, 24, 64, 16, C_LINE);
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        font_draw_centered(r, 240, 64, 30, 1, "LAMP OFF");
    }
    fill(r, 16, 44, 288, 1, C_LINE);
    if (!s->online || !s->obd_connected) {
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        font_draw(r, 16, 56, 1, "CONNECT TO THE CAR FIRST");
    } else if (!s->dtc_known) {
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        font_draw(r, 16, 56, 1, "PRESS READ");
    } else if (s->dtc_count == 0) {
        SDL_SetRenderDrawColor(r, C_GREEN, 255);
        font_draw(r, 16, 56, 2, "NO CODES STORED");
    } else {
        for (int i = 0; i < s->dtc_count && i < 8; i++) {
            int x = 16 + (i % 2) * 148, y = 52 + (i / 2) * 22;
            fill(r, x, y, 140, 18, C_PANEL);
            fill(r, x, y, 4, 18, C_RED);
            SDL_SetRenderDrawColor(r, C_INK, 255);
            font_draw(r, x + 10, y + 4, 2, s->dtc[i].code);
        }
        if (s->dtc_count > 8) {
            char more[24];
            snprintf(more, sizeof more, "+%d MORE", s->dtc_count - 8);
            SDL_SetRenderDrawColor(r, C_DIM, 255);
            font_draw(r, 16, 142, 1, more);
        }
    }
    for (size_t i = 0; i < sizeof dtc_buttons / sizeof dtc_buttons[0]; i++)
        draw_button(r, &dtc_buttons[i], false, true);
    if (s->dtc_message[0] && SDL_GetTicks() - s->dtc_message_at < 4000) {
        SDL_SetRenderDrawColor(r, C_YELLOW, 255);
        font_draw(r, 220, 158, 1, s->dtc_message);
    }
    if (ui->confirm_clear) {
        fill(r, 40, 60, 240, 80, C_BG);
        box(r, 40, 60, 240, 80, C_PINK);
        box(r, 42, 62, 236, 76, C_PINK);
        SDL_SetRenderDrawColor(r, C_INK, 255);
        font_draw_centered(r, 40, 240, 70, 1, "CLEAR ALL CODES AND");
        font_draw_centered(r, 40, 240, 80, 1, "RESET THE LAMP?");
        for (size_t i = 0; i < 2; i++)
            draw_button(r, &confirm_buttons[i], i == 0, true);
    }
}

void ui_draw(Ui *ui, SDL_Renderer *r, Link *l, const State *s)
{
    ui->frame++;
    art_update(&ui->art, r, l, s);
    SDL_SetRenderDrawColor(r, C_BG, 255);
    SDL_RenderClear(r);
    /* the road: a few horizon stripes, so the screen is never flat black */
    for (int i = 0; i < 6; i++)
        fill(r, 0, 16 + i * 27, UI_W, 1, 24 + i * 3, 20 + i * 3, 36 + i * 4);
    draw_top(r, ui, s);
    switch (ui->page) {
    case PAGE_DRIVE: draw_drive(r, ui, s); break;
    case PAGE_MEDIA: draw_media(r, ui, s); break;
    default: draw_dtc(r, ui, s); break;
    }
    draw_tabs(r, ui);
    if (!s->online) {
        fill(r, 60, 80, 200, 30, C_BG);
        box(r, 60, 80, 200, 30, C_RED);
        SDL_SetRenderDrawColor(r, C_RED, 255);
        font_draw_centered(r, 60, 200, 86, 1, "NO SIGNAL FROM OCTAVE");
        SDL_SetRenderDrawColor(r, C_DIM, 255);
        font_draw_centered(r, 60, 200, 96, 1, "RETRYING...");
    }
}

static void act(Ui *ui, Link *l, const State *s, int id)
{
    char args[32];
    switch (id) {
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
    int id = B_NONE;
    if (ui->confirm_clear) {
        id = hit(confirm_buttons, 2, x, y);
    } else {
        id = hit(tabs, 3, x, y);
        if (id == B_NONE && ui->page == PAGE_MEDIA) id = hit(media_buttons, sizeof media_buttons / sizeof media_buttons[0], x, y);
        if (id == B_NONE && ui->page == PAGE_DTC) id = hit(dtc_buttons, 2, x, y);
        if (id == B_NONE && ui->page == PAGE_DRIVE && !s->obd_connected) id = hit(drive_buttons, 1, x, y);
    }
    act(ui, l, s, id);
}

void ui_key(Ui *ui, Link *l, const State *s, SDL_Keycode key)
{
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
