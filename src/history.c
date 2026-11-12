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
    bool has_v2;        /* the version 2 columns and tables exist */
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
 * Version 1 is the 0.1.0 schema below; version 2 (0.2.0) adds kind, last_played and play_count to
 * history, and the plays and queue_backlog tables. */
#define HISTORY_SCHEMA_VERSION 2

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

/* migrations[i] upgrades version i+1 to i+2. */
static const char *const k_migrations[] = {
    /* 1 -> 2 */
    "ALTER TABLE history ADD COLUMN kind INTEGER NOT NULL DEFAULT 0;"          /* 0 video, 1 music, 2 audiobook */
    "ALTER TABLE history ADD COLUMN last_played INTEGER NOT NULL DEFAULT 0;"
    "ALTER TABLE history ADD COLUMN play_count INTEGER NOT NULL DEFAULT 0;"
    "UPDATE history SET last_played = updated WHERE updated > 0;"
    "CREATE TABLE IF NOT EXISTS plays ("
    "  id INTEGER PRIMARY KEY, rel_path TEXT NOT NULL, size INTEGER NOT NULL,"
    "  kind INTEGER NOT NULL DEFAULT 0, played_at INTEGER NOT NULL,"
    "  listened_seconds REAL NOT NULL DEFAULT 0);"
    "CREATE INDEX IF NOT EXISTS plays_time ON plays(played_at);"
    "CREATE TABLE IF NOT EXISTS queue_backlog ("
    "  queue INTEGER NOT NULL, ord INTEGER NOT NULL, rel_path TEXT NOT NULL, size INTEGER NOT NULL,"
    "  kind INTEGER NOT NULL DEFAULT 0, title TEXT, played_at INTEGER NOT NULL DEFAULT 0);",
    NULL
};

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
            sqlite3_exec(h->db, "PRAGMA user_version=1;", NULL, NULL, NULL);
            ver = 1;   /* then upgraded to the current version below, without a backup: it is empty */
        }
        if (ver < target_version) {
            if (had_table) backup_before_migration(h->db, db_path, ver);
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
    h->has_v2 = pragma_int(h->db, "PRAGMA user_version;") >= 2;
    return h;
}

History *history_open(const char *db_path, char *err, size_t err_len)
{
    return history_open_ex(db_path, HISTORY_SCHEMA_VERSION, k_migrations, err, err_len);
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

/* version 2: remember when and what kind (kind -1 keeps the stored kind) */
static void touch_row(History *h, const char *path, int64_t size, int kind)
{
    if (!h->has_v2) return;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db,
            "UPDATE history SET last_played=?1, kind=CASE WHEN ?2>=0 THEN ?2 ELSE kind END "
            "WHERE path=?3 AND size=?4", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_int64(st, 1, (int64_t)time(NULL));
    sqlite3_bind_int(st, 2, kind);
    sqlite3_bind_text(st, 3, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, size);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

bool history_save(History *h, const char *rel_path, int64_t size, double position,
                  double duration, double threshold, bool reached_end)
{
    return history_save_ex(h, rel_path, size, -1, position, duration, threshold, reached_end);
}

/* kind -1: leave the stored kind alone (new rows are video). */
bool history_save_ex(History *h, const char *rel_path, int64_t size, int kind, double position,
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
        bool ok = exec_bound(h,
            "UPDATE history SET path=?2, duration=?3, position=?4, watched=?5, "
            "watch_count=?6, updated=?7 WHERE id=?1",
            id, rel_path, duration, position, watched, count);
        if (ok) touch_row(h, rel_path, size, kind);
        return ok;
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
    if (ok) touch_row(h, rel_path, size, kind);
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

/* ---- version 2: plays, kinds, backlog ------------------------------------------------ */

static bool run1(History *h, const char *sql, int64_t a, int64_t b)
{
    sqlite3_stmt *st;
    if (!h || sqlite3_prepare_v2(h->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, a);
    sqlite3_bind_int64(st, 2, b);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool history_record_play(History *h, const char *rel_path, int64_t size, int kind, double listened)
{
    if (!h || !h->has_v2) return false;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db,
            "INSERT INTO plays(rel_path, size, kind, played_at, listened_seconds) VALUES(?1,?2,?3,?4,?5)", -1, &st,
            NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, rel_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, size);
    sqlite3_bind_int(st, 3, kind);
    sqlite3_bind_int64(st, 4, (int64_t)time(NULL));
    sqlite3_bind_double(st, 5, listened < 0 ? 0 : listened);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
    /* make sure a history row exists, then bump its counters */
    HistoryEntry e;
    if (!find_exact(h, rel_path, size, &e) && !find_moved(h, rel_path, size, 0, &e))
        history_save_ex(h, rel_path, size, kind, 0, 0, 1.0, false);
    if (sqlite3_prepare_v2(h->db,
            "UPDATE history SET play_count=play_count+1, last_played=?1, kind=?2 WHERE path=?3 AND size=?4", -1, &st,
            NULL) != SQLITE_OK)
        return true;
    sqlite3_bind_int64(st, 1, (int64_t)time(NULL));
    sqlite3_bind_int(st, 2, kind);
    sqlite3_bind_text(st, 3, rel_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, size);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return true;
}

int history_play_count(History *h, const char *rel_path, int64_t size)
{
    if (!h || !h->has_v2) return 0;
    sqlite3_stmt *st;
    int n = 0;
    if (sqlite3_prepare_v2(h->db, "SELECT play_count FROM history WHERE path=?1 AND size=?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, rel_path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, size);
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return n;
}

bool history_clear_kind(History *h, int kind)
{
    if (!h) return false;
    if (!h->has_v2) return kind == 0 ? history_clear(h) : true;
    bool ok = run1(h, "DELETE FROM history WHERE kind=?1", kind, 0);
    ok &= run1(h, "DELETE FROM plays WHERE kind=?1", kind, 0);
    ok &= run1(h, "DELETE FROM queue_backlog WHERE kind=?1", kind, 0);
    return ok;
}

bool history_remove(History *h, const char *rel_path, int64_t size)
{
    if (!h) return false;
    sqlite3_stmt *st;
    bool ok = false;
    if (sqlite3_prepare_v2(h->db, "DELETE FROM history WHERE path=?1 AND size=?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, rel_path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, size);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }
    if (h->has_v2 && sqlite3_prepare_v2(h->db, "DELETE FROM plays WHERE rel_path=?1 AND size=?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, rel_path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, size);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    return ok;
}

int history_plays_list(History *h, int kind, int mode, PlayRow *rows, int max)
{
    if (!h || !h->has_v2 || max <= 0) return 0;
    const char *sql = mode == PLAYS_RECENT
        ? "SELECT path, size, kind, play_count, last_played, duration, position FROM history "
          "WHERE kind=?1 AND play_count>0 ORDER BY last_played DESC, id DESC LIMIT ?2"
        : mode == PLAYS_MOST
        ? "SELECT path, size, kind, play_count, last_played, duration, position FROM history "
          "WHERE kind=?1 AND play_count>0 ORDER BY play_count DESC, last_played DESC LIMIT ?2"
        : "SELECT path, size, kind, play_count, last_played, duration, position FROM history "
          "WHERE kind=?1 AND watched=0 AND position>0 ORDER BY last_played DESC, id DESC LIMIT ?2";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, kind);
    sqlite3_bind_int(st, 2, max);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *p = sqlite3_column_text(st, 0);
        snprintf(rows[n].path, sizeof rows[n].path, "%s", p ? (const char *)p : "");
        rows[n].size = sqlite3_column_int64(st, 1);
        rows[n].kind = sqlite3_column_int(st, 2);
        rows[n].play_count = sqlite3_column_int(st, 3);
        rows[n].last_played = sqlite3_column_int64(st, 4);
        rows[n].duration = sqlite3_column_double(st, 5);
        rows[n].position = sqlite3_column_double(st, 6);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void csv_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

bool history_export(History *h, FILE *f, bool json, int kind)
{
    if (!h || !f) return false;
    sqlite3_stmt *st;
    const char *sql = h->has_v2
        ? "SELECT path, size, kind, duration, position, watched, play_count, last_played FROM history "
          "WHERE (?1<0 OR kind=?1) ORDER BY last_played DESC, id DESC"
        : "SELECT path, size, 0, duration, position, watched, watch_count, updated FROM history "
          "WHERE (?1<0 OR ?1=0) ORDER BY updated DESC, id DESC";
    if (sqlite3_prepare_v2(h->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int(st, 1, kind);
    static const char *const kn[] = { "video", "music", "audiobook" };
    if (json) fputs("[\n", f);
    else fputs("path,size,kind,duration_seconds,position_seconds,watched,play_count,last_played\n", f);
    bool first = true;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *p = (const char *)sqlite3_column_text(st, 0);
        int k = sqlite3_column_int(st, 2);
        const char *kname = kn[k >= 0 && k <= 2 ? k : 0];
        if (json) {
            fputs(first ? "  {" : ",\n  {", f);
            fputs("\"path\":", f); json_str(f, p ? p : "");
            fprintf(f, ",\"size\":%lld,\"kind\":\"%s\",\"duration\":%.3f,\"position\":%.3f,\"watched\":%s,"
                       "\"play_count\":%d,\"last_played\":%lld}",
                    (long long)sqlite3_column_int64(st, 1), kname, sqlite3_column_double(st, 3),
                    sqlite3_column_double(st, 4), sqlite3_column_int(st, 5) ? "true" : "false",
                    sqlite3_column_int(st, 6), (long long)sqlite3_column_int64(st, 7));
        } else {
            csv_str(f, p ? p : "");
            fprintf(f, ",%lld,%s,%.3f,%.3f,%d,%d,%lld\n", (long long)sqlite3_column_int64(st, 1), kname,
                    sqlite3_column_double(st, 3), sqlite3_column_double(st, 4), sqlite3_column_int(st, 5),
                    sqlite3_column_int(st, 6), (long long)sqlite3_column_int64(st, 7));
        }
        first = false;
    }
    if (json) fputs(first ? "]\n" : "\n]\n", f);
    sqlite3_finalize(st);
    return true;
}

bool history_backlog_save(History *h, int queue, const BacklogRow *rows, int n)
{
    if (!h || !h->has_v2) return false;
    run1(h, "DELETE FROM queue_backlog WHERE queue=?1", queue, 0);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db,
            "INSERT INTO queue_backlog(queue, ord, rel_path, size, kind, title, played_at) VALUES(?1,?2,?3,?4,?5,?6,?7)", -1,
            &st, NULL) != SQLITE_OK)
        return false;
    for (int i = 0; i < n; i++) {
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, queue);
        sqlite3_bind_int(st, 2, i);
        sqlite3_bind_text(st, 3, rows[i].path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 4, rows[i].size);
        sqlite3_bind_int(st, 5, rows[i].kind);
        sqlite3_bind_text(st, 6, rows[i].title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 7, rows[i].played_at);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
    return true;
}

int history_backlog_load(History *h, int queue, BacklogRow *rows, int max)
{
    if (!h || !h->has_v2) return 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(h->db,
            "SELECT rel_path, size, kind, title, played_at FROM queue_backlog WHERE queue=?1 ORDER BY ord LIMIT ?2", -1,
            &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int(st, 1, queue);
    sqlite3_bind_int(st, 2, max);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *p = sqlite3_column_text(st, 0), *t = sqlite3_column_text(st, 3);
        snprintf(rows[n].path, sizeof rows[n].path, "%s", p ? (const char *)p : "");
        rows[n].size = sqlite3_column_int64(st, 1);
        rows[n].kind = sqlite3_column_int(st, 2);
        snprintf(rows[n].title, sizeof rows[n].title, "%s", t ? (const char *)t : "");
        rows[n].played_at = sqlite3_column_int64(st, 4);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}
