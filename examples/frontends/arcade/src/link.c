/* The service protocol itself (docs/api/): hello, pairing, the
 * subscriptions, and folding every event into `State`. How the JSON
 * messages travel is the transport's business: link_tcp.c (JSON lines over
 * a TCP socket, a reader thread) natively, link_web.c (the page's
 * WebSocket, one message per text frame) in a browser. */
#include "link_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_MIN 1   /* the service protocols this client knows (docs/api/README.md) */
#define PROTOCOL_MAX 3

/* Send a message, adding its id; returns the id or -1. Caller holds the lock. */
int link_send(Link *l, cJSON *msg)
{
    int id = l->next_id++;
    cJSON_AddNumberToObject(msg, "id", id);
    char *text = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!text)
        return -1;
    bool ok = link_transport_send(l, text, strlen(text));
    free(text);
    return ok ? id : -1;
}

static int subscribe(Link *l, const char *object, const char *props_json, const char *signals_json, int throttle)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "subscribe");
    cJSON_AddStringToObject(m, "object", object);
    if (props_json)
        cJSON_AddItemToObject(m, "properties", cJSON_Parse(props_json));
    if (signals_json)
        cJSON_AddItemToObject(m, "signals", cJSON_Parse(signals_json));
    if (throttle > 0)
        cJSON_AddNumberToObject(m, "throttleMs", throttle);
    return link_send(l, m);
}

static void copy_str(char *dst, size_t n, const cJSON *v)
{
    if (cJSON_IsString(v) && v->valuestring)
        snprintf(dst, n, "%s", v->valuestring);
    else
        dst[0] = 0;
}

static void apply_property(State *s, const char *object, const char *name, const cJSON *v)
{
    if (strcmp(object, "nowPlaying") == 0) {
        if (strcmp(name, "hasTrack") == 0) s->has_track = cJSON_IsTrue(v);
        else if (strcmp(name, "playing") == 0) {
            /* re-anchor so a pause doesn't jump */
            if (s->playing) s->position_ms += (long)(SDL_GetTicks() - s->position_at);
            s->position_at = SDL_GetTicks();
            s->playing = cJSON_IsTrue(v);
        }
        else if (strcmp(name, "shuffled") == 0) s->shuffled = cJSON_IsTrue(v);
        else if (strcmp(name, "canSeek") == 0) s->can_seek = cJSON_IsTrue(v);
        else if (strcmp(name, "title") == 0) copy_str(s->title, sizeof s->title, v);
        else if (strcmp(name, "artist") == 0) copy_str(s->artist, sizeof s->artist, v);
        else if (strcmp(name, "durationMs") == 0) s->duration_ms = (long)cJSON_GetNumberValue(v);
        else if (strcmp(name, "positionMs") == 0) { s->position_ms = (long)cJSON_GetNumberValue(v); s->position_at = SDL_GetTicks(); }
        else if (strcmp(name, "artRevision") == 0) s->art_revision = (int)cJSON_GetNumberValue(v);
    } else if (strcmp(object, "volumeController") == 0) {
        if (strcmp(name, "percent") == 0) s->volume = (int)cJSON_GetNumberValue(v);
        else if (strcmp(name, "muted") == 0) s->muted = cJSON_IsTrue(v);
    } else if (strcmp(object, "obdManager") == 0) {
        if (strcmp(name, "connected") == 0) s->obd_connected = cJSON_IsTrue(v);
        else if (strcmp(name, "connectionState") == 0) copy_str(s->obd_state, sizeof s->obd_state, v);
    } else if (strcmp(object, "shiftLightManager") == 0) {
        if (strcmp(name, "lit") == 0) s->shift_lit = cJSON_IsTrue(v);
    } else if (strcmp(object, "clock") == 0) {
        if (strcmp(name, "time") == 0) copy_str(s->clock, sizeof s->clock, v);
    } else if (strcmp(object, "berryIMU") == 0) {
        if (strcmp(name, "connected") == 0) s->has_imu = cJSON_IsTrue(v);
    }
}

static void apply_params(State *s, const cJSON *values)
{
    const cJSON *v;
    cJSON_ArrayForEach(v, values) {
        bool has = cJSON_IsNumber(v);
        double x = has ? v->valuedouble : 0;
        if (strcmp(v->string, "SPEED") == 0) { s->speed = x; s->have_speed = has; }
        else if (strcmp(v->string, "RPM") == 0) { s->rpm = x; s->have_rpm = has; }
        else if (strcmp(v->string, "COOLANT_TEMP") == 0) { s->coolant = x; s->have_coolant = has; }
        else if (strcmp(v->string, "FUEL_LEVEL") == 0) { s->fuel = x; s->have_fuel = has; }
        else if (strcmp(v->string, "CONTROL_MODULE_VOLTAGE") == 0) { s->voltage = x; s->have_voltage = has; }
        else if (strcmp(v->string, "LATERAL_G") == 0) { s->lateral_g = x; }
    }
}

static void set_dtcs(State *s, const cJSON *list)
{
    s->dtc_count = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, list) {
        if (s->dtc_count >= LINK_MAX_DTC)
            break;
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(it, "code");
        copy_str(s->dtc[s->dtc_count].code, sizeof s->dtc[0].code, code);
        s->dtc_count++;
    }
    s->dtc_known = true;
}

static void note(State *s, const char *msg)
{
    snprintf(s->dtc_message, sizeof s->dtc_message, "%s", msg);
    s->dtc_message_at = SDL_GetTicks();
}

static void apply_signal(State *s, const char *object, const char *name, const cJSON *args)
{
    if (strcmp(object, "obdManager") != 0)
        return;
    if (strcmp(name, "dtcCodesChanged") == 0) {
        set_dtcs(s, cJSON_GetArrayItem(args, 0));
        note(s, s->dtc_count ? "CODES READ" : "NO CODES STORED");
    } else if (strcmp(name, "milStatusChanged") == 0) {
        s->mil = cJSON_IsTrue(cJSON_GetArrayItem(args, 0));
    } else if (strcmp(name, "dtcClearResult") == 0) {
        bool ok = cJSON_IsTrue(cJSON_GetArrayItem(args, 0));
        note(s, ok ? "CODES CLEARED" : "CLEAR FAILED");
    }
}

static void subscribe_all(Link *l);

/* The connection went: forget what it said (caller holds the lock). */
void link_disconnected(Link *l)
{
    Uint32 keep = l->st.dtc_message_at;
    memset(&l->st, 0, sizeof l->st);
    l->st.dtc_message_at = keep;
    l->id_ticket = 0;
    l->ticket[0] = 0;
    l->ticket_state = TICKET_NONE;
}

/* Stop asking OCTAVE to poll the car for us (caller holds the lock). */
void link_release_demand(Link *l)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "op", "call");
    cJSON_AddStringToObject(d, "object", "obdManager");
    cJSON_AddStringToObject(d, "method", "setParameterDemand");
    cJSON_AddItemToObject(d, "args", cJSON_Parse("[\"" CONSUMER "\",[]]"));
    link_send(l, d);
}

/* A media ticket: what a browser puts in the /art URL in place of the
 * token (caller holds the lock). The answer lands in `ticket_state`. */
void link_ask_ticket(Link *l)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "mediaTicket");
    l->ticket[0] = 0;
    l->id_ticket = link_send(l, m);
    l->ticket_state = l->id_ticket > 0 ? TICKET_ASKED : TICKET_NONE;
    if (l->id_ticket < 0)
        l->id_ticket = 0;
}

void link_message(Link *l, const char *line)
{
    cJSON *msg = cJSON_Parse(line);
    if (!msg)
        return;
    link_lock(l);
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(msg, "event");
    if (cJSON_IsString(event)) {
        const char *ev = event->valuestring;
        const cJSON *object = cJSON_GetObjectItemCaseSensitive(msg, "object");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(msg, "name");
        const char *obj = cJSON_IsString(object) ? object->valuestring : "";
        const char *nm = cJSON_IsString(name) ? name->valuestring : "";
        if (strcmp(ev, "params") == 0)
            apply_params(&l->st, cJSON_GetObjectItemCaseSensitive(msg, "values"));
        else if (strcmp(ev, "property") == 0)
            apply_property(&l->st, obj, nm, cJSON_GetObjectItemCaseSensitive(msg, "value"));
        else if (strcmp(ev, "signal") == 0)
            apply_signal(&l->st, obj, nm, cJSON_GetObjectItemCaseSensitive(msg, "args"));
    } else {
        int id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(msg, "id"));
        const cJSON *ok = cJSON_GetObjectItemCaseSensitive(msg, "ok");
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(msg, "result");
        if (id == l->id_hello && cJSON_IsTrue(ok)) {
            l->st.protocol = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(result, "protocolVersion"));
            const cJSON *app = cJSON_GetObjectItemCaseSensitive(result, "app");
            copy_str(l->st.app_version, sizeof l->st.app_version, cJSON_GetObjectItemCaseSensitive(app, "version"));
            l->st.online = l->st.protocol >= PROTOCOL_MIN && l->st.protocol <= PROTOCOL_MAX;
            const cJSON *auth = cJSON_GetObjectItemCaseSensitive(result, "auth");
            l->st.untrusted = cJSON_IsString(auth) && strcmp(auth->valuestring, "trusted") != 0;
            if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "tokenRejected")))
                l->token[0] = 0;   /* revoked or unknown: pair again */
            if (!l->st.untrusted)
                subscribe_all(l);
#ifndef __EMSCRIPTEN__
            /* one line per connection: OCTAVE relays it into its log when the arcade is the screen */
            fprintf(stderr, "octave-arcade: connected to %s:%d, OCTAVE %s, protocol %d, %s, %s\n", l->host, l->port,
                    l->st.app_version, l->st.protocol, l->token[0] ? "with a token" : "no token",
                    l->st.untrusted ? "untrusted" : "trusted");
#endif
        } else if (id == l->id_pair) {
            if (cJSON_IsTrue(ok)) {
                copy_str(l->token, sizeof l->token, cJSON_GetObjectItemCaseSensitive(result, "token"));
                l->st.untrusted = false;
                l->st.paired_now = true;
                l->st.pair_error[0] = 0;
                subscribe_all(l);   /* trusted now: the subscriptions */
            } else {
                const cJSON *err = cJSON_GetObjectItemCaseSensitive(msg, "error");
                snprintf(l->st.pair_error, sizeof l->st.pair_error, "%s", cJSON_IsString(err) ? err->valuestring : "PAIRING FAILED");
            }
        } else if (id == l->id_ticket) {
            l->id_ticket = 0;
            copy_str(l->ticket, sizeof l->ticket, cJSON_IsTrue(ok) ? cJSON_GetObjectItemCaseSensitive(result, "ticket") : NULL);
            l->ticket_state = l->ticket[0] ? TICKET_HELD : TICKET_NONE;
        } else if (id == l->id_dtcs && cJSON_IsTrue(ok) && cJSON_IsArray(result)) {
            set_dtcs(&l->st, result);
        } else if (!cJSON_IsTrue(ok)) {
            const cJSON *err = cJSON_GetObjectItemCaseSensitive(msg, "error");
            if (cJSON_IsString(err))
                fprintf(stderr, "octave: request %d failed: %s\n", id, err->valuestring);
        }
    }
    link_unlock(l);
    cJSON_Delete(msg);
}

void link_connected(Link *l)
{
    link_lock(l);
    cJSON *h = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "op", "hello");
    if (l->token[0])
        cJSON_AddStringToObject(h, "token", l->token);
    l->id_hello = link_send(l, h);
    link_unlock(l);
}

/* Everything the pages show; sent once hello says we may (caller holds the lock). */
static void subscribe_all(Link *l)
{
    l->id_params = subscribe(l, "paramStore", "[\"SPEED\",\"RPM\",\"COOLANT_TEMP\",\"FUEL_LEVEL\",\"CONTROL_MODULE_VOLTAGE\",\"LATERAL_G\"]", NULL, 50);
    l->id_np = subscribe(l, "nowPlaying", "[\"hasTrack\",\"playing\",\"shuffled\",\"canSeek\",\"title\",\"artist\",\"durationMs\",\"positionMs\",\"artRevision\"]", NULL, 250);
    l->id_vol = subscribe(l, "volumeController", "[\"percent\",\"muted\"]", NULL, 0);
    l->id_obd = subscribe(l, "obdManager", "[\"connected\",\"connectionState\"]", NULL, 0);
    l->id_sig = subscribe(l, "obdManager", NULL, "[\"dtcCodesChanged\",\"milStatusChanged\",\"dtcClearResult\"]", 0);
    l->id_shift = subscribe(l, "shiftLightManager", "[\"lit\"]", NULL, 0);
    l->id_clock = subscribe(l, "clock", "[\"time\"]", NULL, 0);
    subscribe(l, "berryIMU", "[\"connected\"]", NULL, 0);
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "op", "call");
    cJSON_AddStringToObject(d, "object", "obdManager");
    cJSON_AddStringToObject(d, "method", "setParameterDemand");
    cJSON_AddItemToObject(d, "args", cJSON_Parse("[\"" CONSUMER "\",[\"SPEED\",\"RPM\",\"COOLANT_TEMP\",\"FUEL_LEVEL\",\"CONTROL_MODULE_VOLTAGE\"]]"));
    link_send(l, d);
    cJSON *g = cJSON_CreateObject();
    cJSON_AddStringToObject(g, "op", "call");
    cJSON_AddStringToObject(g, "object", "obdManager");
    cJSON_AddStringToObject(g, "method", "get_dtc_codes");
    l->id_dtcs = link_send(l, g);
}

void link_snapshot(Link *l, State *out)
{
    link_lock(l);
    *out = l->st;
    l->st.paired_now = false;   /* reported once */
    link_unlock(l);
}

void link_pair(Link *l, const char *code, const char *name)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "pair");
    cJSON_AddStringToObject(m, "code", code);
    cJSON_AddStringToObject(m, "name", name);
    /* what the manifest asks for and no more; without `scopes` a device gets full access */
    cJSON_AddItemToObject(m, "scopes", cJSON_Parse("[\"media\",\"vehicle\"]"));
    link_lock(l);
    l->st.pair_error[0] = 0;
    l->id_pair = link_send(l, m);
    link_unlock(l);
}

const char *link_token(const Link *l) { return l->token; }

void link_call(Link *l, const char *object, const char *method, const char *args_json)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "call");
    cJSON_AddStringToObject(m, "object", object);
    cJSON_AddStringToObject(m, "method", method);
    cJSON_AddItemToObject(m, "args", args_json ? cJSON_Parse(args_json) : cJSON_CreateArray());
    link_lock(l);
    link_send(l, m);
    link_unlock(l);
}

void link_set(Link *l, const char *object, const char *prop, const char *value_json)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "set");
    cJSON_AddStringToObject(m, "object", object);
    cJSON_AddStringToObject(m, "property", prop);
    cJSON_AddItemToObject(m, "value", cJSON_Parse(value_json));
    link_lock(l);
    link_send(l, m);
    link_unlock(l);
}

