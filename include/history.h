/* history.h: watch history and resume positions, stored in SQLite.
 *
 * Entries are keyed by the file's path relative to its library folder plus
 * its size in bytes, with the duration as a tie-breaker. If a file is moved
 * or renamed inside the library, the old entry is found again by size (and
 * duration, once known) and re-keyed to the new path, so history follows
 * the file. THC never writes to media files; this database is all it keeps.
 */
#ifndef THC_HISTORY_H
#define THC_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct History History;

typedef struct HistoryEntry {
    double position;   /* seconds */
    double duration;   /* seconds, 0 = unknown */
    bool watched;
    int64_t updated;   /* unix time of the last save */
    int watch_count;   /* times played to the watched threshold */
} HistoryEntry;

/* A row for "continue watching" style lists. */
typedef struct HistoryRow {
    char path[1024];
    int64_t size;
    HistoryEntry e;
} HistoryRow;

#define HISTORY_DEFAULT_THRESHOLD 0.90

History *history_open(const char *db_path, char *err, size_t err_len);
void history_close(History *h);

/* Look up a file. Returns false if there is no history for it. */
bool history_get(History *h, const char *rel_path, int64_t size, HistoryEntry *out);

/* Record the position. Marks the file watched once position reaches
 * `threshold` (0..1) of the duration, or when `reached_end` is set. A file
 * that was watched stays watched. */
bool history_save(History *h, const char *rel_path, int64_t size, double position,
                  double duration, double threshold, bool reached_end);

/* Manually mark a file watched or unwatched (resets the position). */
bool history_set_watched(History *h, const char *rel_path, int64_t size, bool watched);

/* Most recently updated, unfinished entries (position > 0, not watched). */
int history_in_progress(History *h, HistoryRow *rows, int max_rows);

/* Delete everything. */
bool history_clear(History *h);

/* Seconds to resume from: 0 if the file was finished or barely started. */
double history_resume_point(const HistoryEntry *e, double threshold);

/* Fraction (0..1) for a playback bar. */
double history_progress(const HistoryEntry *e);

#endif
