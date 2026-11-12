/* tagcache.c: audio tags for the whole music and audiobook library, read in
 * a background thread after each scan so the interface never waits on disk.
 * Read-only; THC never writes tags. */
#include "app.h"
#include "tags.h"

#include <stdlib.h>
#include <string.h>

static struct {
    int n;
    char **path;
    TagInfo *info;
    SDL_AtomicInt *ready;
    int *slot;          /* open-addressing table of indexes + 1, 0 = empty */
    int slots;
    SDL_Thread *thr;
    SDL_AtomicInt cancel;
    SDL_AtomicInt done;   /* files read so far */
    Uint32 wake;
} T;

static Uint32 hash_path(const char *s)
{
    Uint32 h = 2166136261u;
    for (; *s; s++) h = (h ^ (Uint8)*s) * 16777619u;
    return h;
}

static int find(const char *path)
{
    if (!T.slots) return -1;
    Uint32 h = hash_path(path);
    for (int i = 0; i < T.slots; i++) {
        int s = T.slot[(h + (Uint32)i) % (Uint32)T.slots];
        if (!s) return -1;
        if (!strcmp(T.path[s - 1], path)) return s - 1;
    }
    return -1;
}

static void insert(const char *path, int idx)
{
    Uint32 h = hash_path(path);
    for (int i = 0; i < T.slots; i++) {
        int *s = &T.slot[(h + (Uint32)i) % (Uint32)T.slots];
        if (!*s) {
            *s = idx + 1;
            return;
        }
    }
}

static bool is_tagged_kind(const FsNode *n)
{
    return !n->dir && (n->kind == MK_AUDIO || n->kind == MK_AUDIO_MPV || n->kind == MK_BOOK);
}

static int collect(App *a, const FsNode *n, int root, char ***paths, int *cap, int count)
{
    if (!n) return count;
    if (is_tagged_kind(n)) {
        char full[THC_PATH_MAX];
        app_full_path(a, root, n, full, sizeof full);
        if (count == *cap) {
            *cap = *cap ? *cap * 2 : 256;
            *paths = (char **)realloc(*paths, (size_t)*cap * sizeof **paths);
        }
        (*paths)[count++] = SDL_strdup(full);
        return count;
    }
    for (int i = 0; i < n->nkids; i++) count = collect(a, n->kids[i], root, paths, cap, count);
    return count;
}

static int SDLCALL worker(void *unused)
{
    (void)unused;
    for (int i = 0; i < T.n; i++) {
        if (SDL_GetAtomicInt(&T.cancel)) break;
        tags_read(T.path[i], &T.info[i]);
        SDL_SetAtomicInt(&T.ready[i], 1);
        SDL_AddAtomicInt(&T.done, 1);
        if ((i & 31) == 31 || i == T.n - 1) {
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = T.wake;
            SDL_PushEvent(&ev);
        }
    }
    return 0;
}

void tagcache_shutdown(void)
{
    if (T.thr) {
        SDL_SetAtomicInt(&T.cancel, 1);
        SDL_WaitThread(T.thr, NULL);
        T.thr = NULL;
    }
    for (int i = 0; i < T.n; i++) SDL_free(T.path[i]);
    free(T.path);
    free(T.info);
    free(T.ready);
    free(T.slot);
    memset(&T, 0, sizeof T);
}

void tagcache_start(App *a)
{
    tagcache_shutdown();
    char **paths = NULL;
    int cap = 0;
    int n = collect(a, a->lib.root[ROOT_MUSIC], ROOT_MUSIC, &paths, &cap, 0);
    n = collect(a, a->lib.root[ROOT_BOOKS], ROOT_BOOKS, &paths, &cap, n);
    if (n == 0) {
        free(paths);
        return;
    }
    T.wake = a->mpv_event;
    T.path = paths;
    T.n = n;
    T.info = (TagInfo *)calloc((size_t)n, sizeof *T.info);
    T.ready = (SDL_AtomicInt *)calloc((size_t)n, sizeof *T.ready);
    T.slots = n * 2 + 1;
    T.slot = (int *)calloc((size_t)T.slots, sizeof *T.slot);
    if (!T.info || !T.ready || !T.slot) {
        tagcache_shutdown();
        return;
    }
    for (int i = 0; i < n; i++) insert(T.path[i], i);
    SDL_SetAtomicInt(&T.cancel, 0);
    T.thr = SDL_CreateThread(worker, "thc-tags", NULL);
}

int tagcache_done(void) { return SDL_GetAtomicInt(&T.done); }

const TagInfo *tag_get(const App *a, int root, const FsNode *file)
{
    if (!T.n || !file || file->dir) return NULL;
    char full[THC_PATH_MAX];
    app_full_path(a, root, file, full, sizeof full);
    int i = find(full);
    if (i < 0 || !SDL_GetAtomicInt(&T.ready[i])) return NULL;
    const TagInfo *t = &T.info[i];
    if (!t->title[0] && !t->artist[0] && !t->album[0] && !t->track && !t->has_art && !t->narrator[0] && !t->chapters)
        return NULL;
    return t;
}

bool tag_art_key(const App *a, int root, const FsNode *file, char *buf, size_t cap)
{
    const TagInfo *t = tag_get(a, root, file);
    if (!t || !t->has_art) return false;
    char full[THC_PATH_MAX];
    app_full_path(a, root, file, full, sizeof full);
    SDL_snprintf(buf, cap, "tag:%s", full);
    return true;
}
