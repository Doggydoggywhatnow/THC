/* tags.h: read titles, artists, albums, track numbers and cover art from the
 * metadata inside audio files.
 *
 * Formats: MP3 (ID3v1, ID3v2.2/2.3/2.4), FLAC (Vorbis comments and pictures),
 * OGG Vorbis and Opus (Vorbis comments), M4B/M4A/MP4 (iTunes atoms, covers and
 * chapter list) and WAV (RIFF INFO and embedded ID3).
 *
 * Everything is read-only and local: THC never writes or "fixes" tags, and
 * never talks to the network. Bad or truncated files just return what could
 * be read. No SDL here, so it can be tested on its own. */
#ifndef THC_TAGS_H
#define THC_TAGS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TagInfo {
    char title[256];
    char artist[256];
    char album[256];
    char album_artist[256];
    char genre[64];
    char narrator[256];       /* audiobooks */
    int track, track_total;   /* 0 = unknown */
    int disc, disc_total;
    int year;
    int chapters;             /* M4B chapter-list count, 0 if none */
    bool has_art;             /* an embedded picture exists */
    char art_mime[32];
    char format[8];           /* "id3", "flac", "ogg", "mp4", "wav" */
} TagInfo;

/* Fill `t` from the file's tags. Returns true if any tag field or picture
 * was found. `t` is always initialized. */
bool tags_read(const char *path, TagInfo *t);

/* Return a copy of the embedded cover (front cover preferred) as malloc'd
 * bytes, or NULL. The caller frees it. `mime` (may be NULL) receives its type. */
unsigned char *tags_read_art(const char *path, size_t *len, char *mime, size_t mime_cap);

/* Heuristic: does this tag set order its files by track number? */
int tags_order(const TagInfo *a, const TagInfo *b); /* disc, then track */

#endif
