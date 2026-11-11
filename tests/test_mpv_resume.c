/* test_mpv_resume.c: THC's history, not mpv's own resume, decides where playback starts.
 * Needs libmpv at run time; skips (and passes) when it is not installed. */
#include "mpv_dyn.h"
#include "video.h"
#include "test_util.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

#ifndef THC_TEST_DATA
#define THC_TEST_DATA "."
#endif

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static char *prop(const MpvApi *api, mpv_handle *h, const char *name)
{
    char *v = api->get_property_string(h, name);
    return v;
}

int main(void)
{
    MpvApi api;
    memset(&api, 0, sizeof api);
    if (!mpv_dyn_load(&api, NULL)) {
        printf("SKIP: libmpv not found\n");
        return 0;
    }
    /* a config folder whose mpv.conf tries to turn mpv's own resume back on and to point
     * the watch-later folder somewhere else */
    char dir[256], elsewhere[256], conf[300], wl[300];
    snprintf(dir, sizeof dir, "mpvtest_cfg");
    snprintf(elsewhere, sizeof elsewhere, "mpvtest_elsewhere");
    SDL_RemovePath("mpvtest_elsewhere/keep.txt");   /* leftovers from an earlier run */
    SDL_RemovePath("mpvtest_cfg/watch_later/ABCDEF");
    SDL_CreateDirectory(dir);
    SDL_CreateDirectory(elsewhere);
    snprintf(conf, sizeof conf, "%s/mpv.conf", dir);
    char text[512];
    snprintf(text, sizeof text, "ao=null\nresume-playback=yes\nsave-position-on-quit=yes\nwatch-later-directory=%s\n", elsewhere);
    write_file(conf, text);
    /* ...and a start= line, which must not win over the position THC passes */
    FILE *cf = fopen(conf, "ab");
    if (cf) { fputs("start=99\n", cf); fclose(cf); }

    VideoSession s;
    VideoOptions o = { dir, 0, true, 0 };
    CHECK(video_play(&s, &api, THC_TEST_DATA "/tone.mp3", &o));
    if (s.h) {
        char *v;
        v = prop(&api, s.h, "resume-playback");
        CHECK(v && !strcmp(v, "no"));
        api.free(v);
        v = prop(&api, s.h, "save-position-on-quit");
        CHECK(v && !strcmp(v, "no"));
        api.free(v);
        v = prop(&api, s.h, "watch-later-directory");
        CHECK(v && strstr(v, "mpvtest_cfg") && strstr(v, "watch_later") && !strstr(v, "elsewhere"));
        if (v) api.free(v);
        /* an explicit start of zero really starts at the beginning */
        v = prop(&api, s.h, "start");
        CHECK(v && (!strcmp(v, "0") || !strcmp(v, "00:00:00.000") || !strcmp(v, "0.000") || !strcmp(v, "00:00:00")));
        if (v) api.free(v);
        video_stop(&s);
    }
    /* quitting must not have written anything into the folder mpv.conf pointed to */
    int found = 0;
    char **g = SDL_GlobDirectory(elsewhere, "*", 0, &found);
    SDL_free(g);
    CHECK(found == 0);

    /* clearing history also clears mpv's own saved positions in THC's folder only */
    char wlfile[400];
    snprintf(wl, sizeof wl, "%s/watch_later", dir);
    SDL_CreateDirectory(wl);
    snprintf(wlfile, sizeof wlfile, "%s/ABCDEF", wl);
    write_file(wlfile, "start=12\n");
    char other[300];
    snprintf(other, sizeof other, "%s/keep.txt", elsewhere);
    write_file(other, "x");
    video_clear_watch_later(dir);
    SDL_PathInfo info;
    CHECK(!SDL_GetPathInfo(wlfile, &info));
    CHECK(SDL_GetPathInfo(other, &info));
    mpv_dyn_unload(&api);
    TEST_DONE();
}
