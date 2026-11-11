/* History must work across THC versions: a new version reads and upgrades an old database, and an
 * old version leaves a newer database alone. */
#include "history.h"
#include "test_util.h"

#include <sqlite3.h>
#include <stdlib.h>

#define DB "thc_test_compat.db"

static void clean(void)
{
    const char *ext[] = { "", "-wal", "-shm", ".v1.bak", ".v2.bak" };
    for (int i = 0; i < 5; i++) {
        char p[128];
        snprintf(p, sizeof p, "%s%s", DB, ext[i]);
        remove(p);
    }
}

static void sql(const char *q)
{
    sqlite3 *db;
    sqlite3_open(DB, &db);
    char *m = NULL;
    if (sqlite3_exec(db, q, NULL, NULL, &m) != SQLITE_OK) { fprintf(stderr, "sql: %s\n", m); g_failures++; }
    sqlite3_close(db);
}

static bool exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

/* the exact schema of 0.1.0 and 0.1.1 (user_version=1) */
static const char *k_v1 =
    "CREATE TABLE history (id INTEGER PRIMARY KEY, path TEXT NOT NULL, size INTEGER NOT NULL,"
    " duration REAL NOT NULL DEFAULT 0, position REAL NOT NULL DEFAULT 0, watched INTEGER NOT NULL DEFAULT 0,"
    " watch_count INTEGER NOT NULL DEFAULT 0, updated INTEGER NOT NULL DEFAULT 0, UNIQUE(path, size));"
    "CREATE INDEX history_size ON history(size);"
    "INSERT INTO history(path,size,duration,position,watched,watch_count,updated)"
    " VALUES('Show/S1/E1.mkv',1000,1400,700,0,0,111),('Movie.mkv',5000,7000,7000,1,1,222);"
    "PRAGMA user_version=1;";

int main(void)
{
    char err[256];
    HistoryEntry e;

    /* 1. a database from 0.1.x opens, keeps its data and its version */
    clean();
    sql(k_v1);
    History *h = history_open(DB, err, sizeof err);
    CHECK(h != NULL);
    CHECK(history_schema_version(h) == 1);
    CHECK(history_get(h, "Show/S1/E1.mkv", 1000, &e) && e.position == 700 && !e.watched);
    CHECK(history_get(h, "Movie.mkv", 5000, &e) && e.watched && e.watch_count == 1);
    CHECK(history_save(h, "Show/S1/E1.mkv", 1000, 800, 1400, 0.9, false));
    history_close(h);

    /* 2. a database from before versions were recorded (user_version 0) is treated as version 1 */
    clean();
    sql("BEGIN;" "CREATE TABLE history (id INTEGER PRIMARY KEY, path TEXT NOT NULL, size INTEGER NOT NULL,"
        " duration REAL NOT NULL DEFAULT 0, position REAL NOT NULL DEFAULT 0, watched INTEGER NOT NULL DEFAULT 0,"
        " watch_count INTEGER NOT NULL DEFAULT 0, updated INTEGER NOT NULL DEFAULT 0, UNIQUE(path, size));"
        "INSERT INTO history(path,size,position) VALUES('a.mkv',1,42); COMMIT;");
    h = history_open(DB, err, sizeof err);
    CHECK(h && history_get(h, "a.mkv", 1, &e) && e.position == 42);
    CHECK(history_schema_version(h) == 1);
    history_close(h);

    /* 3. migrating forward: version 1 -> 3, with a backup, keeping every row */
    clean();
    sql(k_v1);
    const char *const migs[] = {
        "ALTER TABLE history ADD COLUMN note TEXT;",                                  /* 1 -> 2 */
        "CREATE TABLE tags (path TEXT, tag TEXT); ALTER TABLE history ADD COLUMN rating INTEGER NOT NULL DEFAULT 0;",
        NULL
    };
    h = history_open_ex(DB, 3, migs, err, sizeof err);
    CHECK(h != NULL);
    CHECK(history_schema_version(h) == 3);
    CHECK(history_get(h, "Show/S1/E1.mkv", 1000, &e) && e.position == 700);
    CHECK(history_get(h, "Movie.mkv", 5000, &e) && e.watched);
    CHECK(exists(DB ".v1.bak"));                      /* the old file was kept */
    history_close(h);

    /* 4. an OLDER version (knows only version 1) opens that version-3 database, reads and writes it,
     *    and does not downgrade it or drop what it does not know */
    sql("UPDATE history SET note='keep me', rating=5 WHERE size=1000;");
    h = history_open(DB, err, sizeof err);
    CHECK(h != NULL);
    CHECK(history_schema_version(h) == 3);
    CHECK(history_get(h, "Show/S1/E1.mkv", 1000, &e) && e.position == 700);
    CHECK(history_save(h, "Show/S1/E1.mkv", 1000, 900, 1400, 0.9, false));       /* update */
    CHECK(history_save(h, "New/File.mkv", 77, 10, 100, 0.9, false));             /* insert */
    CHECK(history_set_watched(h, "Movie.mkv", 5000, false));
    HistoryRow rows[8];
    CHECK(history_in_progress(h, rows, 8) == 3 || history_in_progress(h, rows, 8) == 2);
    history_close(h);
    {
        sqlite3 *db;
        sqlite3_open(DB, &db);
        sqlite3_stmt *st;
        sqlite3_prepare_v2(db, "SELECT note, rating, position FROM history WHERE size=1000", -1, &st, NULL);
        CHECK(sqlite3_step(st) == SQLITE_ROW);
        CHECK_STR((const char *)sqlite3_column_text(st, 0), "keep me");
        CHECK(sqlite3_column_int(st, 1) == 5);
        CHECK(sqlite3_column_double(st, 2) == 900);
        sqlite3_finalize(st);
        sqlite3_close(db);
    }

    /* 5. a failing migration leaves the data untouched and reports the problem */
    clean();
    sql(k_v1);
    const char *const bad[] = { "THIS IS NOT SQL;", NULL };
    h = history_open_ex(DB, 2, bad, err, sizeof err);
    CHECK(h == NULL);
    CHECK(strstr(err, "unchanged") != NULL);
    h = history_open(DB, err, sizeof err);
    CHECK(h && history_schema_version(h) == 1 && history_get(h, "Movie.mkv", 5000, &e));
    history_close(h);

    /* 6. a brand-new database gets the current version */
    clean();
    h = history_open(DB, err, sizeof err);
    CHECK(h && history_schema_version(h) == 1);
    CHECK(history_save(h, "x.mkv", 1, 5, 10, 0.9, false));
    history_close(h);
    clean();
    TEST_DONE();
}
