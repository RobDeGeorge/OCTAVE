/* The browser transport of the service-protocol client: the page's own
 * WebSocket, one JSON message per text frame, to the host that served the
 * page (as templates/hello-car-js does). Event-driven, no thread: the
 * socket's callbacks run between frames. It asks for `WebSocket` by its
 * global name on purpose: inside the OCTAVE browser demo the page gets a
 * stand-in under that name (packaging/wasm/frontend-host.js), which ignores
 * the URL and carries text frames only. The protocol itself is link.c. */
#include "link_impl.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ART_IDLE, ART_TICKET, ART_FETCHING, ART_DONE };

EM_JS_DEPS(arcade_link, "$UTF8ToString,$stringToNewUTF8,$HEAPU8")

/* Open the socket `delay_ms` from now; its events come back as link_web_*(). */
EM_JS(void, js_socket_open, (Link *l, int delay_ms), {
    var A = Module.arcadeLink || (Module.arcadeLink = { ws: null, timer: 0 });
    if (A.timer) clearTimeout(A.timer);
    A.timer = setTimeout(function () {
        A.timer = 0;
        var ws;
        try {
            /* about:srcdoc (the demo's sandbox) has no host: any valid URL does, the stand-in ignores it */
            ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + (location.host || 'localhost') + '/');
        } catch (e) {
            _link_web_closed(l);
            return;
        }
        A.ws = ws;
        ws.onopen = function () { if (A.ws === ws) _link_web_opened(l); };
        ws.onmessage = function (m) {
            if (A.ws !== ws || typeof m.data !== 'string') return;
            var text = stringToNewUTF8(m.data);
            _link_web_message(l, text);
            _free(text);
        };
        ws.onclose = function () {
            if (A.ws !== ws) return;
            A.ws = null;
            _link_web_closed(l);
        };
    }, delay_ms);
})

EM_JS(int, js_socket_send, (const char *text), {
    var A = Module.arcadeLink;
    if (!A || !A.ws || A.ws.readyState !== 1) return 0;
    try {
        A.ws.send(UTF8ToString(text));
        return 1;
    } catch (e) {
        return 0;
    }
})

EM_JS(void, js_socket_close, (void), {
    var A = Module.arcadeLink;
    if (!A) return;
    if (A.timer) clearTimeout(A.timer);
    A.timer = 0;
    var ws = A.ws;
    A.ws = null;
    if (ws) { try { ws.close(); } catch (e) {} }
})

/* GET /art from the host that served the page; the answer is
 * link_web_art(). False where there is no such host (the demo's sandbox:
 * an opaque origin with nothing to fetch from). */
EM_JS(int, js_art_fetch, (Link *l, int revision, const char *ticket), {
    if (location.protocol !== 'http:' && location.protocol !== 'https:') return 0;
    var t = UTF8ToString(ticket);
    var url = location.origin + '/art?rev=' + revision + (t ? '&ticket=' + encodeURIComponent(t) : "");
    fetch(url, { credentials: 'omit', cache: 'no-store' })
        .then(function (r) { return r.ok ? r.arrayBuffer() : null; })
        .catch(function () { return null; })
        .then(function (buf) {
            var n = buf ? buf.byteLength : 0, p = 0;
            if (n) {
                p = _malloc(n);
                if (p) HEAPU8.set(new Uint8Array(buf), p);
            }
            _link_web_art(l, revision, p, p ? n : 0);
        });
    return 1;
})

void link_lock(Link *l) { (void)l; }      /* one thread: nothing to guard */
void link_unlock(Link *l) { (void)l; }

bool link_transport_send(Link *l, char *text, size_t n)
{
    (void)n;
    return l->t.open && js_socket_send(text);
}

EMSCRIPTEN_KEEPALIVE void link_web_opened(Link *l)
{
    l->t.open = true;
    link_connected(l);
}

EMSCRIPTEN_KEEPALIVE void link_web_message(Link *l, const char *text)
{
    link_message(l, text);
}

EMSCRIPTEN_KEEPALIVE void link_web_closed(Link *l)
{
    l->t.open = false;
    link_disconnected(l);
    if (l->t.art_stage == ART_TICKET)
        l->t.art_stage = ART_DONE;   /* its ticket will not come: no cover this time */
    js_socket_open(l, 1000);
}

/* Takes ownership of `bytes` (malloc'd, or NULL for no cover). */
EMSCRIPTEN_KEEPALIVE void link_web_art(Link *l, int revision, unsigned char *bytes, size_t len)
{
    if (revision != l->t.art_revision || l->t.art_stage != ART_FETCHING) {
        free(bytes);   /* the track moved on meanwhile */
        return;
    }
    l->t.art = bytes;
    l->t.art_len = len;
    l->t.art_stage = ART_DONE;
}

Link *link_start(const char *host, int port, const char *token)
{
    Link *l = calloc(1, sizeof *l);
    snprintf(l->host, sizeof l->host, "%s", host ? host : "");
    if (token)
        snprintf(l->token, sizeof l->token, "%s", token);
    l->port = port;
    l->next_id = 1;
    js_socket_open(l, 0);
    return l;
}

void link_stop(Link *l)
{
    if (l->t.open)
        link_release_demand(l);
    js_socket_close();
    free(l->t.art);
    free(l);
}

unsigned char *link_fetch_art(Link *l, int revision, size_t *len, bool *pending)
{
    *len = 0;
    *pending = true;
    if (l->t.art_stage == ART_IDLE || l->t.art_revision != revision) {
        /* a new cover: a media ticket first (the token never goes in a URL), then the fetch */
        free(l->t.art);
        l->t.art = NULL;
        l->t.art_revision = revision;
        l->t.art_stage = ART_TICKET;
        link_ask_ticket(l);
    }
    if (l->t.art_stage == ART_TICKET && l->ticket_state != TICKET_ASKED) {
        /* with the ticket, or without when OCTAVE has none to give (protocol 1 needs none) */
        l->t.art_stage = js_art_fetch(l, revision, l->ticket) ? ART_FETCHING : ART_DONE;
    }
    if (l->t.art_stage != ART_DONE)
        return NULL;
    *pending = false;
    unsigned char *bytes = l->t.art;
    *len = l->t.art_len;
    l->t.art = NULL;
    l->t.art_len = 0;
    return bytes;   /* asked again for this revision: NULL, there is none */
}
