#include "history.h"

#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct History {
    sqlite3 *db;
};

/* Two durations are "the same file" if they differ by less than this. */
#define DURATION_MATCH_SECONDS 1.5

static const char *k_schema =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA synchronous=NORMAL;"
    "CREATE TABLE IF NOT EXISTS history ("
    "  id INTEGER PRIMARY KEY,"
    "  path TEXT NOT NULL,"
    "  size INTEGER NOT NULL,"
    "  duration REAL NOT NULL DEFAULT 0,"
    "  position REAL NOT NULL DEFAULT 0,"
    "  watched INTEGER NOT NULL DEFAULT 0,"
    "  watch_count INTEGER NOT NULL DEFAULT 0,"
    "  updated INTEGER NOT NULL DEFAULT 0,"
    "  UNIQUE(path, size));"
    "CREATE INDEX IF NOT EXISTS history_size ON history(size);"
    "PRAGMA user_version=1;";

History *history_open(const char *db_path, char *err, size_t err_len)
{
    History *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    if (sqlite3_open(db_path, &h->db) != SQLITE_OK) {
        if (err) snprintf(err, err_len, "%s", sqlite3_errmsg(h->db));
        sqlite3_close(h->db);
        free(h);
        return NULL;
    }
    sqlite3_busy_timeout(h->db, 2000);
    char *msg = NULL;
    if (sqlite3_exec(h->db, k_schema, NULL, NULL, &msg) != SQLITE_OK) {
        if (err) snprintf(err, err_len, "%s", msg ? msg : "schema error");
        sqlite3_free(msg);
        sqlite3_close(h->db);
        free(h);
        return NULL;
    }
    return h;
}

void history_close(History *h)
{
    if (!h) return;
    sqlite3_close(h->db);
    free(h);
}

static void read_entry(sqlite3_stmt *st, int col, HistoryEntry *out)
{
    out->duration = sqlite3_column_double(st, col + 0);
    out->position = sqlite3_column_double(st, col + 1);
    out->watched = sqlite3_column_int(st, col + 2) != 0;
    out->watch_count = sqlite3_column_int(st, col + 3);
    out->updated = sqlite3_column_int64(st, col + 4);
}

/* Exact match on (path, size). Returns the row id or 0. */
static int64_t find_exact(History *h, const char *path, int64_t size, HistoryEntry *out)
{
    sqlite3_stmt *st;
    int64_t id = 0;
    if (sqlite3_prepare_v2(h->db,
            "SELECT id, duration, position, watched, watch_count, updated "
            "FROM history WHERE path=?1 AND size=?2", -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, size);
    if (sqlite3_step(st) == SQLITE_ROW) {
        id = sqlite3_column_int64(st, 0);
        if (out) read_entry(st, 1, out);
    }
    sqlite3_finalize(st);
    return id;
}

/* A moved or renamed file: same size, different path. If `duration` > 0 the
 * stored duration must match too. Only an unambiguous match counts. */
static int64_t find_moved(History *h, const char *path, int64_t size, double duration,
                          HistoryEntry *out)
{
    sqlite3_stmt *st;
    int64_t id = 0;
    int matches = 0;
    if (size <= 0) return 0;
    if (sqlite3_prepare_v2(h->db,
            "SELECT id, duration, position, watched, watch_count, updated "
            "FROM history WHERE size=?1 AND path<>?2 ORDER BY updated DESC", -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int64(st, 1, size);
    sqlite3_bind_text(st, 2, path, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        double d = sqlite3_column_double(st, 1);
        if (duration > 0 && d > 0 && fabs(d - duration) >= DURATION_MATCH_SECONDS) continue;
        if (++matches > 1) { id = 0; break; }
        id = sqlite3_column_int64(st, 0);
        if (out) read_entry(st, 1, out);
    }
    sqlite3_finalize(st);
    return matches == 1 ? id : 0;
}

bool history_get(History *h, const char *rel_path, int64_t size, HistoryEntry *out)
{
    HistoryEntry tmp;
    if (!out) out = &tmp;
    memset(out, 0, sizeof *out);
    if (!h) return false;
    if (find_exact(h, rel_path, size, out)) return true;
    return find_moved(h, rel_path, size, 0, out) != 0;
}

static bool exec_bound(History *h, const char *sql, int64_t id, const char *path, double dur,
                       double pos, int watched, int count)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_text(st, 2, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 3, dur);
    sqlite3_bind_double(st, 4, pos);
    sqlite3_bind_int(st, 5, watched);
    sqlite3_bind_int(st, 6, count);
    sqlite3_bind_int64(st, 7, (int64_t)time(NULL));
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool history_save(History *h, const char *rel_path, int64_t size, double position,
                  double duration, double threshold, bool reached_end)
{
    if (!h) return false;
    if (position < 0 || position != position) position = 0;
    if (duration < 0 || duration != duration) duration = 0;
    if (threshold <= 0 || threshold > 1) threshold = HISTORY_DEFAULT_THRESHOLD;

    HistoryEntry old;
    memset(&old, 0, sizeof old);
    int64_t id = find_exact(h, rel_path, size, &old);
    if (!id) id = find_moved(h, rel_path, size, duration, &old);
    if (duration <= 0) duration = old.duration;

    bool now_watched = reached_end || (duration > 0 && position >= threshold * duration);
    int watched = old.watched || now_watched;
    int count = old.watch_count + (now_watched && !(old.watched && old.position >= threshold * duration));

    if (id) {
        /* Also re-keys a moved file to its new path. */
        return exec_bound(h,
            "UPDATE history SET path=?2, duration=?3, position=?4, watched=?5, "
            "watch_count=?6, updated=?7 WHERE id=?1",
            id, rel_path, duration, position, watched, count);
    }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db,
            "INSERT INTO history(path, size, duration, position, watched, watch_count, updated) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, rel_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, size);
    sqlite3_bind_double(st, 3, duration);
    sqlite3_bind_double(st, 4, position);
    sqlite3_bind_int(st, 5, watched);
    sqlite3_bind_int(st, 6, count);
    sqlite3_bind_int64(st, 7, (int64_t)time(NULL));
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool history_set_watched(History *h, const char *rel_path, int64_t size, bool watched)
{
    if (!h) return false;
    HistoryEntry old;
    memset(&old, 0, sizeof old);
    int64_t id = find_exact(h, rel_path, size, &old);
    if (!id) id = find_moved(h, rel_path, size, 0, &old);
    double pos = watched ? old.duration : 0;
    int count = old.watch_count + (watched && !old.watched);
    if (id)
        return exec_bound(h,
            "UPDATE history SET path=?2, duration=?3, position=?4, watched=?5, "
            "watch_count=?6, updated=?7 WHERE id=?1",
            id, rel_path, old.duration, pos, watched, count);
    if (!watched) return true;
    return history_save(h, rel_path, size, 0, 0, 1.0, true);
}

int history_in_progress(History *h, HistoryRow *rows, int max_rows)
{
    if (!h || max_rows <= 0) return 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db,
            "SELECT path, size, duration, position, watched, watch_count, updated FROM history "
            "WHERE watched=0 AND position>0 ORDER BY updated DESC, id DESC LIMIT ?1",
            -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int(st, 1, max_rows);
    int n = 0;
    while (n < max_rows && sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *p = sqlite3_column_text(st, 0);
        snprintf(rows[n].path, sizeof rows[n].path, "%s", p ? (const char *)p : "");
        rows[n].size = sqlite3_column_int64(st, 1);
        read_entry(st, 2, &rows[n].e);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

bool history_clear(History *h)
{
    if (!h) return false;
    return sqlite3_exec(h->db, "DELETE FROM history;", NULL, NULL, NULL) == SQLITE_OK;
}

double history_resume_point(const HistoryEntry *e, double threshold)
{
    if (threshold <= 0 || threshold > 1) threshold = HISTORY_DEFAULT_THRESHOLD;
    if (e->position < 5.0) return 0;                 /* barely started */
    if (e->duration > 0 && e->position >= threshold * e->duration) return 0; /* finished */
    return e->position;
}

double history_progress(const HistoryEntry *e)
{
    if (e->duration <= 0) return e->watched ? 1.0 : 0.0;
    double f = e->position / e->duration;
    return f < 0 ? 0 : f > 1 ? 1 : f;
}
