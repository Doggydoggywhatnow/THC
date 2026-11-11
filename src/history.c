#include "history.h"

#include <math.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct History {
    sqlite3 *db;
    int newer_schema;   /* nonzero: the file was made by a newer THC */
};

/* Two durations are "the same file" if they differ by less than this. */
#define DURATION_MATCH_SECONDS 1.5

/* ---- schema and versions ------------------------------------------------------
 * The database must stay usable by every version of THC, old and new:
 *  - PRAGMA user_version records the schema version. It is only ever raised, never reset.
 *  - A newer THC migrates an older database forward (after a backup next to the file), and
 *    keeps every column an older version reads.
 *  - An older THC that meets a newer database leaves it alone: it does not touch the version,
 *    and only reads and writes the columns it knows.
 * Rules for future versions: only ADD columns (nullable or with a DEFAULT), tables and indexes.
 * Never rename, drop or retype a column of an earlier version.
 * Version 1 is the 0.1.0 schema below. */
#define HISTORY_SCHEMA_VERSION 1

static const char *k_base_schema =
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
    "CREATE INDEX IF NOT EXISTS history_size ON history(size);";

/* migrations[i] upgrades version i+1 to i+2. None yet: version 1 is current. */
static const char *const k_no_migrations[] = { NULL };

static int pragma_int(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st;
    int v = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return v;
}

static bool table_exists(sqlite3 *db, const char *name)
{
    sqlite3_stmt *st;
    bool ok = false;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        ok = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    return ok;
}

/* Copy the database file to <path>.v<version>.bak (once), after folding the WAL into it. */
static void backup_before_migration(sqlite3 *db, const char *path, int version)
{
    char dst[1100];
    snprintf(dst, sizeof dst, "%s.v%d.bak", path, version);
    FILE *test = fopen(dst, "rb");
    if (test) { fclose(test); return; }
    sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE);", NULL, NULL, NULL);
    FILE *in = fopen(path, "rb"), *out = in ? fopen(dst, "wb") : NULL;
    if (in && out) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    }
    if (in) fclose(in);
    if (out) fclose(out);
}

static History *fail(History *h, char *err, size_t err_len, const char *msg)
{
    if (err) snprintf(err, err_len, "%s", msg ? msg : "history database error");
    if (h->db) sqlite3_close(h->db);
    free(h);
    return NULL;
}

History *history_open_ex(const char *db_path, int target_version, const char *const *migrations,
                         char *err, size_t err_len)
{
    History *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    if (sqlite3_open(db_path, &h->db) != SQLITE_OK) return fail(h, err, err_len, sqlite3_errmsg(h->db));
    sqlite3_busy_timeout(h->db, 2000);
    sqlite3_exec(h->db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    int ver = pragma_int(h->db, "PRAGMA user_version;");
    bool had_table = table_exists(h->db, "history");
    char *msg = NULL;
    bool unversioned = ver == 0 && had_table;
    if (unversioned) ver = 1;   /* a database from before versions were recorded */

    if (ver > target_version) {
        /* made by a newer THC: leave the version and everything we do not know alone */
        h->newer_schema = ver;
    } else {
        if (unversioned) sqlite3_exec(h->db, "PRAGMA user_version=1;", NULL, NULL, NULL);
        if (ver == 0) {
            /* brand new */
            if (sqlite3_exec(h->db, k_base_schema, NULL, NULL, &msg) != SQLITE_OK) {
                char m[256];
                snprintf(m, sizeof m, "%s", msg ? msg : "schema error");
                sqlite3_free(msg);
                return fail(h, err, err_len, m);
            }
            char sql[64];
            snprintf(sql, sizeof sql, "PRAGMA user_version=%d;", target_version);
            sqlite3_exec(h->db, sql, NULL, NULL, NULL);
            ver = target_version;
        }
        if (ver < target_version) {
            backup_before_migration(h->db, db_path, ver);
            if (sqlite3_exec(h->db, "BEGIN IMMEDIATE;", NULL, NULL, &msg) != SQLITE_OK) {
                sqlite3_free(msg);
                return fail(h, err, err_len, "could not start the history upgrade");
            }
            sqlite3_exec(h->db, k_base_schema, NULL, NULL, NULL);
            for (int v = ver; v < target_version; v++) {
                if (!migrations || !migrations[v - 1]) break;
                if (sqlite3_exec(h->db, migrations[v - 1], NULL, NULL, &msg) != SQLITE_OK) {
                    char m[300];
                    snprintf(m, sizeof m, "history upgrade to version %d failed: %s (your data is unchanged)", v + 1,
                             msg ? msg : "error");
                    sqlite3_free(msg);
                    sqlite3_exec(h->db, "ROLLBACK;", NULL, NULL, NULL);
                    return fail(h, err, err_len, m);
                }
            }
            char sql[64];
            snprintf(sql, sizeof sql, "PRAGMA user_version=%d;", target_version);
            sqlite3_exec(h->db, sql, NULL, NULL, NULL);
            sqlite3_exec(h->db, "COMMIT;", NULL, NULL, NULL);
        } else {
            sqlite3_exec(h->db, k_base_schema, NULL, NULL, NULL);   /* harmless if it already exists */
        }
    }
    return h;
}

History *history_open(const char *db_path, char *err, size_t err_len)
{
    return history_open_ex(db_path, HISTORY_SCHEMA_VERSION, k_no_migrations, err, err_len);
}

int history_schema_version(History *h)
{
    return h ? pragma_int(h->db, "PRAGMA user_version;") : 0;
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
