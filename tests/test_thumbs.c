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

static void write_blob(const char *path, size_t n, unsigned char fill, unsigned char mid)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (size_t i = 0; i < n; i++) fputc(i >= 5000 && i < n - 5000 ? mid : fill, f);
    fclose(f);
}

/* #22: files with the same size and mtime but different content must not share a key */
static void test_content_keys(void)
{
    char a[17], b[17], c[17], d[17], e[17];
    write_blob("tk_a.bin", 20000, 1, 7);
    write_blob("tk_b.bin", 20000, 2, 7);          /* different head and tail */
    write_blob("tk_c.bin", 20000, 1, 7);          /* identical to a, another name */
    thumb_key_for_file("tk_a.bin", 20000, 5000, a);
    thumb_key_for_file("tk_b.bin", 20000, 5000, b);
    thumb_key_for_file("tk_c.bin", 20000, 5000, c);
    CHECK(strcmp(a, b) != 0);                      /* the batch-copy collision */
    CHECK_STR(a, c);                               /* a rename or move keeps the thumbnail */
    CHECK(strlen(a) == 16);
    /* different mtime: a new key */
    thumb_key_for_file("tk_a.bin", 20000, 5001, d);
    CHECK(strcmp(a, d) != 0);
    /* files shorter than 8 KB, and than 4 KB */
    write_blob("tk_s1.bin", 3000, 1, 1);
    write_blob("tk_s2.bin", 3000, 9, 9);
    thumb_key_for_file("tk_s1.bin", 3000, 1, a);
    thumb_key_for_file("tk_s2.bin", 3000, 1, b);
    CHECK(strcmp(a, b) != 0);
    write_blob("tk_s3.bin", 6000, 1, 1);
    thumb_key_for_file("tk_s3.bin", 6000, 1, a);
    CHECK(strlen(a) == 16);
    /* an unreadable file falls back to the old key and does not crash */
    thumb_key_for_file("no_such_file.bin", 1000, 5000, a);
    thumb_key(1000, 5000, e);
    CHECK_STR(a, e);
    /* the key is computed once per file per session, not once per call */
    thumb_key_cache_clear();
    int r0 = thumb_key_reads();
    for (int i = 0; i < 500; i++) thumb_key_cached("tk_a.bin", 20000, 5000, a);
    for (int i = 0; i < 500; i++) thumb_key_cached("tk_b.bin", 20000, 5000, b);
    CHECK(thumb_key_reads() - r0 == 2);
    CHECK(strcmp(a, b) != 0);
    thumb_key_for_file("tk_a.bin", 20000, 5000, c);
    CHECK_STR(a, c);
    /* a changed mtime makes a new entry (one more read) */
    thumb_key_cached("tk_a.bin", 20000, 6000, c);
    CHECK(thumb_key_reads() - r0 >= 3 && strcmp(a, c) != 0);
    /* many files: the table grows and still answers */
    int r1 = thumb_key_reads();
    for (int i = 0; i < 3000; i++) {
        char p[32];
        snprintf(p, sizeof p, "missing_%d.bin", i);
        thumb_key_cached(p, i, i, a);
    }
    for (int i = 0; i < 3000; i++) {
        char p[32];
        snprintf(p, sizeof p, "missing_%d.bin", i);
        thumb_key_cached(p, i, i, a);
    }
    CHECK(thumb_key_reads() == r1);                /* unreadable files are never "read" */
    thumb_key_cache_clear();
    /* reading the key leaves the file untouched */
    FILE *f = fopen("tk_a.bin", "rb");
    CHECK(f != NULL);
    if (f) { fseek(f, 0, SEEK_END); CHECK(ftell(f) == 20000); fclose(f); }
    remove("tk_a.bin"); remove("tk_b.bin"); remove("tk_c.bin");
    remove("tk_s1.bin"); remove("tk_s2.bin"); remove("tk_s3.bin");
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
    test_content_keys();
    test_sidecar();
    test_brightness();
    test_prune();
    test_generate();
    TEST_DONE();
}
