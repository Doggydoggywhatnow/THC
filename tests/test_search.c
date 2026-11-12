#include "search.h"
#include "test_util.h"

#include <stdlib.h>
#include <time.h>

static void norm(const char *in, const char *want)
{
    char o[256];
    search_normalize(in, o, sizeof o);
    if (strcmp(o, want)) { fprintf(stderr, "normalize(\"%s\") = \"%s\", want \"%s\"\n", in, o, want); g_failures++; }
    g_checks++;
}

static int first_is(SearchIndex *x, const char *q, int kind, const char *name)
{
    SearchHit h[8];
    int n = search_query(x, q, kind, SCOPE_FILTER_ALL, h, 8);
    return n > 0 && !strcmp(x->e[h[0].entry].name, name);
}

int main(void)
{
    /* normalization: case, accents, punctuation, unicode */
    norm("Beyonc\xC3\xA9", "beyonce");
    norm("  Mot\xC3\xB6rhead  ", "motorhead");
    norm("AC/DC", "ac dc");
    norm("Don't Stop!!! (Live)", "don t stop live");
    norm("Sigur R\xC3\xB3s", "sigur ros");
    norm("\xC5\x81\xC3\xB3" "d\xC5\xBA", "lodz");
    norm("\xD0\x9C\xD0\xB5\xD0\xBB\xD0\xBE\xD0\xB4\xD0\xB8\xD1\x8F", "\xD0\xBC\xD0\xB5\xD0\xBB\xD0\xBE\xD0\xB4\xD0\xB8\xD1\x8F");   /* Cyrillic */
    norm("\xE5\x91\xA8\xE6\x9D\xB0\xE5\x80\xAB", "\xE5\x91\xA8\xE6\x9D\xB0\xE5\x80\xAB");                                                /* CJK kept */
    norm("---", "");
    norm("", "");

    CHECK(search_within_one_edit("beyonse", "beyonce") && search_within_one_edit("beyonc", "beyonce"));
    CHECK(search_within_one_edit("beyoncee", "beyonce") && search_within_one_edit("beyoncc", "beyonce") == true);
    CHECK(search_within_one_edit("bayonse", "beyonce") == false);          /* two edits */
    CHECK(search_within_one_edit("teh", "the"));                           /* swapped letters */
    CHECK(!search_within_one_edit("abc", "xyz"));

    SearchIndex x;
    search_index_init(&x);
    search_index_add(&x, SK_ARTIST, "Beyonc\xC3\xA9", NULL, NULL);
    search_index_add(&x, SK_ARTIST, "Bey", NULL, NULL);
    search_index_add(&x, SK_ARTIST, "The Beatles", NULL, NULL);
    search_index_add(&x, SK_ARTIST, "Radiohead", NULL, NULL);
    search_index_add(&x, SK_ALBUM, "Lemonade", "Beyonc\xC3\xA9", NULL);
    search_index_add(&x, SK_ALBUM, "OK Computer", "Radiohead", NULL);
    search_index_add(&x, SK_SONG, "Halo", "Beyonc\xC3\xA9 I Am... Sasha Fierce", NULL);
    search_index_add(&x, SK_SONG, "Karma Police", "Radiohead OK Computer", NULL);
    search_index_add(&x, SK_SONG, "Paranoid Android", "Radiohead OK Computer", NULL);
    search_index_add(&x, SK_SONG, "Let It Be", "The Beatles", NULL);

    /* accent and case insensitive */
    CHECK(first_is(&x, "BEYONCE", SK_ARTIST, "Beyonc\xC3\xA9"));
    CHECK(first_is(&x, "beyoncé", SK_ARTIST, "Beyonc\xC3\xA9"));
    /* ranking: exact, then prefix, then word start, then substring */
    CHECK(first_is(&x, "bey", SK_ARTIST, "Bey"));                          /* exact beats prefix */
    CHECK(first_is(&x, "beyo", SK_ARTIST, "Beyonc\xC3\xA9"));              /* prefix */
    CHECK(first_is(&x, "beatles", SK_ARTIST, "The Beatles"));              /* word start */
    CHECK(first_is(&x, "diohea", SK_ARTIST, "Radiohead"));                 /* substring */
    SearchHit h[8];
    int n = search_query(&x, "bey", SK_ARTIST, SCOPE_FILTER_ALL, h, 8);
    CHECK(n == 2 && !strcmp(x.e[h[0].entry].name, "Bey") && !strcmp(x.e[h[1].entry].name, "Beyonc\xC3\xA9"));
    /* the artist of a song is searchable, but a match in the name ranks higher */
    CHECK(first_is(&x, "radiohead", SK_SONG, "Karma Police") || first_is(&x, "radiohead", SK_SONG, "Paranoid Android"));
    n = search_query(&x, "radiohead", SK_SONG, SCOPE_FILTER_ALL, h, 8);
    CHECK(n == 2);
    CHECK(first_is(&x, "halo", SK_SONG, "Halo"));
    CHECK(first_is(&x, "ok comp", SK_ALBUM, "OK Computer"));               /* more than one word */
    CHECK(first_is(&x, "computer ok", SK_ALBUM, "OK Computer"));           /* any order */
    /* typo tolerance: one edit, words of 4 letters or more */
    CHECK(first_is(&x, "beyonse", SK_ARTIST, "Beyonc\xC3\xA9"));
    CHECK(first_is(&x, "radiohed", SK_ARTIST, "Radiohead"));
    CHECK(first_is(&x, "lemonad", SK_ALBUM, "Lemonade"));
    CHECK(search_query(&x, "hxxo", SK_SONG, SCOPE_FILTER_ALL, h, 8) == 0);   /* 4 letters, but two edits */
    CHECK(search_query(&x, "bex", SK_ARTIST, SCOPE_FILTER_ALL, h, 8) == 0);  /* short words get no typo tolerance */
    CHECK(search_query(&x, "zzzz", SK_ARTIST, SCOPE_FILTER_ALL, h, 8) == 0);
    /* scope */
    CHECK(search_query(&x, "radiohead", SK_ARTIST, SCOPE_FILTER_SONGS, h, 8) == 0);
    CHECK(search_query(&x, "radiohead", SK_SONG, SCOPE_FILTER_SONGS, h, 8) == 2);
    CHECK(search_query(&x, "radiohead", SK_ALBUM, SCOPE_FILTER_ARTISTS, h, 8) == 0);
    CHECK(search_query(&x, "radiohead", SK_ARTIST, SCOPE_FILTER_ARTISTS, h, 8) == 1);
    /* limits and empty queries */
    CHECK(search_query(&x, "e", SK_SONG, SCOPE_FILTER_ALL, h, 1) <= 1);
    CHECK(search_query(&x, "", SK_ARTIST, SCOPE_FILTER_ALL, h, 8) == 0);
    CHECK(search_query(&x, "   !!! ", SK_ARTIST, SCOPE_FILTER_ALL, h, 8) == 0);
    CHECK(search_query(&x, "bey", SK_ARTIST, SCOPE_FILTER_ALL, h, 0) == 0);
    search_index_clear(&x);

    /* a big library stays responsive: 50,000 tracks, timed per keystroke */
    search_index_init(&x);
    static const char *const w[] = { "love", "night", "blue", "river", "fire", "dream", "star", "ocean", "stone", "light",
                                     "road", "heart", "gold", "shadow", "wind", "silver", "storm", "garden", "paper", "mirror" };
    char name[96], extra[96];
    for (int i = 0; i < 50000; i++) {
        snprintf(name, sizeof name, "%s %s %d", w[i % 20], w[(i / 20) % 20], i);
        snprintf(extra, sizeof extra, "Artist %s %d Album %s", w[(i / 7) % 20], i / 500, w[(i / 31) % 20]);
        search_index_add(&x, SK_SONG, name, extra, NULL);
    }
    CHECK(x.n == 50000);
    const char *typed[] = { "d", "dr", "dre", "drea", "dream", "dream s", "dream st", "dream sta", "dream star", "dreem" };
    double worst = 0;
    for (size_t k = 0; k < sizeof typed / sizeof typed[0]; k++) {
        clock_t s = clock();
        n = search_query(&x, typed[k], SK_SONG, SCOPE_FILTER_ALL, h, 8);
        double ms = (clock() - s) * 1000.0 / CLOCKS_PER_SEC;
        if (ms > worst) worst = ms;
        CHECK(n > 0);
    }
    printf("50,000 tracks: slowest keystroke %.1f ms\n", worst);
    CHECK(worst < 250.0);          /* generous: slow CI machines */
    search_index_clear(&x);
    TEST_DONE();
}
