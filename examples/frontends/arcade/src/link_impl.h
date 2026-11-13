/* What link.c (the protocol) and its transport (link_tcp.c, or link_web.c
 * in a browser) share. Nothing else includes this. */
#ifndef ARCADE_LINK_IMPL_H
#define ARCADE_LINK_IMPL_H

#include <stddef.h>

#include "cJSON.h"
#include "link.h"

#define CONSUMER "arcade"   /* our name for obdManager.setParameterDemand */

#ifdef __EMSCRIPTEN__
typedef struct {
    bool open;            /* the WebSocket is open */
    int art_revision;     /* the cover being fetched, or fetched */
    int art_stage;        /* ART_IDLE ... ART_DONE in link_web.c */
    unsigned char *art;   /* the fetched bytes until art.c takes them */
    size_t art_len;
} Transport;
#else
typedef struct {
    int fd;
    SDL_mutex *lock;      /* guards the Link and writes to fd */
    SDL_Thread *thread;
    SDL_atomic_t stop;
} Transport;
#endif

enum { TICKET_NONE, TICKET_ASKED, TICKET_HELD };

struct Link {
    char host[64];
    int port;
    State st;
    int next_id;
    int id_hello, id_params, id_np, id_vol, id_obd, id_sig, id_shift, id_clock, id_dtcs, id_pair, id_ticket;
    char token[80];
    char ticket[80];      /* a media ticket for /art URLs (browsers) */
    int ticket_state;
    Transport t;
};

/* ── link.c, for the transport ─────────────────────────────────────────── */
/* The connection is up: say hello. Takes the lock. */
void link_connected(Link *l);
/* One JSON message from OCTAVE. Takes the lock. */
void link_message(Link *l, const char *text);
/* The rest are called with the lock held. */
void link_disconnected(Link *l);
void link_release_demand(Link *l);
void link_ask_ticket(Link *l);
int link_send(Link *l, cJSON *msg);

/* ── the transport, for link.c ─────────────────────────────────────────── */
void link_lock(Link *l);
void link_unlock(Link *l);
/* Send one message of `n` characters; text[n] (the terminator) may be
 * overwritten. False when there is no connection or it failed. */
bool link_transport_send(Link *l, char *text, size_t n);

#endif
