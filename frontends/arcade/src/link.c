#include "link.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cJSON.h"

#define PROTOCOL_VERSION 1
#define CONSUMER "arcade"

struct Link {
    char host[64];
    int port;
    int fd;
    SDL_mutex *lock;      /* guards state and writes to fd */
    SDL_Thread *thread;
    SDL_atomic_t stop;
    State st;
    int next_id;
    int sub_params, sub_np, sub_vol, sub_obd, sub_sig, sub_shift, sub_clock;
    int id_hello, id_params, id_np, id_vol, id_obd, id_sig, id_shift, id_clock, id_dtcs, id_pair;
    char token[80];
};

static int connect_to(const char *host, int port)
{
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0)
        return -1;
    int fd = -1;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        struct timeval tv = {2, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    return fd;
}

static bool write_all(int fd, const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, s, n, MSG_NOSIGNAL);
        if (w <= 0)
            return false;
        s += w;
        n -= (size_t)w;
    }
    return true;
}

/* Send a message, adding its id; returns the id or -1. Caller holds the lock. */
static int send_json(Link *l, cJSON *msg)
{
    if (l->fd < 0) {
        cJSON_Delete(msg);
        return -1;
    }
    int id = l->next_id++;
    cJSON_AddNumberToObject(msg, "id", id);
    char *text = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!text)
        return -1;
    size_t n = strlen(text);
    text[n] = '\n'; /* cJSON leaves room for the terminator; overwrite and send n+1 */
    bool ok = write_all(l->fd, text, n + 1);
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
    return send_json(l, m);
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

static void on_connected(Link *l);
static void subscribe_all(Link *l);

static void handle_line(Link *l, char *line)
{
    cJSON *msg = cJSON_Parse(line);
    if (!msg)
        return;
    SDL_LockMutex(l->lock);
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
            l->st.online = l->st.protocol == 1 || l->st.protocol == 2;
            const cJSON *auth = cJSON_GetObjectItemCaseSensitive(result, "auth");
            l->st.untrusted = cJSON_IsString(auth) && strcmp(auth->valuestring, "trusted") != 0;
            if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "tokenRejected")))
                l->token[0] = 0;   /* revoked or unknown: pair again */
            if (!l->st.untrusted)
                subscribe_all(l);
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
        } else if (id == l->id_dtcs && cJSON_IsTrue(ok) && cJSON_IsArray(result)) {
            set_dtcs(&l->st, result);
        } else if (!cJSON_IsTrue(ok)) {
            const cJSON *err = cJSON_GetObjectItemCaseSensitive(msg, "error");
            if (cJSON_IsString(err))
                fprintf(stderr, "octave: request %d failed: %s\n", id, err->valuestring);
        }
    }
    SDL_UnlockMutex(l->lock);
    cJSON_Delete(msg);
}

static void on_connected(Link *l)
{
    SDL_LockMutex(l->lock);
    cJSON *h = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "op", "hello");
    if (l->token[0])
        cJSON_AddStringToObject(h, "token", l->token);
    l->id_hello = send_json(l, h);
    SDL_UnlockMutex(l->lock);
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
    send_json(l, d);
    cJSON *g = cJSON_CreateObject();
    cJSON_AddStringToObject(g, "op", "call");
    cJSON_AddStringToObject(g, "object", "obdManager");
    cJSON_AddStringToObject(g, "method", "get_dtc_codes");
    l->id_dtcs = send_json(l, g);
}

static int link_thread(void *arg)
{
    Link *l = arg;
    char buf[1 << 16];
    while (!SDL_AtomicGet(&l->stop)) {
        int fd = connect_to(l->host, l->port);
        if (fd < 0) {
            SDL_Delay(1000);
            continue;
        }
        SDL_LockMutex(l->lock);
        l->fd = fd;
        SDL_UnlockMutex(l->lock);
        on_connected(l);
        size_t have = 0;
        while (!SDL_AtomicGet(&l->stop)) {
            ssize_t n = recv(fd, buf + have, sizeof buf - have - 1, 0);
            if (n <= 0)
                break;
            have += (size_t)n;
            buf[have] = 0;
            char *start = buf, *nl;
            while ((nl = memchr(start, '\n', have - (size_t)(start - buf))) != NULL) {
                *nl = 0;
                handle_line(l, start);
                start = nl + 1;
            }
            have -= (size_t)(start - buf);
            memmove(buf, start, have);
            if (have == sizeof buf - 1)
                have = 0; /* a line longer than the buffer: drop it */
        }
        SDL_LockMutex(l->lock);
        close(l->fd);
        l->fd = -1;
        State keep = l->st;
        memset(&l->st, 0, sizeof l->st);
        l->st.dtc_message_at = keep.dtc_message_at;
        SDL_UnlockMutex(l->lock);
        SDL_Delay(500);
    }
    return 0;
}

Link *link_start(const char *host, int port, const char *token)
{
    Link *l = calloc(1, sizeof *l);
    snprintf(l->host, sizeof l->host, "%s", host);
    if (token)
        snprintf(l->token, sizeof l->token, "%s", token);
    l->port = port;
    l->fd = -1;
    l->next_id = 1;
    l->lock = SDL_CreateMutex();
    l->thread = SDL_CreateThread(link_thread, "octave-link", l);
    return l;
}

void link_stop(Link *l)
{
    SDL_AtomicSet(&l->stop, 1);
    SDL_LockMutex(l->lock);
    if (l->fd >= 0) {
        /* release our polling demand before the socket goes */
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "op", "call");
        cJSON_AddStringToObject(d, "object", "obdManager");
        cJSON_AddStringToObject(d, "method", "setParameterDemand");
        cJSON_AddItemToObject(d, "args", cJSON_Parse("[\"" CONSUMER "\",[]]"));
        send_json(l, d);
        shutdown(l->fd, SHUT_RDWR);
    }
    SDL_UnlockMutex(l->lock);
    SDL_WaitThread(l->thread, NULL);
    SDL_DestroyMutex(l->lock);
    free(l);
}

void link_snapshot(Link *l, State *out)
{
    SDL_LockMutex(l->lock);
    *out = l->st;
    l->st.paired_now = false;   /* reported once */
    SDL_UnlockMutex(l->lock);
}

void link_pair(Link *l, const char *code, const char *name)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "pair");
    cJSON_AddStringToObject(m, "code", code);
    cJSON_AddStringToObject(m, "name", name);
    SDL_LockMutex(l->lock);
    l->st.pair_error[0] = 0;
    l->id_pair = send_json(l, m);
    SDL_UnlockMutex(l->lock);
}

const char *link_token(const Link *l) { return l->token; }

void link_call(Link *l, const char *object, const char *method, const char *args_json)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "call");
    cJSON_AddStringToObject(m, "object", object);
    cJSON_AddStringToObject(m, "method", method);
    cJSON_AddItemToObject(m, "args", args_json ? cJSON_Parse(args_json) : cJSON_CreateArray());
    SDL_LockMutex(l->lock);
    send_json(l, m);
    SDL_UnlockMutex(l->lock);
}

void link_set(Link *l, const char *object, const char *prop, const char *value_json)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "op", "set");
    cJSON_AddStringToObject(m, "object", object);
    cJSON_AddStringToObject(m, "property", prop);
    cJSON_AddItemToObject(m, "value", cJSON_Parse(value_json));
    SDL_LockMutex(l->lock);
    send_json(l, m);
    SDL_UnlockMutex(l->lock);
}

unsigned char *link_fetch_art(const Link *l, size_t *len)
{
    *len = 0;
    int fd = connect_to(l->host, l->port);
    if (fd < 0)
        return NULL;
    char req[384];
    int n = l->token[0]
        ? snprintf(req, sizeof req, "GET /art HTTP/1.0\r\nHost: %s:%d\r\nAuthorization: Bearer %s\r\nConnection: close\r\n\r\n", l->host, l->port, l->token)
        : snprintf(req, sizeof req, "GET /art HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", l->host, l->port);
    if (!write_all(fd, req, (size_t)n)) {
        close(fd);
        return NULL;
    }
    size_t cap = 1 << 16, have = 0;
    unsigned char *buf = malloc(cap);
    for (;;) {
        if (have == cap) {
            cap *= 2;
            unsigned char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                close(fd);
                return NULL;
            }
            buf = nb;
        }
        ssize_t r = recv(fd, buf + have, cap - have, 0);
        if (r <= 0)
            break;
        have += (size_t)r;
    }
    close(fd);
    /* status line, then headers up to the blank line */
    if (have < 12 || memcmp(buf, "HTTP/1.", 7) != 0 || memcmp(buf + 9, "200", 3) != 0) {
        free(buf);
        return NULL;
    }
    unsigned char *body = NULL;
    for (size_t i = 0; i + 3 < have; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            body = buf + i + 4;
            break;
        }
    }
    if (!body) {
        free(buf);
        return NULL;
    }
    *len = have - (size_t)(body - buf);
    memmove(buf, body, *len);
    return buf;
}
