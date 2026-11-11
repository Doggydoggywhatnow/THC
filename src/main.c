/* THC: a lightweight multimedia player. Slice 1: one window, one file. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "mpv_dyn.h"
#include "video.h"

typedef struct App {
    SDL_Window *win;
    SDL_Renderer *ren;
    MpvApi mpv;
    bool have_mpv;
    VideoSession video;
    Uint32 mpv_event;
    char data_dir[1024];
    char mpv_dir[1024];
    char status[512];
} App;

static void play(App *a, const char *path)
{
    VideoOptions o = { a->mpv_dir, 0, false, a->mpv_event };
    if (!a->have_mpv) {
        SDL_snprintf(a->status, sizeof a->status, "libmpv was not found");
        return;
    }
    if (a->video.h) video_stop(&a->video);
    if (video_play(&a->video, &a->mpv, path, &o))
        SDL_snprintf(a->status, sizeof a->status, "Playing %s", path);
    else
        SDL_snprintf(a->status, sizeof a->status, "%s", a->video.error);
}

int main(int argc, char **argv)
{
    App a;
    SDL_zero(a);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    char *pref = SDL_GetPrefPath("THC", "THC");
    SDL_strlcpy(a.data_dir, pref ? pref : "./", sizeof a.data_dir);
    SDL_free(pref);
    SDL_snprintf(a.mpv_dir, sizeof a.mpv_dir, "%smpv", a.data_dir);
    SDL_CreateDirectory(a.mpv_dir);

    a.have_mpv = mpv_dyn_load(&a.mpv, NULL);
    a.mpv_event = SDL_RegisterEvents(1);
    SDL_snprintf(a.status, sizeof a.status,
                 a.have_mpv ? "Drop a video file here" : "libmpv was not found");

    if (!SDL_CreateWindowAndRenderer("THC", 960, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &a.win, &a.ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(a.ren, 1);
    if (argc > 1) play(&a, argv[1]);

    bool running = true;
    while (running) {
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, 250)) {
            do {
                if (ev.type == SDL_EVENT_QUIT) running = false;
                else if (ev.type == SDL_EVENT_DROP_FILE) play(&a, ev.drop.data);
            } while (SDL_PollEvent(&ev));
        }
        if (video_pump(&a.video) & VIDEO_EV_ENDED) {
            SDL_snprintf(a.status, sizeof a.status, "Finished at %.1f of %.1f s", a.video.pos,
                         a.video.duration);
            SDL_Log("%s", a.status);
        }

        SDL_SetRenderDrawColor(a.ren, 18, 18, 24, 255);
        SDL_RenderClear(a.ren);
        SDL_SetRenderDrawColor(a.ren, 240, 240, 240, 255);
        SDL_RenderDebugText(a.ren, 24, 24, "THC " THC_VERSION);
        SDL_RenderDebugText(a.ren, 24, 48, a.status);
        if (a.video.active) {
            char buf[128];
            SDL_snprintf(buf, sizeof buf, "%.1f / %.1f s %s", a.video.pos, a.video.duration,
                         a.video.paused ? "(paused)" : "");
            SDL_RenderDebugText(a.ren, 24, 72, buf);
        }
        SDL_RenderPresent(a.ren);
    }
    video_stop(&a.video);
    mpv_dyn_unload(&a.mpv);
    SDL_Quit();
    (void)argv;
    return 0;
}
