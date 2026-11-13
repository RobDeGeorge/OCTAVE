/* The native transport of the service-protocol client: one TCP connection
 * carrying JSON lines, read by its own thread, plus one short HTTP GET for
 * the cover. The protocol itself is link.c; the browser build has
 * link_web.c in this file's place. */
#include "link_impl.h"

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

void link_lock(Link *l) { SDL_LockMutex(l->t.lock); }
void link_unlock(Link *l) { SDL_UnlockMutex(l->t.lock); }

bool link_transport_send(Link *l, char *text, size_t n)
{
    if (l->t.fd < 0)
        return false;
    text[n] = '\n'; /* one message per line: the terminator's place is ours to overwrite, send n+1 */
    return write_all(l->t.fd, text, n + 1);
}

static int link_thread(void *arg)
{
    Link *l = arg;
    char buf[1 << 16];
    while (!SDL_AtomicGet(&l->t.stop)) {
        int fd = connect_to(l->host, l->port);
        if (fd < 0) {
            SDL_Delay(1000);
            continue;
        }
        link_lock(l);
        l->t.fd = fd;
        link_unlock(l);
        link_connected(l);
        size_t have = 0;
        while (!SDL_AtomicGet(&l->t.stop)) {
            ssize_t n = recv(fd, buf + have, sizeof buf - have - 1, 0);
            if (n <= 0)
                break;
            have += (size_t)n;
            buf[have] = 0;
            char *start = buf, *nl;
            while ((nl = memchr(start, '\n', have - (size_t)(start - buf))) != NULL) {
                *nl = 0;
                link_message(l, start);
                start = nl + 1;
            }
            have -= (size_t)(start - buf);
            memmove(buf, start, have);
            if (have == sizeof buf - 1)
                have = 0; /* a line longer than the buffer: drop it */
        }
        link_lock(l);
        close(l->t.fd);
        l->t.fd = -1;
        link_disconnected(l);
        link_unlock(l);
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
    l->t.fd = -1;
    l->next_id = 1;
    l->t.lock = SDL_CreateMutex();
    l->t.thread = SDL_CreateThread(link_thread, "octave-link", l);
    return l;
}

void link_stop(Link *l)
{
    SDL_AtomicSet(&l->t.stop, 1);
    link_lock(l);
    if (l->t.fd >= 0) {
        link_release_demand(l);   /* before the socket goes */
        shutdown(l->t.fd, SHUT_RDWR);
    }
    link_unlock(l);
    SDL_WaitThread(l->t.thread, NULL);
    SDL_DestroyMutex(l->t.lock);
    free(l);
}

unsigned char *link_fetch_art(Link *l, int revision, size_t *len, bool *pending)
{
    (void)revision;   /* GET /art is always the current cover */
    *len = 0;
    *pending = false; /* fetched here and now */
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
