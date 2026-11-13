/* OCTAVE Arcade: a C + SDL2 frontend over the service API, drawn like an
 * 8-bit arcade dashboard on a small canvas scaled up whole: 320x200, or
 * 200x320 when the window or page is taller than wide. */
#include <SDL.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "link.h"
#include "ui.h"

typedef struct {
    SDL_Window *win;
    SDL_Renderer *r;
    Link *link;
    Ui ui;
    const char *token_file;
    int fullscreen;
} App;

#ifdef __EMSCRIPTEN__
/* The pairing token lives in the page's localStorage. Where the page has
 * no storage (a private window, the OCTAVE demo's sandbox) the calls throw
 * and it is simply not kept. */
EM_JS_DEPS(arcade_main, "$UTF8ToString,$stringToNewUTF8")
EM_JS(char *, js_token_load, (void), {
    try {
        var t = localStorage.getItem('octave.token');
        return t ? stringToNewUTF8(t) : 0;
    } catch (e) {
        return 0;
    }
})
EM_JS(int, js_token_save, (const char *token), {
    try {
        localStorage.setItem('octave.token', UTF8ToString(token));
        return 1;
    } catch (e) {
        return 0;
    }
})
/* The page's shape and its side of a canvas change (web/pre.js), and the
 * page named in the address (?page=media). */
EM_JS(int, js_page_tall, (void), {
    return Module['arcadePageTall'] && Module['arcadePageTall']() ? 1 : 0;
})
EM_JS(void, js_canvas_changed, (void), {
    if (Module['arcadeFit']) Module['arcadeFit']();
})
EM_JS(int, js_query_page, (void), {
    try {
        var p = new URLSearchParams(location.search).get('page');
        return p === 'media' ? 1 : p === 'dtc' ? 2 : 0;
    } catch (e) {
        return 0;
    }
})
#endif

/* The canvas takes the shape of what shows it: tall when that is taller
 * than wide. Asked every frame, so turning the phone or resizing the
 * window swaps the layout at once. */
static void fit(App *a)
{
    int w = 0, h = 0;
#ifdef __EMSCRIPTEN__
    bool changed = ui_set_tall(&a->ui, js_page_tall());
#else
    if (SDL_GetRendererOutputSize(a->r, &w, &h) != 0 || w <= 0 || h <= 0)
        return;   /* minimised: keep the shape it had */
    bool changed = ui_set_tall(&a->ui, h > w);
#endif
    if (!changed)
        return;
    ui_size(&a->ui, &w, &h);
#ifdef __EMSCRIPTEN__
    /* the window is the canvas, one to one; the page then scales it */
    SDL_SetWindowSize(a->win, w, h);
    js_canvas_changed();
#endif
    SDL_RenderSetLogicalSize(a->r, w, h);
}

/* A new token: keep it for the next run. */
static void keep_token(App *a)
{
#ifdef __EMSCRIPTEN__
    snprintf(a->ui.token_note, sizeof a->ui.token_note, "%s",
             js_token_save(link_token(a->link)) ? "PAIRED, TOKEN SAVED" : "PAIRED UNTIL THE PAGE CLOSES");
#else
    if (a->token_file) {
        /* the token is a credential: owner-only, whether the file is new or not */
        int fd = open(a->token_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
        if (f) {
            fchmod(fd, 0600);
            fputs(link_token(a->link), f); fputc('\n', f); fclose(f);
            snprintf(a->ui.token_note, sizeof a->ui.token_note, "PAIRED, TOKEN SAVED");
        } else {
            if (fd >= 0) close(fd);
            snprintf(a->ui.token_note, sizeof a->ui.token_note, "PAIRED, CANNOT SAVE TOKEN");
        }
    } else {
        printf("paired; token: %s\n", link_token(a->link));
        snprintf(a->ui.token_note, sizeof a->ui.token_note, "PAIRED, TOKEN ON STDOUT");
    }
#endif
}

/* One turn of the loop: events, then the picture. False to quit. */
static bool frame(App *a)
{
    bool running = true;
    State s;
    SDL_Event e;
    fit(a);
    link_snapshot(a->link, &s);
    if (s.paired_now)
        keep_token(a);
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_MOUSEBUTTONDOWN:
            /* a finger also arrives as a made-up mouse: it is handled below, once */
            if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID)
                ui_tap(&a->ui, a->link, &s, e.button.x, e.button.y);
            break;
        case SDL_FINGERDOWN: {
            int w, h;
            SDL_GetWindowSize(a->win, &w, &h);
            /* finger coords are normalised to the window; map through the logical scale */
            float lx, ly;
            SDL_RenderWindowToLogical(a->r, (int)(e.tfinger.x * (float)w), (int)(e.tfinger.y * (float)h), &lx, &ly);
            ui_tap(&a->ui, a->link, &s, (int)lx, (int)ly);
            break;
        }
        case SDL_KEYDOWN:
#ifndef __EMSCRIPTEN__   /* a page has no quit, and its fullscreen is the browser's */
            if (e.key.keysym.sym == SDLK_q && (e.key.keysym.mod & KMOD_CTRL)) running = false;
            else if (e.key.keysym.sym == SDLK_F11) {
                a->fullscreen = !a->fullscreen;
                SDL_SetWindowFullscreen(a->win, a->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            } else
#endif
            ui_key(&a->ui, a->link, &s, e.key.keysym.sym);
            break;
        default: break;
        }
    }
    ui_draw(&a->ui, a->r, a->link, &s);
    SDL_RenderPresent(a->r);
    return running;
}

#ifdef __EMSCRIPTEN__
static void web_frame(void *arg) { frame(arg); }
#endif

static void usage(void)
{
    puts("octave-arcade [--host H] [--port P] [--fullscreen] [--page drive|media|dtc] [--scale N] [--portrait]\n"
         "              [--token T | OCTAVE_TOKEN=T] [--token-file PATH]   (pairing, protocol 2, over the network)\n"
         "OCTAVE_HOST, OCTAVE_PORT and OCTAVE_TOKEN (set when OCTAVE runs it as the screen) are the defaults");
}

int main(int argc, char **argv)
{
    /* OCTAVE sets these three when it runs the arcade as the screen; the flags win */
    const char *host = getenv("OCTAVE_HOST");
    if (!host || !host[0]) host = "127.0.0.1";
    const char *token = getenv("OCTAVE_TOKEN");
    if (token && !token[0]) token = NULL;
    const char *token_file = NULL;
    char token_buf[80] = {0};
    const char *env_port = getenv("OCTAVE_PORT");
    int port = env_port && atoi(env_port) > 0 ? atoi(env_port) : 9876, fullscreen = 0, scale = 4, portrait = 0;
    Page page = PAGE_DRIVE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fullscreen") == 0) fullscreen = 1;
        else if (strcmp(argv[i], "--portrait") == 0) portrait = 1;
        else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) token = argv[++i];
        else if (strcmp(argv[i], "--token-file") == 0 && i + 1 < argc) token_file = argv[++i];
        else if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
            const char *p = argv[++i];
            page = strcmp(p, "media") == 0 ? PAGE_MEDIA : strcmp(p, "dtc") == 0 ? PAGE_DTC : PAGE_DRIVE;
        } else {
            usage();
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }
    if (scale < 1) scale = 1;
    if (!token && token_file) {
        FILE *f = fopen(token_file, "r");
        if (f) {
            if (fgets(token_buf, sizeof token_buf, f)) {
                token_buf[strcspn(token_buf, "\r\n")] = 0;
                if (token_buf[0]) token = token_buf;
            }
            fclose(f);
        }
    }

#ifdef __EMSCRIPTEN__
    /* a page: no flags or environment. The canvas is 320x200 or 200x320, as the page is
     * shaped, and the page scales it (web/pre.js) */
    scale = 1;
    fullscreen = 0;
    portrait = js_page_tall();
    page = (Page)js_query_page();
    token_file = NULL;
    char *stored = js_token_load();
    if (stored) {
        snprintf(token_buf, sizeof token_buf, "%s", stored);
        free(stored);
        token = token_buf;
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    static App app;   /* static: in a browser main() is left while the loop goes on */
    Uint32 window_flags = SDL_WINDOW_SHOWN | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#ifndef __EMSCRIPTEN__
    window_flags |= SDL_WINDOW_RESIZABLE;
#endif
    int w, h;
    ui_set_tall(&app.ui, portrait);
    ui_size(&app.ui, &w, &h);
    app.win = SDL_CreateWindow("OCTAVE Arcade", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               w * scale, h * scale, window_flags);
    if (!app.win) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
#ifdef __EMSCRIPTEN__
    /* flat rectangles on a 2D canvas: no WebGL context for a phone to take away in the background */
    app.r = SDL_CreateRenderer(app.win, -1, SDL_RENDERER_SOFTWARE);
#else
    app.r = SDL_CreateRenderer(app.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#endif
    if (!app.r)
        app.r = SDL_CreateRenderer(app.win, -1, 0);
    if (!app.r) {
        fprintf(stderr, "SDL renderer: %s\n", SDL_GetError());
        return 1;
    }
#ifdef __EMSCRIPTEN__
    js_canvas_changed();
#endif
    SDL_RenderSetLogicalSize(app.r, w, h);
    SDL_RenderSetIntegerScale(app.r, SDL_TRUE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    app.link = link_start(host, port, token);
    app.ui.page = page;
    app.token_file = token_file;
    app.fullscreen = fullscreen;
    if (token_file) snprintf(app.ui.token_path, sizeof app.ui.token_path, "%s", token_file);
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(web_frame, &app, 0, 1);   /* the browser's frame clock; does not return */
#else
    while (frame(&app))
        SDL_Delay(16);
#endif
    art_free(&app.ui.art);
    link_stop(app.link);
    SDL_DestroyRenderer(app.r);
    SDL_DestroyWindow(app.win);
    SDL_Quit();
    return 0;
}
