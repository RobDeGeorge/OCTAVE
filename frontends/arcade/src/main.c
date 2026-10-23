/* OCTAVE Arcade: a C + SDL2 frontend over the service API, drawn like an
 * 8-bit arcade dashboard on a 320x200 canvas scaled up whole. */
#include <SDL.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "ui.h"

static void usage(void)
{
    puts("octave-arcade [--host H] [--port P] [--fullscreen] [--page drive|media|dtc] [--scale N]\n"
         "              [--token T | OCTAVE_TOKEN=T] [--token-file PATH]   (pairing, protocol 2, over the network)");
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    const char *token = getenv("OCTAVE_TOKEN");
    const char *token_file = NULL;
    char token_buf[80] = {0};
    int port = 9876, fullscreen = 0, scale = 4;
    Page page = PAGE_DRIVE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fullscreen") == 0) fullscreen = 1;
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("OCTAVE Arcade", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       UI_W * scale, UI_H * scale,
                                       SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r)
        r = SDL_CreateRenderer(win, -1, 0);
    if (!r) {
        fprintf(stderr, "SDL renderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(r, UI_W, UI_H);
    SDL_RenderSetIntegerScale(r, SDL_TRUE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    Link *link = link_start(host, port, token);
    Ui ui = {0};
    ui.page = page;
    if (token_file) snprintf(ui.token_path, sizeof ui.token_path, "%s", token_file);
    State s;
    int running = 1;
    while (running) {
        SDL_Event e;
        link_snapshot(link, &s);
        if (s.paired_now) {
            /* a new token: keep it for the next run */
            if (token_file) {
                /* the token is a credential: owner-only, whether the file is new or not */
                int fd = open(token_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
                if (f) {
                    fchmod(fd, 0600);
                    fputs(link_token(link), f); fputc('\n', f); fclose(f);
                    snprintf(ui.token_note, sizeof ui.token_note, "PAIRED, TOKEN SAVED");
                } else {
                    if (fd >= 0) close(fd);
                    snprintf(ui.token_note, sizeof ui.token_note, "PAIRED, CANNOT SAVE TOKEN");
                }
            } else {
                printf("paired; token: %s\n", link_token(link));
                snprintf(ui.token_note, sizeof ui.token_note, "PAIRED, TOKEN ON STDOUT");
            }
        }
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT)
                    ui_tap(&ui, link, &s, e.button.x, e.button.y);
                break;
            case SDL_FINGERDOWN: {
                int w, h;
                SDL_GetWindowSize(win, &w, &h);
                /* finger coords are normalised to the window; map through the logical scale */
                float lx, ly;
                SDL_RenderWindowToLogical(r, (int)(e.tfinger.x * (float)w), (int)(e.tfinger.y * (float)h), &lx, &ly);
                ui_tap(&ui, link, &s, (int)lx, (int)ly);
                break;
            }
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_q && (e.key.keysym.mod & KMOD_CTRL)) running = 0;
                else if (e.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                } else ui_key(&ui, link, &s, e.key.keysym.sym);
                break;
            default: break;
            }
        }
        ui_draw(&ui, r, link, &s);
        SDL_RenderPresent(r);
        SDL_Delay(16);
    }
    art_free(&ui.art);
    link_stop(link);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
