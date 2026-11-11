/* thumbs.h: the logic behind video thumbnails that needs no window or libmpv
 * (so it can be tested on its own): cache keys, sidecar images, dark-frame
 * detection and cache pruning. The generator itself is in src/thumbs.c. */
#ifndef THC_THUMBS_H
#define THC_THUMBS_H

#include "fstree.h"

#include <stdbool.h>
#include <stdint.h>

/* Cache key for a video file: derived from its size and modification time only, so
 * renaming or moving a file keeps its thumbnail while editing the file makes a new one.
 * `out` receives 16 hex digits and a NUL. */
void thumb_key(int64_t size, int64_t mtime, char out[17]);

/* An image sitting next to a video that acts as its thumbnail: "Name.jpg",
 * "Name-thumb.jpg", "Name-poster.png", "Name-landscape.jpg" or "Name-fanart.jpg".
 * Returns NULL if there is none. */
const FsNode *thumb_sidecar(const FsNode *video);

/* Average brightness (0..255) of an RGBA image, sampled. Used to skip black frames. */
double thumb_brightness(const unsigned char *rgba, int w, int h);
#define THUMB_DARK_LIMIT 14.0

/* Which cache files to delete so the cache fits: when the total size is above `cap`,
 * the oldest files go first until it is at most `target`. Fills `out` with indexes
 * into `files` and returns how many. */
typedef struct ThumbFile {
    int64_t size;
    int64_t mtime;
} ThumbFile;
int thumb_prune_plan(const ThumbFile *files, int n, int64_t cap, int64_t target, int *out);

#endif
