/* The three pages on a 320x200 canvas, scaled up whole. */
#ifndef ARCADE_UI_H
#define ARCADE_UI_H
#include <SDL.h>
#include "art.h"
#include "link.h"

#define UI_W 320
#define UI_H 200

typedef enum { PAGE_DRIVE, PAGE_MEDIA, PAGE_DTC, PAGE_COUNT } Page;

typedef struct {
    Page page;
    bool confirm_clear;      /* the YES / NO overlay */
    int dtc_message_until;   /* ticks until the last DTC note fades */
    Uint32 frame;
    Art art;
} Ui;

void ui_draw(Ui *ui, SDL_Renderer *r, Link *l, const State *s);
/* A tap or click at canvas coordinates. */
void ui_tap(Ui *ui, Link *l, const State *s, int x, int y);
void ui_key(Ui *ui, Link *l, const State *s, SDL_Keycode key);

#endif
