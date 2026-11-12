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
#include <stdio.h>
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
/* The same with an explicit schema version and migrations (migrations[i] upgrades version i+1 to
 * i+2). Used by tests; history_open uses the version this build knows. A database made by a
 * newer version than `target_version` is opened untouched. */
History *history_open_ex(const char *db_path, int target_version, const char *const *migrations,
                         char *err, size_t err_len);
/* The schema version recorded in the file. */
int history_schema_version(History *h);
void history_close(History *h);

/* A moved or renamed file keeps its history only when its old path is gone from the library.
 * History cannot see the library, so the app installs this check: it returns true while `rel_path`
 * still exists (in any root) or when it cannot tell (for example while a scan is running). With no
 * check set nothing is ever treated as moved: a moved file starts a fresh entry (#24). */
void history_set_exists_check(History *h, bool (*exists)(void *ud, const char *rel_path), void *ud);

/* Write a consistent copy of the database to `dest_path` (SQLite's backup API; safe while open). */
bool history_backup_to(History *h, const char *dest_path);

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

/* Like history_save, and records what kind of media it is (KIND_* in settings.h). */
bool history_save_ex(History *h, const char *rel_path, int64_t size, int kind, double position,
                     double duration, double threshold, bool reached_end);

/* ---- version 2: plays, kinds, backlog ---- */
/* One counted play (a song reached the play threshold, an audiobook part or video finished). */
bool history_record_play(History *h, const char *rel_path, int64_t size, int kind, double listened_seconds);
int history_play_count(History *h, const char *rel_path, int64_t size);
/* Delete the history (and plays and saved backlog) of one kind only. */
bool history_clear_kind(History *h, int kind);
/* Forget one item. */
bool history_remove(History *h, const char *rel_path, int64_t size);

typedef struct PlayRow {
    char path[1024];
    int64_t size;
    int kind;
    int play_count;
    int64_t last_played;
    double duration, position;
} PlayRow;
enum { PLAYS_RECENT = 0, PLAYS_MOST, PLAYS_IN_PROGRESS };
int history_plays_list(History *h, int kind, int mode, PlayRow *rows, int max);

/* Write history as JSON or CSV (kind -1 = all). */
bool history_export(History *h, FILE *f, bool json, int kind);

/* The queue backlog kept between sessions (only when the user turns that on). */
typedef struct BacklogRow {
    char path[1024];
    int64_t size;
    int kind;
    char title[256];
    int64_t played_at;
} BacklogRow;
bool history_backlog_save(History *h, int queue, const BacklogRow *rows, int n);
int history_backlog_load(History *h, int queue, BacklogRow *rows, int max);

/* Has a track been listened to long enough to count as a play? (percent 50..100 of its length) */
bool history_play_counts(double listened_seconds, double duration, int percent);

/* Delete everything. */
bool history_clear(History *h);

/* Seconds to resume from: 0 if the file was finished or barely started. */
double history_resume_point(const HistoryEntry *e, double threshold);

/* Fraction (0..1) for a playback bar. */
double history_progress(const HistoryEntry *e);

#endif
