/* The three pages on a small canvas scaled up whole: 320x200 when what
 * shows it is wider than tall, 200x320 when it is taller than wide. */
#ifndef ARCADE_UI_H
#define ARCADE_UI_H
#include <SDL.h>
#include "art.h"
#include "link.h"

/* the two canvases; each has its own layout in ui.c */
#define UI_WIDE_W 320
#define UI_WIDE_H 200
#define UI_TALL_W 200
#define UI_TALL_H 320

typedef enum { PAGE_DRIVE, PAGE_MEDIA, PAGE_DTC, PAGE_COUNT } Page;

typedef struct {
    Page page;
    bool tall;               /* the 200x320 layout; set with ui_set_tall() */
    bool confirm_clear;      /* the YES / NO overlay */
    int dtc_message_until;   /* ticks until the last DTC note fades */
    Uint32 frame;
    Art art;
    char code[8];            /* the pairing code being typed */
    char token_path[512];    /* where a new token is written, or empty */
    char token_note[64];     /* 'TOKEN SAVED' etc, shown after a pair */
} Ui;

/* Pick the layout. True when it changed: the caller then gives the
 * renderer the new canvas size. */
bool ui_set_tall(Ui *ui, bool tall);
void ui_size(const Ui *ui, int *w, int *h);
void ui_draw(Ui *ui, SDL_Renderer *r, Link *l, const State *s);
/* A tap or click at canvas coordinates. */
void ui_tap(Ui *ui, Link *l, const State *s, int x, int y);
void ui_key(Ui *ui, Link *l, const State *s, SDL_Keycode key);

#endif
