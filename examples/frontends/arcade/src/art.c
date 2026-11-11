#include "art.h"

#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

/* The 16-colour arcade palette the cover is snapped to. */
static const Uint8 palette[][3] = {
    {12, 10, 20},   {40, 36, 64},    {90, 60, 120},   {160, 70, 150},
    {240, 80, 140}, {255, 140, 60},  {255, 210, 70},  {240, 240, 200},
    {60, 190, 220}, {30, 110, 190},  {40, 60, 130},   {60, 200, 120},
    {20, 120, 80},  {200, 60, 60},   {120, 120, 130}, {230, 230, 240},
};

static int nearest(int r, int g, int b)
{
    int best = 0;
    long bd = 1L << 40;
    for (size_t i = 0; i < sizeof palette / sizeof palette[0]; i++) {
        long dr = r - palette[i][0], dg = g - palette[i][1], db = b - palette[i][2];
        long d = dr * dr * 2 + dg * dg * 4 + db * db * 3;
        if (d < bd) {
            bd = d;
            best = (int)i;
        }
    }
    return best;
}

void art_update(Art *a, SDL_Renderer *r, const Link *l, const State *s)
{
    if (!s->has_track || s->art_revision <= 0) {
        if (a->tex) {
            SDL_DestroyTexture(a->tex);
            a->tex = NULL;
        }
        a->revision = 0;
        a->tried = false;
        return;
    }
    if (a->revision == s->art_revision && (a->tex || a->tried))
        return;
    a->revision = s->art_revision;
    a->tried = true;
    size_t len = 0;
    unsigned char *bytes = link_fetch_art(l, &len);
    if (!bytes)
        return;
    int w = 0, h = 0, comp = 0;
    unsigned char *px = stbi_load_from_memory(bytes, (int)len, &w, &h, &comp, 3);
    free(bytes);
    if (!px)
        return;
    /* box-filter to ART_SIZE cells, then snap each cell to the palette */
    Uint32 *out = malloc(sizeof(Uint32) * ART_SIZE * ART_SIZE);
    for (int cy = 0; cy < ART_SIZE; cy++) {
        for (int cx = 0; cx < ART_SIZE; cx++) {
            int x0 = cx * w / ART_SIZE, x1 = (cx + 1) * w / ART_SIZE;
            int y0 = cy * h / ART_SIZE, y1 = (cy + 1) * h / ART_SIZE;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            long sr = 0, sg = 0, sb = 0, n = 0;
            for (int y = y0; y < y1 && y < h; y++)
                for (int x = x0; x < x1 && x < w; x++) {
                    const unsigned char *p = px + (y * w + x) * 3;
                    sr += p[0]; sg += p[1]; sb += p[2]; n++;
                }
            if (n == 0) n = 1;
            int k = nearest((int)(sr / n), (int)(sg / n), (int)(sb / n));
            out[cy * ART_SIZE + cx] = 0xFF000000u | ((Uint32)palette[k][0] << 16) | ((Uint32)palette[k][1] << 8) | palette[k][2];
        }
    }
    stbi_image_free(px);
    if (a->tex)
        SDL_DestroyTexture(a->tex);
    a->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ART_SIZE, ART_SIZE);
    if (a->tex) {
        SDL_UpdateTexture(a->tex, NULL, out, ART_SIZE * (int)sizeof(Uint32));
        SDL_SetTextureScaleMode(a->tex, SDL_ScaleModeNearest);
    }
    free(out);
}

void art_free(Art *a)
{
    if (a->tex)
        SDL_DestroyTexture(a->tex);
    a->tex = NULL;
}
