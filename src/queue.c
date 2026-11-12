#include "queue.h"

#include "settings.h"

#include <stdlib.h>
#include <string.h>

int queue_home_of(int kind) { return kind == QK_VIDEO ? QUEUE_VIDEO : QUEUE_MUSIC; }

void queue_init(Queue *q, int id)
{
    memset(q, 0, sizeof *q);
    q->id = id;
    q->cur = -1;
    q->rng = 2463534242u;
}

void queue_free(Queue *q)
{
    free(q->items);
    int id = q->id;
    queue_init(q, id);
}

void queue_clear(Queue *q)
{
    q->n = 0;
    q->cur = -1;
}

static bool grow(Queue *q)
{
    if (q->n < q->cap) return true;
    if (q->n >= QUEUE_MAX_ITEMS) return false;
    int cap = q->cap ? q->cap * 2 : 64;
    if (cap > QUEUE_MAX_ITEMS) cap = QUEUE_MAX_ITEMS;
    QueueItem *p = realloc(q->items, sizeof *p * (size_t)cap);
    if (!p) return false;
    q->items = p;
    q->cap = cap;
    return true;
}

int queue_insert(Queue *q, int index, const QueueItem *it)
{
    if (index < 0 || index > q->n || !grow(q)) return -1;
    memmove(&q->items[index + 1], &q->items[index], sizeof *q->items * (size_t)(q->n - index));
    q->items[index] = *it;
    q->items[index].played = false;
    q->n++;
    if (q->cur >= index) q->cur++;       /* the current item moved down with the rest */
    return index;
}

int queue_add(Queue *q, const QueueItem *it, bool after_current)
{
    int at = after_current && q->cur >= 0 ? q->cur + 1 : q->n;
    if (after_current && q->cur < 0) at = 0;
    return queue_insert(q, at, it);
}

bool queue_remove(Queue *q, int index)
{
    if (index < 0 || index >= q->n) return false;
    memmove(&q->items[index], &q->items[index + 1], sizeof *q->items * (size_t)(q->n - index - 1));
    q->n--;
    if (q->cur > index) q->cur--;
    else if (q->cur == index && q->cur >= q->n) q->cur = q->n - 1;   /* removed the last: stay on the new last */
    if (q->n == 0) q->cur = -1;
    return true;
}

int queue_remove_if(Queue *q, bool (*pred)(void *ud, const QueueItem *it), void *ud, int keep)
{
    int removed = 0;
    for (int i = q->n - 1; i >= 0; i--) {
        if (i == keep || !pred(ud, &q->items[i])) continue;
        queue_remove(q, i);
        if (keep > i) keep--;
        removed++;
    }
    return removed;
}

bool queue_move(Queue *q, int from, int to)
{
    if (from < 0 || from >= q->n || to < 0 || to >= q->n) return false;
    if (from == to) return true;
    QueueItem tmp = q->items[from];
    if (from < to) memmove(&q->items[from], &q->items[from + 1], sizeof tmp * (size_t)(to - from));
    else memmove(&q->items[to + 1], &q->items[to], sizeof tmp * (size_t)(from - to));
    q->items[to] = tmp;
    if (q->cur == from) q->cur = to;
    else if (from < q->cur && to >= q->cur) q->cur--;
    else if (from > q->cur && to <= q->cur) q->cur++;
    return true;
}

int queue_transfer(Queue *from, int index, Queue *to, bool after_current)
{
    if (index < 0 || index >= from->n) return -1;
    QueueItem it = from->items[index];
    int at = queue_add(to, &it, after_current);
    if (at >= 0) queue_remove(from, index);
    return at;
}

bool queue_set_current(Queue *q, int index)
{
    if (index < 0 || index >= q->n) return false;
    q->cur = index;
    for (int i = 0; i < q->n; i++) q->items[i].played = i < index;
    return true;
}

int queue_upcoming(const Queue *q)
{
    return q->cur < 0 ? q->n : q->n - q->cur - 1;
}

int queue_played_count(const Queue *q)
{
    int c = 0;
    for (int i = 0; i < q->n; i++) c += q->items[i].played;
    return c;
}

int queue_next(Queue *q)
{
    if (q->n == 0) return -1;
    if (q->cur >= 0 && q->repeat == REPEAT_ONE) return q->cur;
    if (q->cur >= 0) q->items[q->cur].played = true;
    int next = q->cur + 1;
    if (next >= q->n) {
        if (q->repeat == REPEAT_ALL) {
            for (int i = 0; i < q->n; i++) q->items[i].played = false;
            if (q->shuffle) queue_shuffle_upcoming(q, q->rng * 1664525u + 1013904223u);
            q->cur = 0;
            return 0;
        }
        return -1;
    }
    q->cur = next;
    return next;
}

int queue_prev(Queue *q, double elapsed, bool restart_first)
{
    if (q->cur < 0) return -1;
    if (restart_first && elapsed > 3.0) return q->cur;
    if (q->cur == 0) return restart_first ? 0 : -1;
    q->items[q->cur].played = false;
    q->cur--;
    q->items[q->cur].played = false;     /* it is being played again */
    return q->cur;
}

static uint32_t rnd(Queue *q)
{
    uint32_t x = q->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return q->rng = x ? x : 2463534242u;
}

void queue_shuffle_upcoming(Queue *q, uint32_t seed)
{
    q->rng = seed ? seed : 2463534242u;
    int first = q->cur + 1;
    for (int i = q->n - 1; i > first; i--) {
        int j = first + (int)(rnd(q) % (uint32_t)(i - first + 1));
        QueueItem t = q->items[i];
        q->items[i] = q->items[j];
        q->items[j] = t;
    }
}

int queue_chain_next(int finished, const int chain[QUEUE_COUNT], bool *used)
{
    if (finished < 0 || finished >= QUEUE_COUNT) return -1;
    if (chain[finished] != CHAIN_OTHER || (used && *used)) return -1;
    if (used) *used = true;
    return finished == QUEUE_MUSIC ? QUEUE_VIDEO : QUEUE_MUSIC;
}

/* ---- M3U8 ---- */

bool queue_export_m3u8(const Queue *q, FILE *f, bool relative)
{
    if (!f) return false;
    fputs("\xEF\xBB\xBF", f);   /* UTF-8 byte order mark: some players want it, importers skip it */
    fputs("#EXTM3U\n", f);
    fprintf(f, "#THC-QUEUE:%s\n", q->id == QUEUE_VIDEO ? "video" : "music");
    for (int i = 0; i < q->n; i++) {
        const QueueItem *it = &q->items[i];
        fprintf(f, "#THC-KIND:%s\n", it->kind == QK_VIDEO ? "video" : it->kind == QK_BOOK ? "audiobook" : "song");
        fprintf(f, "#THC-ROOT:%d\n", it->root);
        fprintf(f, "#EXTINF:-1,%s\n", it->title);
        const char *p = relative && it->rel[0] ? it->rel : it->path;
        fprintf(f, "%s\n", p);
    }
    return true;
}

static char *trim_line(char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    char *e = p + strlen(p);
    while (e > p && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return p;
}

void queue_import_m3u8(Queue *q, const char *text, QueueResolve resolve, void *ud, QueueImport *out)
{
    memset(out, 0, sizeof *out);
    out->home = -1;
    int kind = -1, root = -1;
    char title[160] = "";
    if (!strncmp(text, "\xEF\xBB\xBF", 3)) text += 3;
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[1100];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p = nl ? nl + 1 : p + len;
        char *l = trim_line(line);
        if (!*l) continue;
        if (*l == '#') {
            if (!strncmp(l, "#THC-QUEUE:", 11)) out->home = !strcmp(l + 11, "video") ? QUEUE_VIDEO : QUEUE_MUSIC;
            else if (!strncmp(l, "#THC-KIND:", 10))
                kind = !strcmp(l + 10, "video") ? QK_VIDEO : !strcmp(l + 10, "audiobook") ? QK_BOOK : QK_SONG;
            else if (!strncmp(l, "#THC-ROOT:", 10)) root = atoi(l + 10);
            else if (!strncmp(l, "#EXTINF:", 8)) {
                char *comma = strchr(l, ',');
                snprintf(title, sizeof title, "%s", comma ? comma + 1 : "");
            }
            continue;
        }
        QueueItem it;
        memset(&it, 0, sizeof it);
        it.kind = kind;
        it.root = root;
        snprintf(it.title, sizeof it.title, "%s", title);
        if (resolve(ud, root, kind, l, &it)) {
            if (queue_add(q, &it, false) >= 0) out->added++;
        } else {
            if (out->missing < 8) snprintf(out->missing_names[out->missing], sizeof out->missing_names[0], "%s", l);
            out->missing++;
        }
        kind = root = -1;
        title[0] = 0;
    }
}

DispatchPlan queue_dispatch(const DispatchIn *in)
{
    DispatchPlan p = { BACKEND_SKIP, false, false, false };
    const QueueItem *it = in->item;
    if (!it) return p;
    if (it->kind == QK_VIDEO) {
        if (in->queue == QUEUE_VIDEO || it->mode == QMODE_WINDOW) {
            if (!in->have_mpv) { p.backend = BACKEND_NONE_AVAILABLE; return p; }
            p.backend = BACKEND_VIDEO_WINDOW;
            p.open_window = !in->window_open;
            p.suspend_music = in->audio_playing && in->pause_music_on_video;
            return p;
        }
        if (it->mode == QMODE_SKIP) return p;                       /* skipped */
        p.backend = in->have_mpv ? BACKEND_MPV_AUDIO : BACKEND_NONE_AVAILABLE;   /* listen only */
        p.close_window = in->window_open;
        return p;
    }
    /* a song or an audiobook: in the player bar, whatever queue it sits in */
    p.close_window = in->window_open;
    bool book = it->kind == QK_BOOK;
    if (in->have_mpv && (!in->engine_handles || book)) p.backend = BACKEND_MPV_AUDIO;
    else if (in->engine_handles) p.backend = BACKEND_ENGINE;
    else p.backend = BACKEND_NONE_AVAILABLE;
    return p;
}
