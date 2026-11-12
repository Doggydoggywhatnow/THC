/* queue.h: THC's play queues (core logic, no SDL, no UI, no backends).
 *
 * There are two queues: the music queue (songs and audiobooks) and the video queue. Each holds
 * items of any type. Items before the current one are the backlog (already played); items after
 * it are still to come. The app layer plays items and tells the queue what happened. */
#ifndef THC_QUEUE_H
#define THC_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { QUEUE_MUSIC = 0, QUEUE_VIDEO = 1, QUEUE_COUNT };
/* kinds match KIND_* in settings.h */
enum { QK_VIDEO = 0, QK_SONG = 1, QK_BOOK = 2 };
enum { QMODE_WINDOW = 0, QMODE_LISTEN, QMODE_SKIP };   /* a video in the music queue */

#define QUEUE_MAX_ITEMS 20000

typedef struct QueueItem {
    int kind;                /* QK_* */
    int root;                /* library root the item came from (app-defined) */
    char rel[512];           /* path relative to its library folder */
    char path[1024];         /* full path */
    int64_t size;
    char title[160];
    char sub[160];           /* artist, show and episode, ... */
    char art[512];           /* cover: an image path or a tag-art key (app-defined) */
    int mode;                /* QMODE_*: only used for videos in the music queue */
    bool played;
} QueueItem;

typedef struct Queue {
    int id;                  /* QUEUE_MUSIC or QUEUE_VIDEO */
    QueueItem *items;
    int n, cap;
    int cur;                 /* index of the current item, -1 = nothing is playing */
    int repeat;              /* REPEAT_OFF / ONE / ALL (settings.h) */
    bool shuffle;
    uint32_t rng;
} Queue;

/* The queue an item belongs in by default. */
int queue_home_of(int kind);

void queue_init(Queue *q, int id);
void queue_free(Queue *q);
/* Remove everything. */
void queue_clear(Queue *q);

/* Add at the end, or right after the current item. Returns the index, or -1 if full. */
int queue_add(Queue *q, const QueueItem *it, bool after_current);
int queue_insert(Queue *q, int index, const QueueItem *it);
/* Remove one item. The current item may be removed (cur then points at what followed it). */
bool queue_remove(Queue *q, int index);
bool queue_move(Queue *q, int from, int to);
/* Move an item to the other queue. Returns its index there, or -1. */
int queue_transfer(Queue *from, int index, Queue *to, bool after_current);

/* Start at an item (marks everything before it as played, everything from it on as unplayed). */
bool queue_set_current(Queue *q, int index);
/* Number of items still to come after the current one. */
int queue_upcoming(const Queue *q);

/* Advance after the current item finished or was skipped. Returns the index to play, or -1 at the
 * end of the queue (cur stays on the last item). Repeat one returns the same item; repeat all wraps
 * around and clears the played marks. */
int queue_next(Queue *q);
/* Previous: if `elapsed` is more than 3 seconds and `restart_first`, returns the current index (restart it);
 * otherwise goes back through the backlog. Returns -1 if there is nothing before. */
int queue_prev(Queue *q, double elapsed, bool restart_first);

/* Shuffle only what is still to come; the current item and the backlog stay where they are. */
void queue_shuffle_upcoming(Queue *q, uint32_t seed);

/* After a queue ends: which queue (if any) starts next. chain[finished] is CHAIN_NONE/CHAIN_OTHER
 * (settings.h). *used makes a chain run once per start: it is cleared when the user starts playback. */
int queue_chain_next(int finished, const int chain[QUEUE_COUNT], bool *used);

/* ---- M3U8 export and import ---- */
/* Writes UTF-8 M3U8 with #THC-QUEUE:music|video, and per item #THC-KIND, #THC-ROOT and #EXTINF.
 * With `relative`, each path line is the item's library-relative path; otherwise the full path. */
bool queue_export_m3u8(const Queue *q, FILE *f, bool relative);

/* Resolves one line of an imported file to a playable item: fills path and size, returns false
 * if the file is missing. `rel_or_abs` is the line as written, `root` and `kind` come from the
 * comments (or -1 / -1 for a plain M3U). */
typedef bool (*QueueResolve)(void *ud, int root, int kind, const char *rel_or_abs, QueueItem *out);

typedef struct QueueImport {
    int home;                /* QUEUE_MUSIC or QUEUE_VIDEO from #THC-QUEUE, -1 if not stated */
    int added;
    int missing;
    char missing_names[8][160];   /* the first few missing entries, for the message */
} QueueImport;
void queue_import_m3u8(Queue *q, const char *text, QueueResolve resolve, void *ud, QueueImport *out);

/* ---- the dispatcher's decisions (pure, so they can be tested without any backend) ---- */
enum { BACKEND_SKIP = 0, BACKEND_VIDEO_WINDOW, BACKEND_ENGINE, BACKEND_MPV_AUDIO, BACKEND_NONE_AVAILABLE };
typedef struct DispatchIn {
    int queue;                 /* QUEUE_MUSIC / QUEUE_VIDEO: the queue the item is played from */
    const QueueItem *item;
    bool engine_handles;       /* THC's own engine can decode this file */
    bool have_mpv;
    bool audio_playing;        /* a sound backend is playing (not paused) */
    bool window_open;          /* the mpv video window is open */
    bool pause_music_on_video; /* setting */
} DispatchIn;
typedef struct DispatchPlan {
    int backend;               /* BACKEND_* for the item */
    bool suspend_music;        /* pause the playing sound first (and resume it after the video queue) */
    bool close_window;         /* close the video window (a sound item is next) */
    bool open_window;          /* a window has to be opened (none is open yet) */
} DispatchPlan;
DispatchPlan queue_dispatch(const DispatchIn *in);

/* Utility used by the UI: a stable "N of M" position text pieces. */
int queue_played_count(const Queue *q);

#endif
