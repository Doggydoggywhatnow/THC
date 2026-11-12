/* test_scan_nsfw.c: the NSFW folder is scanned once, as its own root, and left out of every other root (#34). */
#include "nsfw.h"
#include "scantree.h"
#include "test_util.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static void touch(const char *base, const char *rel, const char *data)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", base, rel);
    for (char *s = p + strlen(base) + 1; *s; s++)
        if (*s == '/') { *s = 0; SDL_CreateDirectory(p); *s = '/'; }
    /* SDL's file calls take UTF-8 paths on every OS (fopen does not on Windows) */
    SDL_IOStream *f = SDL_IOFromFile(p, "wb");
    if (f) { SDL_WriteIO(f, data, strlen(data)); SDL_CloseIO(f); }
}

static bool has(const FsNode *root, const char *rel)
{
    return root && fs_find((FsNode *)root, rel) != NULL;
}

static bool unchanged(const char *base, const char *rel, const char *want)
{
    char p[1024], buf[64] = { 0 };
    snprintf(p, sizeof p, "%s/%s", base, rel);
    SDL_IOStream *f = SDL_IOFromFile(p, "rb");
    if (!f) return false;
    size_t n = SDL_ReadIO(f, buf, sizeof buf - 1);
    SDL_CloseIO(f);
    buf[n] = 0;
    return !strcmp(buf, want);
}

static bool nsfw_node(void *ud, const FsNode *n)
{
    (void)ud;
    return n->dir ? false : true;       /* every file of the NSFW root */
}

int main(void)
{
    const char *base = "thc_test_scan_nsfw";
    SDL_RemovePath(base);
    SDL_CreateDirectory(base);
    char vid[256], nsf[256], inner[256];
    snprintf(vid, sizeof vid, "%s/videos", base);
    snprintf(nsf, sizeof nsf, "%s/videos/Private", base);          /* the NSFW folder lies INSIDE the video folder */
    snprintf(inner, sizeof inner, "%s/other", base);
    touch(base, "videos/Show/S01/e1.mkv", "show-bytes");
    touch(base, "videos/Private/Group One/a.mkv", "a-bytes");
    touch(base, "videos/Private/Group One/deeper/b.mp3", "b-bytes");
    touch(base, "videos/Private/loose.mkv", "loose-bytes");
    touch(base, "videos/Private/Empty Group/notes.txt", "text");
    touch(base, "videos/Private/B\xc3\xa9ta/c.m4b", "c-bytes");
    touch(base, "other/film.mkv", "film-bytes");

    /* 1. the video folder, with the NSFW folder left out: it appears only once (in its own root) */
    FsNode *v = scan_tree(vid, nsf);
    CHECK(v != NULL && has(v, "Show/S01/e1.mkv"));
    CHECK(!has(v, "Private") && !has(v, "Private/loose.mkv") && !has(v, "Private/Group One/a.mkv"));
    FsNode *n = scan_tree(nsf, NULL);
    CHECK(n != NULL && has(n, "loose.mkv") && has(n, "Group One/a.mkv") && has(n, "Group One/deeper/b.mp3"));
    CHECK(has(n, "B\xc3\xa9ta/c.m4b"));
    CHECK(!has(n, "Empty Group"));                                  /* empty folders are never listed */
    CHECK(!has(n, "Show"));

    /* 2. without a skip the same files show up twice: the exclusion is what prevents it */
    FsNode *dup = scan_tree(vid, NULL);
    CHECK(dup && has(dup, "Private/loose.mkv"));
    fs_free(dup);

    /* 3. grouping of what the scan found */
    NsfwGroup g[16];
    int ng = nsfw_groups(n, g, 16);
    CHECK(ng == 3);                                                 /* Group One, B\xc3\xa9ta, Unsorted */
    bool unsorted = false;
    for (int i = 0; i < ng; i++) unsorted |= !strcmp(g[i].name, "Unsorted") && g[i].items == 1 && !g[i].dir;
    CHECK(unsorted);

    /* 4. the NSFW root is hidden, everything else is not; the normal tree has no NSFW file, the NSFW tree has them all */
    NsfwIndex *x = nsfw_index_new();
    NsfwConfig c = { .enabled = true, .dir_set = true, .dir_root = 7 };
    nsfw_index_config(x, &c);
    FsNode *normal = fs_filter(n, nsfw_node, x, false);
    FsNode *only = fs_filter(n, nsfw_node, x, true);
    CHECK(nsfw_item_count(normal) == 0 && nsfw_item_count(only) == 4);
    CHECK(nsfw_hidden(x, 7, "loose.mkv", false, 1, NULL) && !nsfw_hidden(x, 0, "Show/S01/e1.mkv", false, 1, NULL));
    fs_free(normal);
    fs_free(only);
    nsfw_index_free(x);

    /* 5. a library folder that is not the NSFW folder is scanned as usual; no folder = no tree */
    FsNode *o = scan_tree(inner, nsf);
    CHECK(o && has(o, "film.mkv"));
    CHECK(scan_tree("thc_test_scan_nsfw/does-not-exist", NULL) == NULL);
    CHECK(scan_tree("", NULL) == NULL);

    /* 6. THC only reads: no file was changed, added or removed */
    CHECK(unchanged(base, "videos/Private/loose.mkv", "loose-bytes"));
    CHECK(unchanged(base, "videos/Private/Group One/a.mkv", "a-bytes"));
    CHECK(unchanged(base, "videos/Show/S01/e1.mkv", "show-bytes"));
    CHECK(unchanged(base, "videos/Private/Empty Group/notes.txt", "text"));

    fs_free(v);
    fs_free(n);
    fs_free(o);
    SDL_RemovePath(base);
    TEST_DONE();
}
