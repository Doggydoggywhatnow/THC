/* THC: a lightweight multimedia player. Slice 1: one window, one file. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "history.h"
#include "mpv_dyn.h"
#include "video.h"

typedef struct App {
    SDL_Window *win;
    SDL_Renderer *ren;
    MpvApi mpv;
    bool have_mpv;
    VideoSession video;
    History *history;
    char playing_key[1024];   /* history key of the file being played */
    Sint64 playing_size;
    HistoryEntry last;        /* history of the last played file */
    bool have_last;
    Uint32 mpv_event;
    char data_dir[1024];
    char mpv_dir[1024];
    char status[512];
} App;

static void save_progress(App *a)
{
    if (!a->history || !a->playing_key[0]) return;
    history_save(a->history, a->playing_key, a->playing_size, a->video.pos, a->video.duration,
                 HISTORY_DEFAULT_THRESHOLD, a->video.reached_eof);
    a->have_last = history_get(a->history, a->playing_key, a->playing_size, &a->last);
}

static void play(App *a, const char *path)
{
    VideoOptions o = { a->mpv_dir, 0, false, a->mpv_event };
    if (!a->have_mpv) {
        SDL_snprintf(a->status, sizeof a->status, "libmpv was not found");
        return;
    }
    if (a->video.h) {
        save_progress(a);
        video_stop(&a->video);
    }
    /* A file opened on its own is keyed by its file name. */
    const char *name = SDL_strrchr(path, '/');
#ifdef _WIN32
    const char *bs = SDL_strrchr(path, '\\');
    if (bs > name) name = bs;
#endif
    SDL_strlcpy(a->playing_key, name ? name + 1 : path, sizeof a->playing_key);
    SDL_PathInfo info;
    a->playing_size = SDL_GetPathInfo(path, &info) ? (Sint64)info.size : 0;
    a->have_last = history_get(a->history, a->playing_key, a->playing_size, &a->last);
    if (a->have_last) o.start = history_resume_point(&a->last, HISTORY_DEFAULT_THRESHOLD);
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

    char db_path[1100], err[256];
    SDL_snprintf(db_path, sizeof db_path, "%shistory.db", a.data_dir);
    a.history = history_open(db_path, err, sizeof err);
    if (!a.history) SDL_Log("history database: %s", err);

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
        int vf = video_pump(&a.video);
        if (vf & (VIDEO_EV_PROGRESS | VIDEO_EV_PAUSED | VIDEO_EV_ENDED)) save_progress(&a);
        if (vf & VIDEO_EV_ENDED) {
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
        if (a.have_last) {
            /* Playback bar and checkmark for the last file. */
            float w = 360, x = 24, y = 104;
            double f = history_progress(&a.last);
            SDL_FRect bg = { x, y, w, 6 }, fg = { x, y, (float)(w * f), 6 };
            SDL_SetRenderDrawColor(a.ren, 60, 60, 72, 255);
            SDL_RenderFillRect(a.ren, &bg);
            SDL_SetRenderDrawColor(a.ren, 255, 140, 66, 255);
            SDL_RenderFillRect(a.ren, &fg);
            if (a.last.watched) {
                SDL_SetRenderDrawColor(a.ren, 80, 220, 120, 255);
                for (int t = 0; t < 3; t++) {
                    SDL_RenderLine(a.ren, x + w + 12, y + 2 + t, x + w + 17, y + 7 + t);
                    SDL_RenderLine(a.ren, x + w + 17, y + 7 + t, x + w + 27, y - 4 + t);
                }
            }
        }
        SDL_RenderPresent(a.ren);
    }
    if (a.video.h) save_progress(&a);
    video_stop(&a.video);
    history_close(a.history);
    mpv_dyn_unload(&a.mpv);
    SDL_Quit();
    (void)argv;
    return 0;
}
