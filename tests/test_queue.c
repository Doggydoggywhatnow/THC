#include "queue.h"
#include "settings.h"
#include "test_util.h"

static QueueItem mk(const char *name, int kind)
{
    QueueItem it;
    memset(&it, 0, sizeof it);
    it.kind = kind;
    snprintf(it.title, sizeof it.title, "%s", name);
    snprintf(it.rel, sizeof it.rel, "Lib/%s.x", name);
    snprintf(it.path, sizeof it.path, "/media/Lib/%s.x", name);
    it.size = 100;
    return it;
}

static void fill(Queue *q, const char *names)
{
    for (const char *p = names; *p; p++) {
        char n[2] = { *p, 0 };
        QueueItem it = mk(n, QK_SONG);
        queue_add(q, &it, false);
    }
}

static void order(const Queue *q, char *out)
{
    for (int i = 0; i < q->n; i++) out[i] = q->items[i].title[0];
    out[q->n] = 0;
}

static bool resolve(void *ud, int root, int kind, const char *line, QueueItem *out)
{
    (void)ud;
    if (strstr(line, "missing")) return false;
    snprintf(out->rel, sizeof out->rel, "%s", line);
    snprintf(out->path, sizeof out->path, "/media/%s", line);
    out->root = root;
    if (kind >= 0) out->kind = kind;
    if (out->kind < 0) out->kind = QK_SONG;
    out->size = 1;
    return true;
}

int main(void)
{
    char o[64];
    Queue q, v;
    queue_init(&q, QUEUE_MUSIC);
    queue_init(&v, QUEUE_VIDEO);

    CHECK(queue_home_of(QK_VIDEO) == QUEUE_VIDEO && queue_home_of(QK_SONG) == QUEUE_MUSIC && queue_home_of(QK_BOOK) == QUEUE_MUSIC);

    /* add, insert, remove, move */
    fill(&q, "abcde");
    order(&q, o); CHECK_STR(o, "abcde");
    QueueItem x = mk("x", QK_SONG);
    CHECK(queue_insert(&q, 2, &x) == 2);
    order(&q, o); CHECK_STR(o, "abxcde");
    CHECK(queue_remove(&q, 2));
    order(&q, o); CHECK_STR(o, "abcde");
    CHECK(queue_move(&q, 0, 3));
    order(&q, o); CHECK_STR(o, "bcdae");
    CHECK(queue_move(&q, 4, 0));
    order(&q, o); CHECK_STR(o, "ebcda");
    CHECK(!queue_remove(&q, 9) && !queue_move(&q, 0, 9));

    /* the current item follows moves and removals */
    queue_clear(&q);
    fill(&q, "abcde");
    CHECK(queue_set_current(&q, 2));                      /* c */
    CHECK(queue_move(&q, 0, 4));                          /* a to the end */
    order(&q, o); CHECK_STR(o, "bcdea");
    CHECK(q.cur == 1);                                    /* still c */
    CHECK(queue_move(&q, 3, 0));                          /* e to the front */
    CHECK(q.cur == 2);
    CHECK(queue_remove(&q, 0));
    CHECK(q.cur == 1 && q.items[q.cur].title[0] == 'c');
    CHECK(queue_remove(&q, q.cur));                       /* remove the current: d takes its place */
    CHECK(q.items[q.cur].title[0] == 'd');
    queue_clear(&q);
    CHECK(q.n == 0 && q.cur == -1);

    /* add: end versus right after the current item */
    fill(&q, "abc");
    queue_set_current(&q, 0);
    QueueItem y = mk("y", QK_SONG), z = mk("z", QK_SONG);
    CHECK(queue_add(&q, &y, true) == 1);
    CHECK(queue_add(&q, &z, false) == 4);
    order(&q, o); CHECK_STR(o, "aybcz");
    CHECK(queue_upcoming(&q) == 4);

    /* next and previous with a backlog */
    queue_clear(&q);
    fill(&q, "abcd");
    CHECK(queue_next(&q) == 0);                           /* nothing was playing: start at the top */
    CHECK(queue_next(&q) == 1 && q.items[0].played);
    CHECK(queue_next(&q) == 2);
    CHECK(queue_played_count(&q) == 2);
    CHECK(queue_prev(&q, 10.0, true) == 2);               /* more than 3 s in: restart this one */
    CHECK(queue_prev(&q, 1.0, true) == 1);                /* otherwise go back */
    CHECK(!q.items[1].played && q.cur == 1);
    CHECK(queue_prev(&q, 1.0, false) == 0);
    CHECK(queue_prev(&q, 10.0, false) == -1);             /* nothing before the first, no restart wanted */
    queue_set_current(&q, 3);
    CHECK(queue_next(&q) == -1 && q.cur == 3);            /* the end: stay on the last one */

    /* repeat one and repeat all */
    q.repeat = REPEAT_ONE;
    queue_set_current(&q, 1);
    CHECK(queue_next(&q) == 1 && queue_next(&q) == 1);
    q.repeat = REPEAT_ALL;
    queue_set_current(&q, 3);
    CHECK(queue_next(&q) == 0 && queue_played_count(&q) == 0);
    q.repeat = REPEAT_OFF;

    /* shuffle keeps the current item and the backlog, and shuffles the rest */
    queue_clear(&q);
    fill(&q, "abcdefghijklmnopqrst");
    queue_set_current(&q, 5);                             /* f */
    char before[64];
    order(&q, before);
    queue_shuffle_upcoming(&q, 12345);
    order(&q, o);
    CHECK(!strncmp(o, before, 6));                        /* abcdef untouched */
    CHECK(strcmp(o + 6, before + 6) != 0);                /* the rest changed */
    char s1[64], s2[64];
    strcpy(s1, o + 6);
    strcpy(s2, before + 6);
    for (char *a = s1; *a; a++) for (char *b = a + 1; *b; b++) if (*b < *a) { char t = *a; *a = *b; *b = t; }
    CHECK_STR(s1, s2);                                    /* same items, nothing lost or duplicated */
    CHECK(q.cur == 5);

    /* moving between queues */
    queue_clear(&q);
    queue_clear(&v);
    QueueItem vid = mk("vid", QK_VIDEO);
    queue_add(&q, &vid, false);
    fill(&q, "ab");
    CHECK(queue_transfer(&q, 0, &v, false) == 0);
    CHECK(q.n == 2 && v.n == 1 && v.items[0].kind == QK_VIDEO);
    CHECK(queue_transfer(&q, 5, &v, false) == -1);

    /* chaining runs once and cannot loop */
    int chain[QUEUE_COUNT] = { CHAIN_OTHER, CHAIN_OTHER };
    bool used = false;
    CHECK(queue_chain_next(QUEUE_MUSIC, chain, &used) == QUEUE_VIDEO && used);
    CHECK(queue_chain_next(QUEUE_VIDEO, chain, &used) == -1);     /* the other way back: not again */
    used = false;                                                  /* the user started playback again */
    CHECK(queue_chain_next(QUEUE_VIDEO, chain, &used) == QUEUE_MUSIC);
    chain[QUEUE_MUSIC] = CHAIN_NONE;
    used = false;
    CHECK(queue_chain_next(QUEUE_MUSIC, chain, &used) == -1 && !used);

    /* capacity: at least 5000 items */
    Queue big;
    queue_init(&big, QUEUE_MUSIC);
    QueueItem it = mk("b", QK_SONG);
    int ok = 0;
    for (int i = 0; i < 6000; i++) ok += queue_add(&big, &it, false) >= 0;
    CHECK(ok == 6000 && big.n == 6000);
    queue_free(&big);

    /* M3U8 round trip, relative and absolute, unicode, missing files */
    queue_clear(&q);
    QueueItem a1 = mk("Caf\xC3\xA9 d\xC3\xA9j\xC3\xA0", QK_SONG), a2 = mk("Book", QK_BOOK), a3 = mk("Episode", QK_VIDEO);
    a1.root = 1; a2.root = 2; a3.root = 0;
    snprintf(a1.rel, sizeof a1.rel, "Music/B\xC3\xB6rk/Caf\xC3\xA9.flac");
    queue_add(&q, &a1, false);
    queue_add(&q, &a2, false);
    queue_add(&q, &a3, false);
    FILE *f = fopen("thc_test_queue.m3u8", "wb");
    CHECK(queue_export_m3u8(&q, f, true));
    fclose(f);
    f = fopen("thc_test_queue.m3u8", "rb");
    char text[4096];
    size_t nr = fread(text, 1, sizeof text - 1, f);
    text[nr] = 0;
    fclose(f);
    remove("thc_test_queue.m3u8");
    CHECK(strstr(text, "#THC-QUEUE:music") && strstr(text, "#THC-KIND:audiobook") && strstr(text, "B\xC3\xB6rk/Caf\xC3\xA9.flac"));
    CHECK(!strstr(text, "/media/Lib/"));                  /* relative export has no absolute paths */
    Queue r;
    queue_init(&r, QUEUE_MUSIC);
    QueueImport imp;
    queue_import_m3u8(&r, text, resolve, NULL, &imp);
    CHECK(imp.home == QUEUE_MUSIC && imp.added == 3 && imp.missing == 0);
    CHECK(r.items[0].kind == QK_SONG && r.items[1].kind == QK_BOOK && r.items[2].kind == QK_VIDEO);
    CHECK(r.items[0].root == 1 && !strcmp(r.items[0].title, "Caf\xC3\xA9 d\xC3\xA9j\xC3\xA0"));   /* order and details kept */
    f = tmpfile();
    CHECK(queue_export_m3u8(&q, f, false));
    fclose(f);
    queue_clear(&r);
    queue_import_m3u8(&r, "#EXTM3U\n#THC-QUEUE:video\nOne.mkv\nmissing-one.mkv\n\nTwo.mkv\nmissing-two.mkv\n", resolve, NULL, &imp);
    CHECK(imp.home == QUEUE_VIDEO && imp.added == 2 && imp.missing == 2);
    CHECK_STR(imp.missing_names[0], "missing-one.mkv");
    queue_clear(&r);
    queue_import_m3u8(&r, "\xEF\xBB\xBF#EXTM3U\r\nPlain.mp3\r\n", resolve, NULL, &imp);     /* a plain M3U with a BOM and CRLF */
    CHECK(imp.home == -1 && imp.added == 1 && r.n == 1 && !strcmp(r.items[0].rel, "Plain.mp3"));
    queue_free(&r);
    queue_free(&q);
    queue_free(&v);
    TEST_DONE();
}
