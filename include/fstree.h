/* fstree.h: an in-memory snapshot of a library folder.
 *
 * The scanner fills it from disk (read-only: THC never changes media files);
 * the parser turns it into shows, seasons, episodes, albums and books. Tests
 * build trees directly with fs_add_path().
 */
#ifndef THC_FSTREE_H
#define THC_FSTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum MediaKind {
    MK_OTHER = 0,
    MK_VIDEO,
    MK_AUDIO,     /* music the built-in decoders play (FLAC, WAV, OGG, MP3) */
    MK_AUDIO_MPV, /* audio that needs libmpv (M4A, AAC, Opus, ...) */
    MK_BOOK,      /* M4B audiobook */
    MK_IMAGE      /* cover art */
} MediaKind;

typedef struct FsNode {
    char *name;
    bool dir;
    MediaKind kind;
    int64_t size;
    int64_t mtime;  /* unix seconds */
    struct FsNode *parent;
    struct FsNode **kids;
    int nkids, cap;
} FsNode;

MediaKind media_kind(const char *name);
const char *path_ext(const char *name); /* lower-case-insensitive extension, without the dot */

FsNode *fs_new(const char *name, bool dir);
FsNode *fs_add(FsNode *parent, const char *name, bool dir, int64_t size, int64_t mtime);
/* Create any missing folders along a '/'-separated relative path. */
FsNode *fs_add_path(FsNode *root, const char *rel, int64_t size, int64_t mtime);
void fs_free(FsNode *n);
/* Sort children: folders first, then natural order ("Episode 2" < "Episode 10"). */
void fs_sort(FsNode *n, bool recursive);
/* Path relative to the root, with '/' separators. */
char *fs_relpath(const FsNode *n, char *buf, size_t cap);
/* Counts of media files under a node. */
int fs_count(const FsNode *n, MediaKind kind);
/* Newest mtime under a node. */
int64_t fs_newest(const FsNode *n);
/* Find a direct child by name (case-insensitive). */
FsNode *fs_child(const FsNode *n, const char *name);
/* Find a node by '/'-separated relative path ("" = the root). */
FsNode *fs_find(FsNode *root, const char *rel);
/* Find a cover image in a folder (cover/folder/poster/front .jpg/.png). */
const FsNode *fs_find_art(const FsNode *dir);

/* Natural, case-insensitive comparison. */
int natcmp(const char *a, const char *b);

#endif
