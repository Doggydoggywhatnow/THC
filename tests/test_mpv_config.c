/* test_mpv_config.c: does editing THC's own mpv.conf and input.conf really change the
 * mpv that THC runs? Plays a tiny file audio-only (no window) through video_play, the same
 * code path as real playback, and reads the settings back from libmpv.
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

static MpvApi api;

/* Start a session on `dir`, run `check`, stop. */
typedef void (*CheckFn)(VideoSession *s);

static void session(const char *dir, CheckFn check)
{
    VideoSession s;
    VideoOptions o = { dir, 0, true, 0 };
    CHECK(video_play(&s, &api, THC_TEST_DATA "/tone.mp3", &o));
    if (s.h) {
        check(&s);
        video_stop(&s);
    }
}

static double num(VideoSession *s, const char *name)
{
    double d = -1;
    api.get_property(s->h, name, MPV_FORMAT_DOUBLE, &d);
    return d;
}

static bool str_is(VideoSession *s, const char *name, const char *want)
{
    char *v = api.get_property_string(s->h, name);
    bool ok = v && !strcmp(v, want);
    if (!ok) fprintf(stderr, "%s = \"%s\", want \"%s\"\n", name, v ? v : "(null)", want);
    if (v) api.free(v);
    return ok;
}

static bool binding_has(VideoSession *s, const char *key, const char *cmd)
{
    char *v = api.get_property_string(s->h, "input-bindings");
    bool ok = v && strstr(v, key) && (!cmd || strstr(v, cmd));
    if (v) api.free(v);
    return ok;
}

static void check_empty(VideoSession *s)
{
    /* no mpv.conf, no input.conf: mpv's and THC's defaults */
    CHECK_NEAR(num(s, "volume"), 100);
    CHECK(str_is(s, "keep-open", "no"));
    CHECK(!binding_has(s, "F9", "thc-test-one"));
}

static void check_first(VideoSession *s)
{
    CHECK_NEAR(num(s, "volume"), 37);                 /* an option THC does not set */
    CHECK(str_is(s, "keep-open", "always"));          /* an option THC sets itself: the user wins */
    CHECK(str_is(s, "osc", "no"));                    /* same */
    CHECK(binding_has(s, "F9", "thc-test-one"));      /* input.conf is read */
}

static void check_second(VideoSession *s)
{
    CHECK_NEAR(num(s, "volume"), 61);                 /* editing mpv.conf changes mpv */
    CHECK(str_is(s, "keep-open", "no"));
    CHECK(!binding_has(s, "F9", "thc-test-one"));     /* editing input.conf changes mpv */
    CHECK(binding_has(s, "F8", "thc-test-two"));
}

static void check_profile(VideoSession *s)
{
    CHECK_NEAR(num(s, "volume"), 44);                 /* a [profile] section + profile= works */
}

static void check_script(VideoSession *s)
{
    /* scripts/ in THC's config folder run (libmpv does not autoload them by itself) */
    double v = -1;
    for (int i = 0; i < 40 && v != 23; i++) {
        SDL_Delay(50);
        v = num(s, "volume");
    }
    CHECK_NEAR(v, 23);
}

int main(void)
{
    memset(&api, 0, sizeof api);
    if (!mpv_dyn_load(&api, NULL)) {
        printf("SKIP: libmpv not found\n");
        return 0;
    }
    /* a "system" mpv setup that THC must never read */
    SDL_CreateDirectory("mpvcfg_sys");
    SDL_CreateDirectory("mpvcfg_sys/mpv");
    write_file("mpvcfg_sys/mpv/mpv.conf", "volume=11\nkeep-open=always\n");
    write_file("mpvcfg_sys/mpv/input.conf", "F9 show-text thc-test-system\n");
    SDL_setenv_unsafe("XDG_CONFIG_HOME", "mpvcfg_sys", 1);
    SDL_setenv_unsafe("HOME", "mpvcfg_sys", 1);
    SDL_setenv_unsafe("APPDATA", "mpvcfg_sys", 1);

    SDL_CreateDirectory("mpvcfg_thc");
    SDL_RemovePath("mpvcfg_thc/mpv.conf");
    SDL_RemovePath("mpvcfg_thc/input.conf");

    /* 1. empty THC config: defaults, and nothing from the system config */
    session("mpvcfg_thc", check_empty);

    /* 2. a config written into THC's folder takes effect */
    write_file("mpvcfg_thc/mpv.conf", "ao=null\nvolume=37\nkeep-open=always\nosc=no\n");
    write_file("mpvcfg_thc/input.conf", "F9 show-text thc-test-one\n");
    session("mpvcfg_thc", check_first);

    /* 3. editing the files changes the next playback */
    write_file("mpvcfg_thc/mpv.conf", "ao=null\nvolume=61\n");
    write_file("mpvcfg_thc/input.conf", "F8 show-text thc-test-two\n");
    session("mpvcfg_thc", check_second);

    /* 4. profiles work */
    write_file("mpvcfg_thc/mpv.conf", "ao=null\nprofile=quiet\n[quiet]\nvolume=44\n");
    session("mpvcfg_thc", check_profile);

    /* 5. scripts work */
    SDL_CreateDirectory("mpvcfg_thc/scripts");
    write_file("mpvcfg_thc/mpv.conf", "ao=null\n");
    write_file("mpvcfg_thc/scripts/set_volume.lua", "mp.set_property('volume', '23')\n");
    session("mpvcfg_thc", check_script);
    SDL_RemovePath("mpvcfg_thc/scripts/set_volume.lua");
    SDL_RemovePath("mpvcfg_thc/scripts");

    SDL_RemovePath("mpvcfg_thc/mpv.conf");
    SDL_RemovePath("mpvcfg_thc/input.conf");
    SDL_RemovePath("mpvcfg_sys/mpv/mpv.conf");
    SDL_RemovePath("mpvcfg_sys/mpv/input.conf");
    TEST_DONE();
}
