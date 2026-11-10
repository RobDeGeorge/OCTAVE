/* The service-protocol client: one TCP connection carrying JSON lines
 * (wiki/utilities.html#service-protocol, docs/api/). A reader thread folds
 * events into `State`; the UI reads it under the lock and sends calls. */
#ifndef ARCADE_LINK_H
#define ARCADE_LINK_H

#include <SDL.h>
#include <stdbool.h>

#define LINK_MAX_DTC 16
#define LINK_TEXT 128

typedef struct {
    char code[8];
} DtcCode;

typedef struct {
    bool online;             /* hello answered, subscriptions up */
    bool untrusted;          /* protocol 2: this client must pair before the service answers */
    char pair_error[LINK_TEXT];
    bool paired_now;         /* a pair just succeeded; the token is in link_token() */
    int protocol;
    char app_version[32];
    /* paramStore, native units */
    double speed, rpm, coolant, fuel, voltage;
    bool have_speed, have_rpm, have_coolant, have_fuel, have_voltage;
    bool obd_connected;
    char obd_state[32];
    /* nowPlaying */
    bool has_track, playing, shuffled, can_seek;
    char title[LINK_TEXT], artist[LINK_TEXT];
    long duration_ms, position_ms;
    Uint32 position_at;      /* SDL_GetTicks() when position_ms was reported */
    int art_revision;
    /* volumeController */
    int volume;
    bool muted;
    /* DTC */
    DtcCode dtc[LINK_MAX_DTC];
    int dtc_count;
    bool dtc_known;          /* a read has answered */
    bool mil;
    char dtc_message[LINK_TEXT];
    Uint32 dtc_message_at;
    bool shift_lit;
    char clock[16];
    bool has_imu;
    double lateral_g;
} State;

typedef struct Link Link;

/* `token`: a paired device's token (protocol 2) or NULL. */
Link *link_start(const char *host, int port, const char *token);
/* Pair with a code from the head unit's Settings; the result arrives as
 * `paired_now` (then link_token() has the token) or `pair_error`. */
void link_pair(Link *l, const char *code, const char *name);
const char *link_token(const Link *l);
void link_stop(Link *l);
/* Copy the state out under the lock. */
void link_snapshot(Link *l, State *out);
/* Send {"op":"call",...}; `args_json` is a JSON array literal or NULL. */
void link_call(Link *l, const char *object, const char *method, const char *args_json);
void link_set(Link *l, const char *object, const char *prop, const char *value_json);
/* Fetch GET /art over a second, short connection. Returns malloc'd bytes
 * (caller frees) and sets *len, or NULL when there is none. */
unsigned char *link_fetch_art(const Link *l, size_t *len);

#endif
