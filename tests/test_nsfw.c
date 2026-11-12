#include "library.h"
#include "nsfw.h"
#include "pin.h"
#include "queue.h"
#include "settings.h"
#include "test_util.h"

#include <stdlib.h>

static bool g_alive[8];       /* which of "gone.mkv"... still exist, for the moved-file rule */
static bool exists_fn(void *ud, int root, const char *rel)
{
    (void)ud; (void)root;
    return !strcmp(rel, "Shows/Old/ep.mkv") ? g_alive[0] : false;
}

static bool hidden_cb(void *ud, const FsNode *n)
{
    char rel[512];
    fs_relpath(n, rel, sizeof rel);
    return nsfw_hidden((const NsfwIndex *)ud, 0, rel, n->dir, n->size, NULL);
}

struct PCtx { NsfwIndex *x; };
static bool nsfw_queue_pred(void *ud, const QueueItem *it)
{
    return nsfw_hidden(((struct PCtx *)ud)->x, it->root, it->rel, false, it->size, NULL);
}

static NsfwIndex *make(bool enabled)
{
    NsfwIndex *x = nsfw_index_new();
    NsfwConfig c = { 0 };
    c.enabled = enabled;
    nsfw_index_config(x, &c);
    return x;
}

static int count_files(const FsNode *n)
{
    if (!n->dir) return 1;
    int c = 0;
    for (int i = 0; i < n->nkids; i++) c += count_files(n->kids[i]);
    return c;
}

int main(void)
{
    /* off by default: nothing is hidden, even with tags in the database */
    NsfwIndex *x = make(false);
    nsfw_tag(x, NULL, 0, NSFW_SCOPE_FOLDER, "Shows/Secret", 0, 1);
    CHECK(!nsfw_hidden(x, 0, "Shows/Secret/S01/e1.mkv", false, 10, NULL));
    NsfwConfig on = { .enabled = true };
    nsfw_index_config(x, &on);

    /* a file tag (path + size), and a folder tag that covers everything inside, also files added later */
    int why = 0;
    CHECK(nsfw_hidden(x, 0, "Shows/Secret/S01/e1.mkv", false, 10, &why) && why == NSFW_WHY_FOLDER);
    CHECK(nsfw_hidden(x, 0, "Shows/Secret/S09/brand-new.mkv", false, 77, NULL));          /* added later */
    CHECK(nsfw_hidden(x, 0, "Shows/Secret", true, 0, &why) && why == NSFW_WHY_ITEM);
    CHECK(!nsfw_hidden(x, 0, "Shows/Other/e1.mkv", false, 10, NULL));
    CHECK(!nsfw_hidden(x, 0, "Shows/SecretNot/e1.mkv", false, 10, NULL));                 /* a prefix is not a parent */
    CHECK(!nsfw_hidden(x, 1, "Shows/Secret/S01/e1.mkv", false, 10, NULL));                /* another root */
    nsfw_tag(x, NULL, 0, NSFW_SCOPE_FILE, "Movies/film.mkv", 500, 1);
    CHECK(nsfw_hidden(x, 0, "Movies/film.mkv", false, 500, &why) && why == NSFW_WHY_ITEM);
    CHECK(!nsfw_hidden(x, 0, "Movies/film.mkv", false, 501, NULL));                       /* a different file of that name */

    /* an explicit "not NSFW" beats an inherited tag; nearest wins; untagging goes back to inheriting */
    nsfw_tag(x, NULL, 0, NSFW_SCOPE_FILE, "Shows/Secret/S01/e2.mkv", 20, 0);
    CHECK(!nsfw_hidden(x, 0, "Shows/Secret/S01/e2.mkv", false, 20, &why) && why == NSFW_WHY_EXPLICIT_NOT);
    CHECK(nsfw_hidden(x, 0, "Shows/Secret/S01/e1.mkv", false, 10, NULL));
    nsfw_tag(x, NULL, 0, NSFW_SCOPE_FOLDER, "Shows/Secret/Fine", 0, 0);
    CHECK(!nsfw_hidden(x, 0, "Shows/Secret/Fine/e1.mkv", false, 5, NULL));
    nsfw_tag(x, NULL, 0, NSFW_SCOPE_FOLDER, "Shows/Secret/Fine/Again", 0, 1);              /* and back to NSFW below it */
    CHECK(nsfw_hidden(x, 0, "Shows/Secret/Fine/Again/e.mkv", false, 5, NULL));
    CHECK(nsfw_untag(x, NULL, 0, NSFW_SCOPE_FILE, "Shows/Secret/S01/e2.mkv", 20));
    CHECK(nsfw_hidden(x, 0, "Shows/Secret/S01/e2.mkv", false, 20, NULL));
    CHECK(!nsfw_untag(x, NULL, 0, NSFW_SCOPE_FILE, "no/such.mkv", 1));
    /* a file inside two NSFW folders is just NSFW */
    nsfw_tag(x, NULL, 0, NSFW_SCOPE_FOLDER, "Shows/Secret/S01", 0, 1);
    CHECK(nsfw_hidden(x, 0, "Shows/Secret/S01/e1.mkv", false, 10, &why) && why == NSFW_WHY_FOLDER);

    /* rules: names and patterns, nesting, never over an explicit "not NSFW" */
    NsfwIndex *r = make(true);
    NsfwConfig rc = { .enabled = true, .rules_enabled = true };
    snprintf(rc.rules, sizeof rc.rules, "NSFW | *xxx* |adult?");
    nsfw_index_config(r, &rc);
    CHECK(nsfw_hidden(r, 0, "Videos/nsfw/a.mkv", false, 1, &why) && why == NSFW_WHY_RULE);   /* case-insensitive */
    CHECK(nsfw_hidden(r, 0, "Videos/My XXX Collection/a/b/c.mkv", false, 1, NULL));         /* nested, pattern */
    CHECK(nsfw_hidden(r, 0, "adult1/a.mkv", false, 1, NULL));
    CHECK(!nsfw_hidden(r, 0, "adultss/a.mkv", false, 1, NULL));                             /* ? is exactly one letter */
    CHECK(!nsfw_hidden(r, 0, "Videos/Normal/a.mkv", false, 1, NULL));
    CHECK(nsfw_hidden(r, 0, "Videos/Normal/xxx.mkv", false, 1, NULL));                      /* a file name counts, too */
    nsfw_tag(r, NULL, 0, NSFW_SCOPE_FILE, "Videos/nsfw/ok.mkv", 9, 0);
    CHECK(!nsfw_hidden(r, 0, "Videos/nsfw/ok.mkv", false, 9, &why) && why == NSFW_WHY_EXPLICIT_NOT);
    nsfw_tag(r, NULL, 0, NSFW_SCOPE_FOLDER, "Videos/xxx-clean", 0, 0);
    CHECK(!nsfw_hidden(r, 0, "Videos/xxx-clean/a.mkv", false, 1, NULL));                    /* a "not" folder beats the rule */
    rc.rules_enabled = false;
    nsfw_index_config(r, &rc);
    CHECK(!nsfw_hidden(r, 0, "Videos/nsfw/a.mkv", false, 1, NULL));                         /* rules are off by default */
    CHECK(nsfw_pattern_match("*", "anything") && nsfw_pattern_match("a*c", "abbbc") && !nsfw_pattern_match("a*c", "abbbd"));
    CHECK(!nsfw_rules_match("", "a/b") && !nsfw_rules_match("x", NULL));

    /* a whole library folder */
    NsfwConfig mc = { .enabled = true, .root_mask = 1u << 2 };
    nsfw_index_config(r, &mc);
    CHECK(nsfw_hidden(r, 2, "Anyone/book.m4b", false, 1, &why) && why == NSFW_WHY_LIBRARY);
    CHECK(!nsfw_hidden(r, 1, "Anyone/song.mp3", false, 1, NULL));
    nsfw_tag(r, NULL, 2, NSFW_SCOPE_FILE, "Anyone/ok.m4b", 3, 0);
    CHECK(!nsfw_hidden(r, 2, "Anyone/ok.m4b", false, 3, NULL));                             /* explicit "not" wins here, too */

    /* a file tag follows a moved file only when the old path is gone (the #24 rule) */
    NsfwIndex *m = make(true);
    nsfw_tag(m, NULL, 0, NSFW_SCOPE_FILE, "Shows/Old/ep.mkv", 4242, 1);
    CHECK(!nsfw_hidden(m, 0, "Shows/New/ep.mkv", false, 4242, NULL));                       /* no check: never follows */
    nsfw_index_set_exists_check(m, exists_fn, NULL);
    g_alive[0] = true;                                                                      /* the old file still exists: a copy */
    CHECK(!nsfw_hidden(m, 0, "Shows/New/ep.mkv", false, 4242, NULL));
    CHECK(nsfw_hidden(m, 0, "Shows/Old/ep.mkv", false, 4242, NULL));
    g_alive[0] = false;                                                                     /* a real move */
    CHECK(nsfw_hidden(m, 0, "Shows/New/ep.mkv", false, 4242, NULL));
    CHECK(!nsfw_hidden(m, 0, "Shows/New/other.mkv", false, 4243, NULL));                    /* another size: no */
    nsfw_tag(m, NULL, 0, NSFW_SCOPE_FILE, "Shows/New/ep.mkv", 4242, 1);                     /* saving re-keys the tag */
    CHECK(nsfw_tag_count(m) == 1);

    /* filtered trees: the normal portals never see NSFW files, the NSFW portal only does */
    NsfwIndex *f = make(true);
    nsfw_tag(f, NULL, 0, NSFW_SCOPE_FOLDER, "Shows/Secret", 0, 1);
    nsfw_tag(f, NULL, 0, NSFW_SCOPE_FOLDER, "Shows/Mixed/Season 2", 0, 1);                  /* one NSFW season in a normal show */
    nsfw_tag(f, NULL, 0, NSFW_SCOPE_FILE, "Movies/Film B.mkv", 800, 1);
    FsNode *root = fs_new("lib", true);
    fs_add_path(root, "Shows/Secret/S01/e1.mkv", 10, 1);
    fs_add_path(root, "Shows/Secret/S01/e2.mkv", 11, 1);
    fs_add_path(root, "Shows/Secret/poster.jpg", 5, 1);
    fs_add_path(root, "Shows/Mixed/Season 1/e1.mkv", 20, 1);
    fs_add_path(root, "Shows/Mixed/Season 2/e1.mkv", 21, 1);
    fs_add_path(root, "Shows/Plain/S01/e1.mkv", 30, 1);
    fs_add_path(root, "Movies/Film A.mkv", 700, 1);
    fs_add_path(root, "Movies/Film B.mkv", 800, 1);
    fs_add_path(root, "Empty/kept", 0, 1)->dir = true;
    fs_sort(root, true);
    int total = count_files(root);
    FsNode *normal = fs_filter(root, hidden_cb, f, false);
    FsNode *adult = fs_filter(root, hidden_cb, f, true);
    CHECK(normal && adult);
    CHECK(!fs_find(normal, "Shows/Secret"));                                                 /* the whole show is gone */
    CHECK(!fs_find(normal, "Shows/Secret/S01/e1.mkv") && !fs_find(normal, "Shows/Secret/poster.jpg"));
    CHECK(fs_find(normal, "Shows/Mixed/Season 1/e1.mkv") && !fs_find(normal, "Shows/Mixed/Season 2"));   /* the show stays */
    CHECK(fs_find(normal, "Shows/Plain/S01/e1.mkv") && fs_find(normal, "Movies/Film A.mkv") && !fs_find(normal, "Movies/Film B.mkv"));
    CHECK(fs_find(normal, "Empty/kept"));                                                    /* an empty normal folder is kept */
    CHECK(fs_find(adult, "Shows/Secret/S01/e1.mkv") && fs_find(adult, "Shows/Secret/poster.jpg"));
    CHECK(fs_find(adult, "Shows/Mixed/Season 2/e1.mkv") && !fs_find(adult, "Shows/Mixed/Season 1"));
    CHECK(fs_find(adult, "Movies/Film B.mkv") && !fs_find(adult, "Movies/Film A.mkv") && !fs_find(adult, "Shows/Plain"));
    CHECK(count_files(normal) + count_files(adult) == total);                                /* every file is in exactly one tree */
    /* the library built from the normal tree has no trace of the NSFW show, and the NSFW one has only it */
    LibraryRoot lr = { normal, 0, -1 };
    VideoLibrary lib;
    library_build(&lib, &lr, 1, NULL);
    bool leak = false;
    for (int i = 0; i < lib.nshows; i++) leak |= strstr(lib.shows[i].title, "Secret") != NULL;
    for (int i = 0; i < lib.nmovies; i++) leak |= strstr(lib.movies[i].title, "Film B") != NULL;
    CHECK(!leak);
    library_clear(&lib);
    LibraryRoot nr = { adult, 0, -1 };
    library_build(&lib, &nr, 1, NULL);
    bool only = lib.nmovies >= 1;
    for (int i = 0; i < lib.nmovies; i++) only &= strstr(lib.movies[i].title, "Film B") != NULL;
    CHECK(only);
    library_clear(&lib);
    /* switching the portal off puts everything back (the caller rebuilds from the full tree) */
    NsfwConfig off = { .enabled = false };
    nsfw_index_config(f, &off);
    FsNode *back = fs_filter(root, hidden_cb, f, false);
    CHECK(count_files(back) == total);
    fs_free(back);
    fs_free(normal); fs_free(adult); fs_free(root);

    /* the database: tags survive a reload, with the history compatibility of schema 3 */
    remove("nsfw_test.db");
    char err[200];
    History *h = history_open("nsfw_test.db", err, sizeof err);
    NsfwIndex *d = make(true);
    CHECK(h && nsfw_tag(d, h, 0, NSFW_SCOPE_FOLDER, "Shows/Secret", 0, 1) && nsfw_tag(d, h, 1, NSFW_SCOPE_FILE, "a/b.flac", 99, 1));
    CHECK(nsfw_tag(d, h, 0, NSFW_SCOPE_FOLDER, "Shows/Fine", 0, 0));
    CHECK(nsfw_tag_count(d) == 3 && nsfw_tag_count_state(d, 0) == 1 && nsfw_tag_count_state(d, 1) == 2);
    CHECK(nsfw_untag(d, h, 0, NSFW_SCOPE_FOLDER, "Shows/Fine", 0));
    if (h) history_close(h);
    h = history_open("nsfw_test.db", err, sizeof err);
    NsfwIndex *d2 = make(true);
    CHECK(nsfw_index_load(d2, h) && nsfw_tag_count(d2) == 2);
    CHECK(nsfw_hidden(d2, 0, "Shows/Secret/x.mkv", false, 1, NULL) && nsfw_hidden(d2, 1, "a/b.flac", false, 99, NULL));
    /* export and import of the tag list */
    FILE *ef = fopen("nsfw_export.txt", "wb");
    CHECK(ef && nsfw_export(d2, ef) == 2);
    if (ef) fclose(ef);
    NsfwIndex *d3 = make(true);
    ef = fopen("nsfw_export.txt", "rb");
    CHECK(ef && nsfw_import(d3, NULL, ef) == 2);
    if (ef) fclose(ef);
    CHECK(nsfw_hidden(d3, 0, "Shows/Secret/x.mkv", false, 1, NULL) && nsfw_hidden(d3, 1, "a/b.flac", false, 99, NULL));
    /* an import with path tricks is refused line by line */
    ef = fopen("nsfw_export.txt", "wb");
    if (ef) {
        fputs("THC NSFW tags 1\n0\td\t1\t0\t../../etc\n0\td\t1\t0\t/abs\n0\tf\t1\t5\ta\\b\n0\td\t1\t0\tfine/folder\n9\tz\t1\t0\tx\n", ef);
        fclose(ef);
    }
    NsfwIndex *d4 = make(true);
    ef = fopen("nsfw_export.txt", "rb");
    CHECK(ef && nsfw_import(d4, NULL, ef) == 1);
    if (ef) fclose(ef);
    CHECK(nsfw_tag_count(d4) == 1);
    if (h) history_close(h);
    remove("nsfw_test.db"); remove("nsfw_export.txt");
    nsfw_index_free(x); nsfw_index_free(r); nsfw_index_free(m); nsfw_index_free(f);
    nsfw_index_free(d); nsfw_index_free(d2); nsfw_index_free(d3); nsfw_index_free(d4);

    /* a large library stays fast: 50,000 lookups against 2,000 tags */
    NsfwIndex *big = make(true);
    for (int i = 0; i < 2000; i++) {
        char p[64];
        snprintf(p, sizeof p, "Lib/Folder %d", i);
        nsfw_tag(big, NULL, 0, NSFW_SCOPE_FOLDER, p, 0, 1);
    }
    int hits = 0;
    for (int i = 0; i < 50000; i++) {
        char p[96];
        snprintf(p, sizeof p, "Lib/Folder %d/Sub/file %d.mkv", i % 4000, i);
        hits += nsfw_hidden(big, 0, p, false, i, NULL);
    }
    CHECK(hits == 26000);
    nsfw_index_free(big);

    /* ---- 6.5 leaks ---- */
    NsfwIndex *L = make(true);
    nsfw_tag(L, NULL, 0, NSFW_SCOPE_FOLDER, "Shows/Secret", 0, 1);
    nsfw_tag(L, NULL, 1, NSFW_SCOPE_FOLDER, "Artist/Hidden Album", 0, 1);
    /* history, plays and backlog: not recorded for NSFW items unless the user turned it on */
    CHECK(!nsfw_record_allowed(L, true, false, 0, "Shows/Secret/e1.mkv", 10));
    CHECK(nsfw_record_allowed(L, true, true, 0, "Shows/Secret/e1.mkv", 10));          /* the user's setting */
    CHECK(nsfw_record_allowed(L, true, false, 0, "Shows/Plain/e1.mkv", 10));          /* normal items are unaffected */
    CHECK(!nsfw_record_allowed(L, false, true, 0, "Shows/Plain/e1.mkv", 10));         /* the portal's own history switch still rules */
    CHECK(nsfw_record_allowed(NULL, true, false, 0, "x.mkv", 1));
    NsfwConfig offc = { .enabled = false };
    nsfw_index_config(L, &offc);
    CHECK(nsfw_record_allowed(L, true, false, 0, "Shows/Secret/e1.mkv", 10));          /* portal off: nothing special */
    nsfw_index_config(L, &on);
    /* thumbnails are cached in a folder of their own */
    char tn[40];
    nsfw_thumb_name("0123456789abcdef", false, tn, sizeof tn);
    CHECK_STR(tn, "0123456789abcdef");
    nsfw_thumb_name("0123456789abcdef", true, tn, sizeof tn);
    CHECK_STR(tn, "nsfw/0123456789abcdef");
    /* panic: every NSFW item leaves the queue, the played ones above the current item and the upcoming ones below */
    {
        Queue q;
        queue_init(&q, QUEUE_MUSIC);
        const char *paths[] = { "Artist/Fine/a.mp3", "Artist/Hidden Album/b.mp3", "Artist/Fine/c.mp3", "Artist/Hidden Album/d.mp3",
                                "Artist/Fine/e.mp3", "Artist/Hidden Album/f.mp3" };
        for (int i = 0; i < 6; i++) {
            QueueItem it;
            memset(&it, 0, sizeof it);
            it.kind = QK_SONG;
            it.root = 1;
            snprintf(it.rel, sizeof it.rel, "%s", paths[i]);
            it.size = 100 + i;
            queue_add(&q, &it, false);
        }
        queue_set_current(&q, 3);                                   /* d.mp3 is "playing": an NSFW item */
        struct PCtx pc = { L };
        /* a panic stops the item first, so it too is removed (keep = -1) */
        int gone = queue_remove_if(&q, nsfw_queue_pred, &pc, -1);
        CHECK(gone == 3 && q.n == 3);
        for (int i = 0; i < q.n; i++) CHECK(!strstr(q.items[i].rel, "Hidden"));
        CHECK(q.cur >= -1 && q.cur < q.n);
        CHECK(queue_remove_if(&q, nsfw_queue_pred, &pc, -1) == 0 && q.n == 3);   /* pressing it again changes nothing */
        /* without a stop, an NSFW item that keeps playing is kept (masked) */
        queue_free(&q);
        queue_init(&q, QUEUE_MUSIC);
        for (int i = 0; i < 6; i++) {
            QueueItem it;
            memset(&it, 0, sizeof it);
            it.kind = QK_SONG;
            it.root = 1;
            snprintf(it.rel, sizeof it.rel, "%s", paths[i]);
            queue_add(&q, &it, false);
        }
        queue_set_current(&q, 3);
        CHECK(queue_remove_if(&q, nsfw_queue_pred, &pc, 3) == 2 && q.n == 4);
        CHECK(q.cur >= 0 && strstr(q.items[q.cur].rel, "d.mp3"));
        queue_free(&q);
    }
    /* the PIN is never in settings.ini; only the salted hash is */
    {
        static Settings st;
        settings_defaults(&st);
        uint8_t salt[PIN_SALT_BYTES] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6 };
        pin_make("hunter2-secret", salt, st.nsfw_pin_salt, st.nsfw_pin_hash);
        CHECK(settings_save(&st, "nsfw_pin_test.ini"));
        char buf[20000] = "";
        FILE *pf = fopen("nsfw_pin_test.ini", "rb");
        if (pf) { size_t got = fread(buf, 1, sizeof buf - 1, pf); buf[got] = 0; fclose(pf); }
        CHECK(!strstr(buf, "hunter2") && strstr(buf, st.nsfw_pin_hash) && strstr(buf, st.nsfw_pin_salt));
        static Settings back;
        settings_defaults(&back);
        CHECK(settings_load(&back, "nsfw_pin_test.ini") && pin_verify("hunter2-secret", back.nsfw_pin_salt, back.nsfw_pin_hash));
        CHECK(!pin_verify("hunter3", back.nsfw_pin_salt, back.nsfw_pin_hash));
        remove("nsfw_pin_test.ini");
    }
    nsfw_index_free(L);
    TEST_DONE();
}
