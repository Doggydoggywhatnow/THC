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
