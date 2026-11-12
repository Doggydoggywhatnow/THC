/* perf.c: a headless timing run for docs/performance.md (#12). Set THC_PERF=1 and THC scans its library, reads the
 * tags, times a search and a large queue, scrolls the Music screen, prints one JSON line and exits. It is for
 * measuring only: it changes nothing, and with the variable unset none of this runs. */
#include "app.h"
#include "queue.h"
#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/resource.h>
#endif

enum { S_SCAN, S_TAGS, S_BENCH, S_SCROLL, S_DONE };

static struct {
    bool on;
    int state;
    Uint64 t0, t_state;
    double first_frame, scan_ms, tags_ms, search_build_ms, search_avg_ms, search_max_ms, queue_add_ms, queue_shuffle_ms,
        queue_export_ms, queue_remove_ms;
    int files, songs, scroll_frames, search_queries;
    double frames[400];
    Uint64 last_frame;
    bool got_first;
} P;

bool perf_enabled(void) { return P.on; }

void perf_init(void)
{
    P.on = SDL_getenv("THC_PERF") != NULL;
    P.t0 = SDL_GetTicksNS();
}

static double ms_since(Uint64 t) { return (double)(SDL_GetTicksNS() - t) / 1e6; }

void perf_after_frame(void)
{
    if (!P.on) return;
    Uint64 now = SDL_GetTicksNS();
    if (!P.got_first) {
        P.got_first = true;
        P.first_frame = (double)(now - P.t0) / 1e6;
    }
    if (P.state == S_SCROLL && P.last_frame && P.scroll_frames < 400)
        P.frames[P.scroll_frames++] = (double)(now - P.last_frame) / 1e6;
    P.last_frame = now;
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static int count_songs(const FsNode *n, SearchIndex *x)
{
    if (!n) return 0;
    if (!n->dir) {
        if (n->kind != MK_AUDIO && n->kind != MK_AUDIO_MPV) return 0;
        if (x) search_index_add(x, SK_SONG, n->name, n->parent ? n->parent->name : NULL, (void *)n);
        return 1;
    }
    int c = 0;
    for (int i = 0; i < n->nkids; i++) c += count_songs(n->kids[i], x);
    return c;
}

static void bench(App *a)
{
    /* search: build the index and time keystrokes of a typical query */
    SearchIndex x;
    search_index_init(&x);
    Uint64 t = SDL_GetTicksNS();
    P.songs = count_songs(a->lib.root[ROOT_MUSIC], &x);
    P.search_build_ms = ms_since(t);
    static const char *const q[] = { "a", "al", "alb", "albu", "album", "album 1", "tra", "trac", "track", "track 7", "ar", "art",
                                     "arti", "artis", "artist", "artist 3", "zz", "beyo", "son", "song" };
    SearchHit hits[64];
    double sum = 0, mx = 0;
    for (int i = 0; i < (int)(sizeof q / sizeof q[0]); i++) {
        t = SDL_GetTicksNS();
        for (int k = 0; k < SK_COUNT; k++) search_query(&x, q[i], k, SCOPE_FILTER_ALL, hits, 64);
        double d = ms_since(t);
        sum += d;
        if (d > mx) mx = d;
        P.search_queries++;
    }
    P.search_avg_ms = sum / P.search_queries;
    P.search_max_ms = mx;
    search_index_clear(&x);

    /* a large queue: add, shuffle, remove, export */
    Queue qu;
    queue_init(&qu, QUEUE_MUSIC);
    t = SDL_GetTicksNS();
    for (int i = 0; i < 10000; i++) {
        QueueItem it;
        memset(&it, 0, sizeof it);
        it.kind = QK_SONG;
        it.size = 1000 + i;
        SDL_snprintf(it.rel, sizeof it.rel, "Artist %d/Album %d/Track %d.mp3", i % 50, i % 500, i);
        SDL_snprintf(it.path, sizeof it.path, "/library/%s", it.rel);
        SDL_snprintf(it.title, sizeof it.title, "Track %d", i);
        queue_add(&qu, &it, false);
    }
    P.queue_add_ms = ms_since(t);
    queue_set_current(&qu, 100);
    t = SDL_GetTicksNS();
    queue_shuffle_upcoming(&qu, 12345u);
    P.queue_shuffle_ms = ms_since(t);
    t = SDL_GetTicksNS();
    FILE *f = tmpfile();
    if (f) {
        queue_export_m3u8(&qu, f, false);
        fclose(f);
    }
    P.queue_export_ms = ms_since(t);
    t = SDL_GetTicksNS();
    for (int i = 0; i < 1000; i++) queue_remove(&qu, qu.n / 2);
    P.queue_remove_ms = ms_since(t);
    queue_free(&qu);
}

static long peak_rss_kb(void)
{
#ifndef _WIN32
    struct rusage u;
    if (getrusage(RUSAGE_SELF, &u) == 0) {
#ifdef __APPLE__
        return (long)(u.ru_maxrss / 1024);
#else
        return (long)u.ru_maxrss;
#endif
    }
#endif
    return 0;
}

static int files_in(const FsNode *n)
{
    if (!n) return 0;
    if (!n->dir) return 1;
    int c = 0;
    for (int i = 0; i < n->nkids; i++) c += files_in(n->kids[i]);
    return c;
}

/* Called every loop turn. */
void perf_step(App *a)
{
    if (!P.on) return;
    a->ui.needs_redraw = true;                       /* a real frame each turn, like scrolling */
    switch (P.state) {
    case S_SCAN:
        if (!a->scanning && a->lib_version > 0) {
            P.scan_ms = (double)(SDL_GetTicksNS() - P.t0) / 1e6;
            for (int r = 0; r < ROOT_COUNT; r++) P.files += files_in(a->full.root[r]);
            P.t_state = SDL_GetTicksNS();
            P.state = S_TAGS;
        } else if (ms_since(P.t0) > 600000) P.state = S_DONE;
        break;
    case S_TAGS:
        if (tagcache_done() >= tagcache_total()) {
            P.tags_ms = ms_since(P.t_state);
            bench(a);
            app_go(a, SCREEN_MUSIC);
            P.state = S_SCROLL;
            P.last_frame = 0;
        }
        break;
    case S_SCROLL: {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.y = -1;
        ev.wheel.mouse_x = (float)a->ui.w / 2;
        ev.wheel.mouse_y = (float)a->ui.h / 2;
        SDL_PushEvent(&ev);
        if (P.scroll_frames >= 300) P.state = S_DONE;
        break;
    }
    case S_DONE: {
        qsort(P.frames, (size_t)P.scroll_frames, sizeof P.frames[0], cmp_d);
        double sum = 0;
        for (int i = 0; i < P.scroll_frames; i++) sum += P.frames[i];
        int n = P.scroll_frames;
        printf("{\"files\":%d,\"songs\":%d,\"scan_ms\":%.1f,\"tags_ms\":%.1f,\"first_frame_ms\":%.1f,"
               "\"scroll_frames\":%d,\"scroll_avg_ms\":%.2f,\"scroll_p95_ms\":%.2f,\"scroll_max_ms\":%.2f,"
               "\"search_build_ms\":%.2f,\"search_query_avg_ms\":%.3f,\"search_query_max_ms\":%.3f,"
               "\"queue_add_10000_ms\":%.2f,\"queue_shuffle_ms\":%.2f,\"queue_export_ms\":%.2f,\"queue_remove_1000_ms\":%.2f,"
               "\"peak_rss_kb\":%ld}\n",
               P.files, P.songs, P.scan_ms, P.tags_ms, P.first_frame, n, n ? sum / n : 0, n ? P.frames[(int)(n * 0.95)] : 0,
               n ? P.frames[n - 1] : 0, P.search_build_ms, P.search_avg_ms, P.search_max_ms, P.queue_add_ms, P.queue_shuffle_ms,
               P.queue_export_ms, P.queue_remove_ms, peak_rss_kb());
        fflush(stdout);
        a->running = false;
        break;
    }
    }
}
