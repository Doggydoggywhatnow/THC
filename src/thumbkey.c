/* thumbkey.c: see thumbs.h. */
#include "thumbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void thumb_key(int64_t size, int64_t mtime, char out[17])
{
    uint64_t h = 1469598103934665603ull;
    uint64_t v[2] = { (uint64_t)size, (uint64_t)mtime };
    for (int i = 0; i < 2; i++)
        for (int b = 0; b < 8; b++) {
            h ^= (v[i] >> (b * 8)) & 0xFF;
            h *= 1099511628211ull;
        }
    snprintf(out, 17, "%016llx", (unsigned long long)h);
}

#define KEY_BLOCK 4096

static uint64_t fnv_bytes(uint64_t h, const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static int g_reads;

void thumb_key_for_file(const char *path, int64_t size, int64_t mtime, char out[17])
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        thumb_key(size, mtime, out);       /* unreadable: the old key, no crash */
        return;
    }
    g_reads++;
    unsigned char head[KEY_BLOCK], tail[KEY_BLOCK];
    size_t nh = fread(head, 1, sizeof head, f), nt = 0;
    if (size > (int64_t)KEY_BLOCK && fseek(f, -(long)KEY_BLOCK, SEEK_END) == 0) nt = fread(tail, 1, sizeof tail, f);
    fclose(f);
    char base[17];
    thumb_key(size, mtime, base);
    uint64_t h = 1469598103934665603ull;
    h = fnv_bytes(h, (const unsigned char *)base, 16);
    h = fnv_bytes(h, head, nh);
    h = fnv_bytes(h, tail, nt);
    snprintf(out, 17, "%016llx", (unsigned long long)h);
}

typedef struct KeyEntry { char *path; int64_t size, mtime; char key[17]; } KeyEntry;
static KeyEntry *g_keys;
static size_t g_nkeys, g_capkeys;
static uint32_t *g_slots;           /* open addressing: index + 1, 0 = empty */
static size_t g_nslots;

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static void rehash(size_t n)
{
    free(g_slots);
    g_slots = (uint32_t *)calloc(n, sizeof *g_slots);
    g_nslots = g_slots ? n : 0;
    for (size_t i = 0; i < g_nkeys && g_nslots; i++) {
        size_t s = hash_str(g_keys[i].path) & (g_nslots - 1);
        while (g_slots[s]) s = (s + 1) & (g_nslots - 1);
        g_slots[s] = (uint32_t)i + 1;
    }
}

void thumb_key_cached(const char *path, int64_t size, int64_t mtime, char out[17])
{
    if (!path || !g_slots) {
        if (!g_slots && path) rehash(1024);
        if (!g_slots) { thumb_key_for_file(path, size, mtime, out); return; }
    }
    size_t s = hash_str(path) & (g_nslots - 1);
    while (g_slots[s]) {
        KeyEntry *e = &g_keys[g_slots[s] - 1];
        if (!strcmp(e->path, path)) {
            if (e->size == size && e->mtime == mtime) { memcpy(out, e->key, 17); return; }
            thumb_key_for_file(path, size, mtime, e->key);      /* the file changed: key it again */
            e->size = size;
            e->mtime = mtime;
            memcpy(out, e->key, 17);
            return;
        }
        s = (s + 1) & (g_nslots - 1);
    }
    thumb_key_for_file(path, size, mtime, out);
    if (g_nkeys == g_capkeys) {
        size_t nc = g_capkeys ? g_capkeys * 2 : 1024;
        KeyEntry *nk = (KeyEntry *)realloc(g_keys, nc * sizeof *nk);
        if (!nk) return;
        g_keys = nk;
        g_capkeys = nc;
    }
    if ((g_nkeys + 1) * 2 > g_nslots) {
        rehash(g_nslots * 2);
        s = hash_str(path) & (g_nslots - 1);
        while (g_slots[s]) s = (s + 1) & (g_nslots - 1);
    }
    KeyEntry *e = &g_keys[g_nkeys];
    size_t pl = strlen(path) + 1;
    e->path = (char *)malloc(pl);
    if (!e->path) return;
    memcpy(e->path, path, pl);
    e->size = size;
    e->mtime = mtime;
    memcpy(e->key, out, 17);
    g_slots[s] = (uint32_t)++g_nkeys;
}

int thumb_key_reads(void) { return g_reads; }

void thumb_key_cache_clear(void)
{
    for (size_t i = 0; i < g_nkeys; i++) free(g_keys[i].path);
    free(g_keys);
    free(g_slots);
    g_keys = NULL;
    g_slots = NULL;
    g_nkeys = g_capkeys = g_nslots = 0;
}

const FsNode *thumb_sidecar(const FsNode *video)
{
    if (!video || video->dir || !video->parent) return NULL;
    char stem[512];
    snprintf(stem, sizeof stem, "%s", video->name);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) *dot = 0;
    static const char *const suffix[] = { "", "-thumb", "-poster", "-landscape", "-fanart" };
    static const char *const ext[] = { "jpg", "jpeg", "png" };
    for (size_t s = 0; s < sizeof suffix / sizeof suffix[0]; s++)
        for (size_t e = 0; e < sizeof ext / sizeof ext[0]; e++) {
            char name[640];
            snprintf(name, sizeof name, "%s%s.%s", stem, suffix[s], ext[e]);
            const FsNode *n = fs_child(video->parent, name);
            if (n && !n->dir && n->kind == MK_IMAGE) return n;
        }
    return NULL;
}

double thumb_brightness(const unsigned char *rgba, int w, int h)
{
    if (!rgba || w <= 0 || h <= 0) return 0;
    double sum = 0;
    long n = 0;
    int sx = w > 64 ? w / 64 : 1, sy = h > 64 ? h / 64 : 1;
    for (int y = 0; y < h; y += sy)
        for (int x = 0; x < w; x += sx) {
            const unsigned char *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
            sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
            n++;
        }
    return n ? sum / (double)n : 0;
}

typedef struct Old { int64_t mtime; int idx; } Old;

static int old_cmp(const void *a, const void *b)
{
    const Old *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime < y->mtime ? -1 : 1;
    return x->idx - y->idx;
}

int thumb_prune_plan(const ThumbFile *files, int n, int64_t cap, int64_t target, int *out)
{
    int64_t total = 0;
    for (int i = 0; i < n; i++) total += files[i].size;
    if (total <= cap || n <= 0) return 0;
    Old *order = (Old *)malloc((size_t)n * sizeof *order);
    if (!order) return 0;
    for (int i = 0; i < n; i++) order[i] = (Old){ files[i].mtime, i };
    qsort(order, (size_t)n, sizeof *order, old_cmp);
    int k = 0;
    for (int i = 0; i < n && total > target; i++) {
        out[k++] = order[i].idx;
        total -= files[order[i].idx].size;
    }
    free(order);
    return k;
}
