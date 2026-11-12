#include "history.h"
#include "test_util.h"

#include <stdlib.h>

/* The library as the app would see it: the paths that exist right now. */
static const char *g_exists[16];
static int g_exists_n;
static bool exists_fn(void *ud, const char *rel)
{
    (void)ud;
    for (int i = 0; i < g_exists_n; i++)
        if (!strcmp(g_exists[i], rel)) return true;
    return false;
}

static History *fresh_db(const char *path)
{
    remove(path);
    char wal[512], shm[512];
    snprintf(wal, sizeof wal, "%s-wal", path);
    snprintf(shm, sizeof shm, "%s-shm", path);
    remove(wal);
    remove(shm);
    char err[256] = "";
    History *h = history_open(path, err, sizeof err);
    if (!h) fprintf(stderr, "open failed: %s\n", err);
    return h;
}

int main(void)
{
    const char *db = "thc_test_history.db";
    History *h = fresh_db(db);
    CHECK(h != NULL);
    if (!h) TEST_DONE();

    HistoryEntry e;
    const char *ep1 = "Show/Season 1/Episode 1.mkv";

    /* Unknown file. */
    CHECK(!history_get(h, ep1, 1000, &e));
    CHECK_NEAR(e.position, 0);

    /* Save a position and read it back. */
    CHECK(history_save(h, ep1, 1000, 120.0, 1400.0, 0.9, false));
    CHECK(history_get(h, ep1, 1000, &e));
    CHECK_NEAR(e.position, 120.0);
    CHECK_NEAR(e.duration, 1400.0);
    CHECK(!e.watched);
    CHECK_NEAR(history_resume_point(&e, 0.9), 120.0);
    CHECK(history_progress(&e) > 0.085 && history_progress(&e) < 0.086);

    /* Same path, different size: a different file. */
    CHECK(!history_get(h, ep1, 2000, &e));

    /* Passing the threshold marks it watched; resume starts over. */
    CHECK(history_save(h, ep1, 1000, 1300.0, 1400.0, 0.9, false));
    CHECK(history_get(h, ep1, 1000, &e));
    CHECK(e.watched);
    CHECK(e.watch_count == 1);
    CHECK_NEAR(history_resume_point(&e, 0.9), 0);

    /* Watched stays watched when rewatching from the start. */
    CHECK(history_save(h, ep1, 1000, 30.0, 1400.0, 0.9, false));
    CHECK(history_get(h, ep1, 1000, &e));
    CHECK(e.watched);
    CHECK_NEAR(e.position, 30.0);

    /* A custom threshold. */
    const char *mv = "Movies/Film.mkv";
    CHECK(history_save(h, mv, 5000, 700.0, 1000.0, 0.75, false));
    CHECK(history_get(h, mv, 5000, &e));
    CHECK(!e.watched);
    CHECK(history_save(h, mv, 5000, 760.0, 1000.0, 0.75, false));
    CHECK(history_get(h, mv, 5000, &e));
    CHECK(e.watched);

    /* Reaching the end always counts, even with an unknown duration. */
    CHECK(history_save(h, "clip.webm", 77, 3.0, 0, 0.9, true));
    CHECK(history_get(h, "clip.webm", 77, &e));
    CHECK(e.watched);

    /* #24: with no exists check installed nothing is ever treated as moved */
    const char *ep2 = "Show/Season 1/Episode 2.mkv";
    const char *ep2_moved = "Show (2019)/S01/02 - The Second One.mkv";
    CHECK(history_save(h, ep2, 3333, 600.0, 1500.0, 0.9, false));
    CHECK(!history_get(h, ep2_moved, 3333, &e));
    /* the old file still exists: a new file of the same size does not take its row */
    history_set_exists_check(h, exists_fn, NULL);
    g_exists[0] = ep2;
    g_exists_n = 1;
    CHECK(!history_get(h, ep2_moved, 3333, &e));
    CHECK(history_save(h, "Copy/Of Episode 2.mkv", 3333, 5.0, 1500.0, 0.9, false));   /* a plain copy */
    CHECK(history_get(h, ep2, 3333, &e));
    CHECK_NEAR(e.position, 600.0);                                                   /* the original keeps its row */
    CHECK(history_get(h, "Copy/Of Episode 2.mkv", 3333, &e));
    CHECK_NEAR(e.position, 5.0);
    CHECK(history_remove(h, "Copy/Of Episode 2.mkv", 3333));
    /* two same-size files, both played: each keeps its own row (the 0.2.0 album case) */
    g_exists[1] = "Album/a.mp3";
    g_exists[2] = "Album/b.mp3";
    g_exists_n = 3;
    CHECK(history_save(h, "Album/a.mp3", 4242, 10.0, 0, 0.9, false));
    CHECK(history_save(h, "Album/b.mp3", 4242, 20.0, 0, 0.9, false));
    CHECK(history_get(h, "Album/a.mp3", 4242, &e) && e.position == 10.0);
    CHECK(history_get(h, "Album/b.mp3", 4242, &e) && e.position == 20.0);
    CHECK(!history_get(h, "Album/c.mp3", 4242, &e));                                 /* a plain get moves nothing */
    CHECK(history_get(h, "Album/a.mp3", 4242, &e) && e.position == 10.0);
    /* marking a new same-size file watched does not take the other file's row either */
    CHECK(history_set_watched(h, "Album/c.mp3", 4242, true));
    CHECK(history_get(h, "Album/a.mp3", 4242, &e) && e.position == 10.0 && !e.watched);
    CHECK(history_remove(h, "Album/c.mp3", 4242));
    CHECK(history_remove(h, "Album/a.mp3", 4242) && history_remove(h, "Album/b.mp3", 4242));
    /* a real move: the old path is gone from the library, so the history follows */
    g_exists_n = 0;
    CHECK(history_get(h, ep2_moved, 3333, &e));
    CHECK_NEAR(e.position, 600.0);
    /* ...and saving under the new path re-keys the old entry. */
    CHECK(history_save(h, ep2_moved, 3333, 650.0, 1500.2, 0.9, false));
    CHECK(!history_get(h, "unrelated.mkv", 9999, &e));
    CHECK(history_get(h, ep2_moved, 3333, &e));
    CHECK_NEAR(e.position, 650.0);
    {
        HistoryRow rows[8];
        int n = history_in_progress(h, rows, 8);
        int seen_old = 0, seen_new = 0;
        for (int i = 0; i < n; i++) {
            if (!strcmp(rows[i].path, ep2)) seen_old++;
            if (!strcmp(rows[i].path, ep2_moved)) seen_new++;
        }
        CHECK(seen_old == 0);
        CHECK(seen_new == 1);
    }

    /* A same-size file with a clearly different duration is not a move. */
    CHECK(history_save(h, "Other/Thing.mkv", 3333, 10.0, 200.0, 0.9, false));
    CHECK(history_get(h, ep2_moved, 3333, &e));
    CHECK_NEAR(e.position, 650.0);
    CHECK(history_get(h, "Other/Thing.mkv", 3333, &e));
    CHECK_NEAR(e.position, 10.0);
    /* Two candidates of the same size: ambiguous, so no guess. */
    CHECK(!history_get(h, "Somewhere/Else.mkv", 3333, &e));

    /* In-progress list: unfinished, started entries only, newest first. */
    {
        HistoryRow rows[8];
        int n = history_in_progress(h, rows, 8);
        CHECK(n == 2);
        for (int i = 0; i < n; i++) CHECK(!rows[i].e.watched && rows[i].e.position > 0);
    }

    /* Manual watched toggle. */
    CHECK(history_set_watched(h, "Other/Thing.mkv", 3333, true));
    CHECK(history_get(h, "Other/Thing.mkv", 3333, &e));
    CHECK(e.watched);
    CHECK(history_set_watched(h, "Other/Thing.mkv", 3333, false));
    CHECK(history_get(h, "Other/Thing.mkv", 3333, &e));
    CHECK(!e.watched);
    CHECK_NEAR(e.position, 0);
    CHECK(history_set_watched(h, "Never/Played.mkv", 42, true));
    CHECK(history_get(h, "Never/Played.mkv", 42, &e));
    CHECK(e.watched);

    /* Persistence across reopen. */
    history_close(h);
    char err[128];
    h = history_open(db, err, sizeof err);
    CHECK(h != NULL);
    CHECK(history_get(h, ep1, 1000, &e));
    CHECK(e.watched);

    /* Clearing removes everything. */
    CHECK(history_clear(h));
    CHECK(!history_get(h, ep1, 1000, &e));
    history_close(h);
    remove(db);
    TEST_DONE();
}
