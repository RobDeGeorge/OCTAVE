/* The cover, fetched from GET /art and crushed to chunky pixels. */
#ifndef ARCADE_ART_H
#define ARCADE_ART_H
#include <SDL.h>
#include "link.h"

#define ART_SIZE 24   /* the cover is ART_SIZE x ART_SIZE big pixels */

typedef struct {
    SDL_Texture *tex;     /* ART_SIZE x ART_SIZE, or NULL */
    int revision;         /* the artRevision it was made from */
    bool tried;
} Art;

/* Refetch when the revision changed. Cheap when nothing changed. */
void art_update(Art *a, SDL_Renderer *r, const Link *l, const State *s);
void art_free(Art *a);

#endif
