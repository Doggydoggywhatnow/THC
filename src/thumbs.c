/* thumbs.c: video thumbnails.
 *
 * A thumbnail comes from, in order: an image next to the video (see thumb_sidecar),
 * a frame generated from the video itself, or the card's normal placeholder.
 * Frames are made by a hidden libmpv instance (vo=image, no window, no sound) on a
 * background thread and cached as small JPEGs in THC's own data folder, never next
 * to your media. Nothing here writes to your media files or uses the network. */
#include "app.h"
#include "thumbs.h"

#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 16384
#define MAX_JOBS 256
#define CACHE_CAP (512ll * 1024 * 1024)
#define CACHE_TARGET (384ll * 1024 * 1024)

enum { TS_UNKNOWN = 0, TS_QUEUED, TS_READY, TS_FAILED };

typedef struct Entry {
    char key[17];
    SDL_AtomicInt state;
} Entry;

typedef struct Job {
    int entry;
    int generation;
    char path[THC_PATH_MAX];
} Job;

static struct {
    bool init;
    Entry *entries;
    int nentries;
    SDL_Mutex *mu;
    SDL_Condition *cv;
    Job jobs[MAX_JOBS];
    int njobs;               /* a stack: the most recently asked-for (visible) first */
    SDL_Thread *thr;
    bool quit;
    int generation;
    const MpvApi *api;
    char dir[THC_PATH_MAX];  /* cache folder */
    Uint32 wake;
} T;

static bool exists(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* ------------------------------------------------------------------ pruning */

typedef struct PruneList {
    ThumbFile *f;
    char (*names)[64];
    int n, cap;
} PruneList;

static SDL_EnumerationResult SDLCALL prune_entry(void *ud, const char *dirname, const char *fname)
{
    PruneList *l = ud;
    char full[THC_PATH_MAX + 80];
    SDL_snprintf(full, sizeof full, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(full, &info) || info.type != SDL_PATHTYPE_FILE) return SDL_ENUM_CONTINUE;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 512;
        l->f = (ThumbFile *)realloc(l->f, (size_t)l->cap * sizeof *l->f);
        l->names = realloc(l->names, (size_t)l->cap * sizeof *l->names);
    }
    l->f[l->n] = (ThumbFile){ (int64_t)info.size, (int64_t)(info.modify_time / SDL_NS_PER_SECOND) };
    SDL_strlcpy(l->names[l->n], fname, 64);
    l->n++;
    return SDL_ENUM_CONTINUE;
}

static void prune_cache(void)
{
    PruneList l = { 0 };
    SDL_EnumerateDirectory(T.dir, prune_entry, &l);
    if (l.n) {
        int *del = (int *)malloc((size_t)l.n * sizeof *del);
        if (del) {
            int k = thumb_prune_plan(l.f, l.n, CACHE_CAP, CACHE_TARGET, del);
            for (int i = 0; i < k; i++) {
                char full[THC_PATH_MAX + 80];
                SDL_snprintf(full, sizeof full, "%s/%s", T.dir, l.names[del[i]]);
                SDL_RemovePath(full);
            }
            free(del);
        }
    }
    free(l.f);
    free(l.names);
}

/* ------------------------------------------------------------------ worker */

static int SDLCALL worker(void *unused)
{
    (void)unused;
    prune_cache();
    for (;;) {
        SDL_LockMutex(T.mu);
        while (!T.quit && T.njobs == 0) SDL_WaitCondition(T.cv, T.mu);
        if (T.quit) {
            SDL_UnlockMutex(T.mu);
            return 0;
        }
        Job job = T.jobs[--T.njobs];
        SDL_UnlockMutex(T.mu);

        char out[THC_PATH_MAX + 40];
        SDL_snprintf(out, sizeof out, "%s/%s.jpg", T.dir, T.entries[job.entry].key);
        bool ok = thumb_generate(T.api, job.path, out);
        SDL_LockMutex(T.mu);
        if (job.generation != T.generation) {
            if (ok) SDL_RemovePath(out);          /* the cache was cleared meanwhile */
        } else {
            SDL_SetAtomicInt(&T.entries[job.entry].state, ok ? TS_READY : TS_FAILED);
        }
        SDL_UnlockMutex(T.mu);
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = T.wake;
        SDL_PushEvent(&ev);
    }
}

static bool start(App *a)
{
    if (T.init) return T.thr != NULL;
    T.init = true;
    if (!a->have_mpv) return false;
    SDL_snprintf(T.dir, sizeof T.dir, "%sthumbs", a->data_dir);
    SDL_CreateDirectory(T.dir);
    T.entries = (Entry *)calloc(MAX_ENTRIES, sizeof *T.entries);
    T.mu = SDL_CreateMutex();
    T.cv = SDL_CreateCondition();
    T.api = &a->mpv;
    T.wake = a->mpv_event;
    if (!T.entries || !T.mu || !T.cv) return false;
    T.thr = SDL_CreateThread(worker, "thc-thumbs", NULL);
    return T.thr != NULL;
}

void thumbs_shutdown(void)
{
    if (T.thr) {
        SDL_LockMutex(T.mu);
        T.quit = true;
        SDL_SignalCondition(T.cv);
        SDL_UnlockMutex(T.mu);
        SDL_WaitThread(T.thr, NULL);
    }
    if (T.cv) SDL_DestroyCondition(T.cv);
    if (T.mu) SDL_DestroyMutex(T.mu);
    free(T.entries);
    memset(&T, 0, sizeof T);
}

void thumbs_clear(App *a)
{
    if (!start(a)) {
        /* no libmpv: nothing was generated, but an old cache folder may exist */
        SDL_snprintf(T.dir, sizeof T.dir, "%sthumbs", a->data_dir);
    }
    if (T.mu) SDL_LockMutex(T.mu);
    T.njobs = 0;
    T.generation++;
    if (T.entries) memset(T.entries, 0, MAX_ENTRIES * sizeof *T.entries);
    T.nentries = 0;
    if (T.mu) SDL_UnlockMutex(T.mu);
    PruneList l = { 0 };
    SDL_EnumerateDirectory(T.dir, prune_entry, &l);
    for (int i = 0; i < l.n; i++) {
        char full[THC_PATH_MAX + 80];
        SDL_snprintf(full, sizeof full, "%s/%s", T.dir, l.names[i]);
        SDL_RemovePath(full);
    }
    free(l.f);
    free(l.names);
}

/* ------------------------------------------------------------------ lookup */

static Entry *entry_for(const char *key, int *index)
{
    for (int i = 0; i < T.nentries; i++)
        if (!strcmp(T.entries[i].key, key)) {
            *index = i;
            return &T.entries[i];
        }
    if (T.nentries >= MAX_ENTRIES) return NULL;
    *index = T.nentries++;
    Entry *e = &T.entries[*index];
    SDL_strlcpy(e->key, key, sizeof e->key);
    return e;
}

bool thumb_get(App *a, int root, const FsNode *file, char *out, size_t cap)
{
    if (!file || file->dir || file->kind != MK_VIDEO) return false;
    /* 1. an image next to the video */
    const FsNode *side = thumb_sidecar(file);
    if (side) {
        app_full_path(a, root, side, out, cap);
        return true;
    }
    /* 2. a generated frame */
    if (!a->set.video_thumbs || !a->have_mpv || !start(a)) return false;
    char key[17], full[THC_PATH_MAX];
    app_full_path(a, root, file, full, sizeof full);
    thumb_key_cached(full, file->size, file->mtime, key);   /* content-based, read once per session (#22) */
    int idx = 0;
    Entry *e = entry_for(key, &idx);
    if (!e) return false;
    int st = SDL_GetAtomicInt(&e->state);
    if (st == TS_READY) {
        SDL_snprintf(out, cap, "%s/%s.jpg", T.dir, key);
        return true;
    }
    if (st != TS_UNKNOWN) return false;
    SDL_snprintf(out, cap, "%s/%s.jpg", T.dir, key);
    if (exists(out)) {                 /* made in an earlier run */
        SDL_SetAtomicInt(&e->state, TS_READY);
        return true;
    }
    SDL_SetAtomicInt(&e->state, TS_QUEUED);
    SDL_LockMutex(T.mu);
    if (T.njobs == MAX_JOBS) {         /* drop the oldest request */
        SDL_SetAtomicInt(&T.entries[T.jobs[0].entry].state, TS_UNKNOWN);
        memmove(&T.jobs[0], &T.jobs[1], (MAX_JOBS - 1) * sizeof T.jobs[0]);
        T.njobs--;
    }
    Job *j = &T.jobs[T.njobs++];
    j->entry = idx;
    j->generation = T.generation;
    app_full_path(a, root, file, j->path, sizeof j->path);
    SDL_SignalCondition(T.cv);
    SDL_UnlockMutex(T.mu);
    out[0] = 0;
    return false;
}
