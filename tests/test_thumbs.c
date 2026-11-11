/* test_thumbs.c: thumbnail cache keys, sidecar images, black-frame detection, cache
 * pruning, and (when libmpv is installed) making a real thumbnail. */
#include "mpv_dyn.h"
#include "thumbs.h"
#include "test_util.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#ifndef THC_TEST_DATA
#define THC_TEST_DATA "."
#endif

/* the app's generator (src/thumbgen.c) */
bool thumb_generate(const MpvApi *api, const char *video, const char *out);

#include "stb_image.h"

static void test_keys(void)
{
    char a[17], b[17], c[17], d[17];
    thumb_key(1000, 5000, a);
    thumb_key(1000, 5000, b);
    thumb_key(1000, 5001, c);   /* the file was edited */
    thumb_key(1001, 5000, d);   /* a different file */
    CHECK_STR(a, b);
    CHECK(strcmp(a, c) != 0 && strcmp(a, d) != 0 && strcmp(c, d) != 0);
    CHECK(strlen(a) == 16);
}

static void test_sidecar(void)
{
    FsNode *root = fs_new("lib", true);
    fs_add_path(root, "Show/S01E01.mkv", 10, 1);
    fs_add_path(root, "Show/S01E01-thumb.jpg", 10, 1);
    fs_add_path(root, "Show/S01E02.mkv", 10, 1);
    fs_add_path(root, "Show/S01E02.png", 10, 1);
    fs_add_path(root, "Show/S01E02-thumb.jpg", 10, 1);
    fs_add_path(root, "Show/S01E03.mkv", 10, 1);
    fs_add_path(root, "Show/poster.jpg", 10, 1);          /* the show's poster is not an episode thumbnail */
    fs_add_path(root, "Show/s01e04.MKV", 10, 1);
    fs_add_path(root, "Show/S01E04-Poster.PNG", 10, 1);   /* case-insensitive */
    fs_sort(root, true);
    const FsNode *e1 = fs_find(root, "Show/S01E01.mkv");
    const FsNode *e2 = fs_find(root, "Show/S01E02.mkv");
    const FsNode *e3 = fs_find(root, "Show/S01E03.mkv");
    const FsNode *e4 = fs_find(root, "Show/s01e04.MKV");
    CHECK(e1 && e2 && e3 && e4);
    const FsNode *s1 = thumb_sidecar(e1);
    CHECK(s1 && !strcmp(s1->name, "S01E01-thumb.jpg"));
    const FsNode *s2 = thumb_sidecar(e2);
    CHECK(s2 && !strcmp(s2->name, "S01E02.png"));          /* the plain name comes first */
    CHECK(thumb_sidecar(e3) == NULL);
    const FsNode *s4 = thumb_sidecar(e4);
    CHECK(s4 && !strcmp(s4->name, "S01E04-Poster.PNG"));
    CHECK(thumb_sidecar(NULL) == NULL);
    CHECK(thumb_sidecar(fs_find(root, "Show")) == NULL);   /* folders have none */
    fs_free(root);
}

static void test_brightness(void)
{
    unsigned char black[16 * 16 * 4], white[16 * 16 * 4], dim[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; i++) {
        black[i * 4] = black[i * 4 + 1] = black[i * 4 + 2] = 2; black[i * 4 + 3] = 255;
        white[i * 4] = white[i * 4 + 1] = white[i * 4 + 2] = 250; white[i * 4 + 3] = 255;
        dim[i * 4] = dim[i * 4 + 1] = dim[i * 4 + 2] = 60; dim[i * 4 + 3] = 255;
    }
    CHECK(thumb_brightness(black, 16, 16) < THUMB_DARK_LIMIT);
    CHECK(thumb_brightness(dim, 16, 16) > THUMB_DARK_LIMIT);
    CHECK(thumb_brightness(white, 16, 16) > 200);
    CHECK(thumb_brightness(NULL, 0, 0) == 0);
}

static void test_prune(void)
{
    ThumbFile f[5] = { { 100, 50 }, { 100, 10 }, { 100, 40 }, { 100, 20 }, { 100, 30 } };
    int out[5];
    CHECK(thumb_prune_plan(f, 5, 500, 300, out) == 0);          /* fits: nothing to do */
    int k = thumb_prune_plan(f, 5, 400, 250, out);              /* 500 > 400: drop oldest down to <= 250 */
    CHECK(k == 3);
    CHECK(out[0] == 1 && out[1] == 3 && out[2] == 4);           /* mtimes 10, 20, 30 */
    CHECK(thumb_prune_plan(f, 0, 0, 0, out) == 0);
}

static void test_generate(void)
{
    MpvApi api;
    memset(&api, 0, sizeof api);
    if (!mpv_dyn_load(&api, NULL)) {
        printf("SKIP generation: libmpv not found\n");
        return;
    }
    const char *video = THC_TEST_DATA "/tiny.mp4";
    const char *out = "thumbtest_out.jpg";
    SDL_RemovePath(out);

    /* the video file is only read */
    size_t len1 = 0;
    void *before = SDL_LoadFile(video, &len1);
    CHECK(thumb_generate(&api, video, out));
    SDL_PathInfo info;
    CHECK(SDL_GetPathInfo(out, &info) && info.size > 100);
    size_t len2 = 0;
    void *after = SDL_LoadFile(video, &len2);
    CHECK(before && after && len1 == len2 && !memcmp(before, after, len1));
    SDL_free(before);
    SDL_free(after);

    /* the picture decodes and is not black */
    int w = 0, h = 0, c = 0;
    size_t jl = 0;
    void *jd = SDL_LoadFile(out, &jl);
    unsigned char *px = jd ? stbi_load_from_memory(jd, (int)jl, &w, &h, &c, 4) : NULL;
    SDL_free(jd);
    CHECK(px && w == 160 && h == 90);
    if (px) {
        CHECK(thumb_brightness(px, w, h) > THUMB_DARK_LIMIT);
        stbi_image_free(px);
    }
    /* nothing is left behind next to the output */
    CHECK(!SDL_GetPathInfo("thumbtest_out.jpg.work", &info));
    CHECK(!SDL_GetPathInfo("thumbtest_out.jpg.best", &info));

    /* a missing or broken video fails cleanly */
    CHECK(!thumb_generate(&api, "thumbtest_missing.mkv", "thumbtest_none.jpg"));
    CHECK(!SDL_GetPathInfo("thumbtest_none.jpg", &info));
    mpv_dyn_unload(&api);
}

int main(void)
{
    test_keys();
    test_sidecar();
    test_brightness();
    test_prune();
    test_generate();
    TEST_DONE();
}
