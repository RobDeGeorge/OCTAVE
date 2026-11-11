/* A 5x5 pixel font drawn from rectangles: no font files. */
#ifndef ARCADE_FONT_H
#define ARCADE_FONT_H
#include <SDL.h>

/* Width in pixels of `text` at `scale` (glyph 5 + gap 1). */
int font_width(const char *text, int scale);
/* Draw `text` with its top-left at (x, y). The current render colour is used. */
void font_draw(SDL_Renderer *r, int x, int y, int scale, const char *text);
/* Centred within [x, x + w). */
void font_draw_centered(SDL_Renderer *r, int x, int w, int y, int scale, const char *text);
/* Right-aligned so the text ends at x_right. */
void font_draw_right(SDL_Renderer *r, int x_right, int y, int scale, const char *text);

#endif
