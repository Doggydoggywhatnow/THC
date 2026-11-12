/* search.h: music search (core, no SDL).
 *
 * Matches case-, accent- and punctuation-insensitively ("beyonce" finds "Beyoncé"), ranks exact,
 * then prefix, then word start, then substring, then a typo (one edit, words of 4 or more letters).
 * An index holds artists, albums and songs; audiobooks are never added to it. */
#ifndef THC_SEARCH_H
#define THC_SEARCH_H

#include <stdbool.h>
#include <stddef.h>

enum { SK_ARTIST = 0, SK_ALBUM = 1, SK_SONG = 2, SK_COUNT };
enum { SCOPE_FILTER_ALL = 0, SCOPE_FILTER_ARTISTS, SCOPE_FILTER_ALBUMS, SCOPE_FILTER_SONGS };   /* match SCOPE_* in settings.h */

typedef struct SearchEntry {
    int kind;
    char *name;      /* as shown */
    char *norm;      /* normalized name (and, for songs, the artist and album words after it) */
    char *name_norm; /* normalized name alone */
    void *ref;       /* the caller's object (a folder or file node) */
} SearchEntry;

typedef struct SearchIndex {
    SearchEntry *e;
    int n, cap;
} SearchIndex;

typedef struct SearchHit {
    int entry;
    int score;       /* higher is better */
} SearchHit;

/* Lower-case, fold accents, turn punctuation into single spaces. Returns the length written. */
size_t search_normalize(const char *in, char *out, size_t cap);

void search_index_init(SearchIndex *x);
void search_index_clear(SearchIndex *x);
/* `extra` (may be NULL) is searched too but is not part of the name (a song's artist and album). */
bool search_index_add(SearchIndex *x, int kind, const char *name, const char *extra, void *ref);

/* One word is within one edit (insert, delete, change, swap) of another. */
bool search_within_one_edit(const char *a, const char *b);

/* Best matches of the query in one group (kind), best first, at most `max`. Returns the count.
 * scope filters kinds like SCOPE_FILTER_*; a kind that the scope excludes gives no hits. */
int search_query(const SearchIndex *x, const char *query, int kind, int scope, SearchHit *out, int max);

#endif
