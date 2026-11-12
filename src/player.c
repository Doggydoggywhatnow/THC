/* player.c: the player. It owns the two queues (music and video) and plays their items through
 * the right backend:
 *   - video                      -> ONE mpv window for the whole video queue (the next video loads into it)
 *   - song, audiobook MP3        -> THC's own engine (audio.c: lib-Peep + MP3/Ogg codecs, SDL3 output)
 *   - audiobook M4B, other audio -> libmpv without a window, inside THC's player bar
 *   - a video in the music queue -> a new mpv window (default), or sound only, or skipped (per item)
 * The queue logic itself is in queue.c (no SDL); this file starts and stops backends and records
 * history. The player bar at the bottom is drawn here too. */
#include "app.h"
#include "mediakeys.h"
#include "screenshots.h"
#include "ontop.h"
#include "audio.h"
#include "queue.h"

#include <string.h>

enum { BK_NONE = 0, BK_ENGINE, BK_MPV };     /* the audio backend */
enum { SLEEP_OFF = 0, SLEEP_15, SLEEP_30, SLEEP_60, SLEEP_CHAPTER, SLEEP_COUNT };
static const char *const k_sleep_names[SLEEP_COUNT] = { "Off", "15 min", "30 min", "60 min", "End of chapter" };
static const double k_speeds[] = { 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5 };
#define NSPEEDS ((int)(sizeof k_speeds / sizeof k_speeds[0]))
#define MAXQ 512
#define COUNTDOWN_SECONDS 5.0

static Queue Q[QUEUE_COUNT];

static struct {
    int active;               /* the queue that is playing (or was last) */
    int ab;                   /* audio backend in use */
    VideoSession mpv;         /* libmpv without a window (M4B, listen-only videos) */
    bool paused;              /* the audio backend is paused */
    bool music_suspended;     /* music was paused because the video queue took over */
    bool chain_used;          /* a queue chain already ran since the user last started playback */
    bool video_idle;          /* the video window is open but no file is playing in it */
    double next_at;           /* video "next episode" countdown: when to go on, 0 = none */
    int next_queue;
    double listened;          /* seconds listened to the current item (for plays) */
    bool play_counted;
    double last_tick;
    int sleep;
    double sleep_until;
    Uint64 last_save;
    double seek_preview;      /* while dragging the seek bar, else < 0 */
} P = { .seek_preview = -1 };

Queue *player_queue(int id) { return &Q[id < 0 || id >= QUEUE_COUNT ? 0 : id]; }
int player_active_queue(void) { return P.active; }

static Queue *AQ(void) { return &Q[P.active]; }
static QueueItem *cur_item(void)
{
    Queue *q = AQ();
    return q->cur >= 0 && q->cur < q->n ? &q->items[q->cur] : NULL;
}

static bool video_item_active(const App *a)
{
    const QueueItem *it = (const QueueItem *)0;
    Queue *q = (Queue *)&Q[P.active];
    if (q->cur >= 0 && q->cur < q->n) it = &q->items[q->cur];
    return it && it->kind == QK_VIDEO && a->video.h && !P.video_idle;
}

static bool book_mode(void)
{
    const QueueItem *it = cur_item();
    return it && it->kind == QK_BOOK;
}

/* Is anything loaded that the bar should show? */
static bool have_item(const App *a)
{
    return cur_item() && (P.ab != BK_NONE || video_item_active(a));
}

static double pos_now(const App *a)
{
    if (video_item_active(a)) return a->video.pos;
    if (P.ab == BK_ENGINE) return audio_position();
    if (P.ab == BK_MPV) return P.mpv.pos;
    return 0;
}

static double dur_now(const App *a)
{
    if (video_item_active(a)) return a->video.duration;
    if (P.ab == BK_ENGINE) return audio_duration();
    if (P.ab == BK_MPV) return P.mpv.duration;
    return 0;
}

static bool is_paused(const App *a)
{
    if (video_item_active(a)) return a->video.paused;
    return P.ab == BK_NONE || P.paused;
}

/* ------------------------------------------------------------------------ */
/* History                                                                    */
/* ------------------------------------------------------------------------ */

static void count_play(App *a, const QueueItem *it, double listened)
{
    if (P.play_counted || !settings_history_on(&a->set, it->kind)) return;
    P.play_counted = true;
    history_record_play(a->hist, it->rel, it->size, it->kind, listened);
    app_history_changed();
}

/* Position of the current non-video item (audiobooks), kept in history. Songs only record plays. */
static void save_audio_progress(App *a, bool reached_end)
{
    QueueItem *it = cur_item();
    if (!it || it->kind == QK_VIDEO || P.ab == BK_NONE) return;
    if (!settings_history_on(&a->set, it->kind)) return;
    double d = dur_now(a);
    double pos = reached_end && d > 0 ? d : pos_now(a);
    bool resumable = it->kind == QK_BOOK || (d >= a->set.music_resume_min_minutes * 60.0 && a->set.queue_resume_song);
    if (resumable && (pos > 0 || reached_end))
        history_save_ex(a->hist, it->rel, it->size, it->kind, pos, d, a->set.watched_threshold, reached_end);
    if (reached_end) count_play(a, it, P.listened);
    app_history_changed();
}

static void save_video_progress(App *a)
{
    if (!a->play_key[0] || !settings_history_on(&a->set, KIND_VIDEO)) return;
    if (a->video.failed && a->video.pos <= 0) return;
    history_save_ex(a->hist, a->play_key, a->play_size, KIND_VIDEO, a->video.pos, a->video.duration,
                    a->set.watched_threshold, a->video.reached_eof);
    QueueItem *it = cur_item();
    if (it && it->kind == QK_VIDEO && !P.play_counted &&
        (a->video.reached_eof || (a->video.duration > 0 && a->video.pos >= a->set.watched_threshold * a->video.duration)))
        count_play(a, it, a->video.pos);
    app_history_changed();
}

static double resume_point(App *a, const QueueItem *it)
{
    bool want = it->kind == QK_VIDEO ? a->set.queue_resume_video : it->kind == QK_BOOK ? a->set.queue_resume_book : a->set.queue_resume_song;
    if (!want || !settings_history_on(&a->set, it->kind)) return 0;
    HistoryEntry e;
    if (!history_get(a->hist, it->rel, it->size, &e)) return 0;
    if (it->kind == QK_SONG && e.duration < a->set.music_resume_min_minutes * 60.0) return 0;
    return history_resume_point(&e, a->set.watched_threshold);
}

/* ------------------------------------------------------------------------ */
/* Backends                                                                   */
/* ------------------------------------------------------------------------ */

static void stop_audio(App *a)
{
    (void)a;
    if (P.ab == BK_ENGINE) audio_unload();
    if (P.ab == BK_MPV) video_stop(&P.mpv);
    P.ab = BK_NONE;
    P.paused = false;
}

static void stop_video_window(App *a)
{
    if (a->video.h) {
        save_video_progress(a);
        video_stop(&a->video);
    }
    P.video_idle = false;
    a->play_key[0] = 0;
}

static bool start_audio(App *a, const QueueItem *it, double start)
{
    stop_audio(a);
    const char *name = SDL_strrchr(it->path, '/');
#ifdef _WIN32
    const char *bs = SDL_strrchr(it->path, '\\');
    if (bs > name) name = bs;
#endif
    name = name ? name + 1 : it->path;
    bool engine_ok = audio_engine_handles(name) && it->kind != QK_VIDEO;
    bool book = it->kind == QK_BOOK;
    /* Audiobooks prefer libmpv (speed without pitch change, M4B chapters). Songs prefer THC's engine. */
    bool use_mpv = a->have_mpv && (!engine_ok || book);
    char err[256] = "";
    if (use_mpv) {
        VideoOptions o = { a->mpv_dir, start, true, a->mpv_event, false, a->set.mpv_block_network, NULL, NULL, NULL };
        if (video_play(&P.mpv, &a->mpv, it->path, &o)) {
            P.ab = BK_MPV;
            video_set_volume(&P.mpv, a->set.volume);
            if (book) video_set_speed(&P.mpv, a->set.book_speed);
        } else {
            SDL_strlcpy(err, P.mpv.error, sizeof err);
        }
    } else if (engine_ok) {
        audio_set_volume(a->set.volume);
        audio_set_speed(book ? a->set.book_speed : 1.0);
        if (audio_load(it->path, err, sizeof err)) {
            P.ab = BK_ENGINE;
            if (start > 0) audio_seek(start);
        }
    } else {
        SDL_snprintf(err, sizeof err, "This format needs libmpv, which was not found");
    }
    if (P.ab == BK_NONE) {
        app_toast(a, "Could not play %s: %s", it->title, err);
        return false;
    }
    P.paused = false;
    P.last_save = SDL_GetTicks();
    return true;
}

/* The video window stays on top per the setting (mpv.conf's own `ontop` never wins). */
static void apply_ontop(App *a)
{
    if (!a->video.h) return;
    bool playing = a->video.active && !a->video.paused && !P.video_idle;
    video_set_ontop(&a->video,
                    ontop_wanted(a->set.video_ontop, playing, a->video.fullscreen, a->set.video_ontop_ignore_fullscreen),
                    ontop_level_name(a->set.video_ontop_macos_level));
}

void player_cycle_ontop(App *a)
{
    a->set.video_ontop = ontop_next_mode(a->set.video_ontop);
    app_save_settings(a);
    apply_ontop(a);
    app_toast(a, "Video window on top: %s", ontop_mode_label(a->set.video_ontop));
}

static bool start_video_window(App *a, const QueueItem *it, double start)
{
    if (!a->have_mpv) {
        app_toast(a, "libmpv was not found. Install mpv (libmpv) to play videos.");
        return false;
    }
    SDL_strlcpy(a->play_key, it->rel, sizeof a->play_key);
    a->play_size = it->size;
    SDL_strlcpy(a->play_title, it->title, sizeof a->play_title);
    bool ok;
    if (a->video.h && a->video.active) {
        ok = video_load(&a->video, it->path, start);
    } else {
        if (a->video.h) video_stop(&a->video);
        char shots[THC_PATH_MAX];
        screenshot_dir_resolve(a->data_dir, a->set.screenshot_dir, shots, sizeof shots);
        VideoOptions o = { a->mpv_dir, start, false, a->mpv_event, true, a->set.mpv_block_network,
                           shots, screenshot_format_name(a->set.screenshot_format),
                           screenshot_template(a->set.screenshot_template) };
        ok = video_play(&a->video, &a->mpv, it->path, &o);
        if (!ok) app_toast(a, "%s", a->video.error);
    }
    if (!ok) {
        a->play_key[0] = 0;
        return false;
    }
    P.video_idle = false;
    apply_ontop(a);
    {   /* say so if screenshots cannot be saved, instead of failing silently */
        char shots[THC_PATH_MAX];
        screenshot_dir_resolve(a->data_dir, a->set.screenshot_dir, shots, sizeof shots);
        SDL_PathInfo pi;
        if (!SDL_GetPathInfo(shots, &pi) || pi.type != SDL_PATHTYPE_DIRECTORY)
            app_toast(a, "Screenshots cannot be saved: the folder could not be created");
    }
    if (start > 0) {
        char t[32];
        fmt_time(start, t, sizeof t);
        app_toast(a, "Resuming %s at %s", it->title, t);
    }
    return true;
}

/* Start item `idx` of queue `qid`. Skips items that cannot play (a skipped video in the music
 * queue). Returns false if nothing could be started. */
static bool start_item(App *a, int qid, int idx)
{
    Queue *q = &Q[qid];
    while (idx >= 0 && idx < q->n) {
        QueueItem *it = &q->items[idx];
        if (it->kind == QK_VIDEO && qid == QUEUE_MUSIC && it->mode == QMODE_SKIP) {
            q->items[idx].played = true;
            idx++;
            continue;
        }
        break;
    }
    if (idx < 0 || idx >= q->n) return false;
    QueueItem *it = &q->items[idx];
    double start = resume_point(a, it);
    queue_set_current(q, idx);
    P.active = qid;
    P.listened = 0;
    P.play_counted = false;
    P.next_at = 0;

    const char *name = SDL_strrchr(it->path, '/');
#ifdef _WIN32
    const char *bs2 = SDL_strrchr(it->path, '\\');
    if (bs2 > name) name = bs2;
#endif
    DispatchIn din = { qid, it, audio_engine_handles(name ? name + 1 : it->path), a->have_mpv,
                       P.ab != BK_NONE && !P.paused, a->video.h != NULL, a->set.queue_pause_music_on_video };
    DispatchPlan plan = queue_dispatch(&din);
    if (plan.backend == BACKEND_VIDEO_WINDOW) {
        /* the video queue takes over: music pauses (setting), and resumes after the videos */
        if (plan.suspend_music) {
            if (P.ab == BK_ENGINE) audio_set_paused(true); else video_set_pause(&P.mpv, true);
            P.paused = true;
            P.music_suspended = true;
        }
        return start_video_window(a, it, start);
    }
    /* sound: closes the video window if one is open (it reopens for the next video) */
    if (plan.close_window) stop_video_window(a);
    P.music_suspended = false;
    if (plan.backend == BACKEND_MPV_AUDIO && it->kind == QK_VIDEO) {
        /* listen-only video from the music queue */
        VideoOptions o = { a->mpv_dir, start, true, a->mpv_event, false, a->set.mpv_block_network, NULL, NULL, NULL };
        stop_audio(a);
        if (!video_play(&P.mpv, &a->mpv, it->path, &o)) {
            app_toast(a, "Could not play %s", it->title);
            return false;
        }
        P.ab = BK_MPV;
        P.paused = false;
        video_set_volume(&P.mpv, a->set.volume);
        return true;
    }
    return start_audio(a, it, start);
}

/* ------------------------------------------------------------------------ */
/* Advancing                                                                  */
/* ------------------------------------------------------------------------ */

static void end_of_queue(App *a, int qid)
{
    if (qid == QUEUE_VIDEO) stop_video_window(a);
    else stop_audio(a);
    int chain[QUEUE_COUNT] = { a->set.queue_chain_music, a->set.queue_chain_video };
    int next = queue_chain_next(qid, chain, &P.chain_used);
    if (next >= 0) {
        Queue *o = &Q[next];
        int idx = o->cur >= 0 ? o->cur + 1 : 0;
        if (idx < o->n && start_item(a, next, idx)) {
            app_toast(a, next == QUEUE_VIDEO ? "Now playing the video queue" : "Now playing the music queue");
            return;
        }
    }
    if (qid == QUEUE_VIDEO && P.music_suspended && a->set.queue_resume_music_after_video && P.ab != BK_NONE) {
        if (P.ab == BK_ENGINE) audio_set_paused(false); else video_set_pause(&P.mpv, false);
        P.paused = false;
        P.music_suspended = false;
        P.active = QUEUE_MUSIC;
    }
}

static void advance(App *a, int qid)
{
    Queue *q = &Q[qid];
    if (qid == QUEUE_VIDEO && !a->set.queue_auto_advance_video) {
        stop_video_window(a);
        return;
    }
    int n = queue_next(q);
    while (n >= 0 && !start_item(a, qid, n)) {
        /* this one could not play: try the next */
        n = queue_next(q);
        if (n < 0) break;
    }
    if (n < 0) end_of_queue(a, qid);
}

/* An item came to its natural end. */
static void item_finished(App *a, int qid)
{
    QueueItem *it = cur_item();
    if (it && it->kind == QK_VIDEO) {
        a->video.reached_eof = true;
        save_video_progress(a);
    } else {
        save_audio_progress(a, true);
    }
    if (P.sleep == SLEEP_CHAPTER) {
        P.sleep = SLEEP_OFF;
        if (P.ab == BK_ENGINE) audio_set_paused(true); else if (P.ab == BK_MPV) video_set_pause(&P.mpv, true);
        P.paused = true;
        stop_audio(a);
        return;
    }
    Queue *q = &Q[qid];
    if (qid == QUEUE_VIDEO && a->set.queue_next_countdown && q->repeat != REPEAT_ONE && q->cur + 1 < q->n) {
        P.video_idle = true;
        P.next_at = SDL_GetTicks() / 1000.0 + COUNTDOWN_SECONDS;
        P.next_queue = qid;
        app_toast(a, "Next in %d seconds. Press Next to skip the wait.", (int)COUNTDOWN_SECONDS);
        return;
    }
    if (qid == QUEUE_VIDEO) P.video_idle = true;
    advance(a, qid);
}

/* ------------------------------------------------------------------------ */
/* Controls                                                                   */
/* ------------------------------------------------------------------------ */

static void flush_progress(App *a)
{
    QueueItem *it = cur_item();
    if (!it) return;
    if (it->kind == QK_VIDEO) save_video_progress(a);
    else save_audio_progress(a, false);
}

void player_play_index(App *a, int qid, int idx)
{
    flush_progress(a);
    P.chain_used = false;                       /* a new start: a chain may run once more */
    P.next_at = 0;
    start_item(a, qid, idx);
}

/* Replace a queue's contents and play from `start_index`. */
void player_play_items(App *a, int qid, const QueueItem *items, int n, int start_index)
{
    Queue *q = &Q[qid];
    flush_progress(a);
    queue_clear(q);
    for (int i = 0; i < n; i++) queue_add(q, &items[i], false);
    player_play_index(a, qid, start_index < 0 || start_index >= q->n ? 0 : start_index);
}

/* Play one item now: right after the current one, keeping the rest of the queue. */
void player_play_now(App *a, const QueueItem *it)
{
    int qid = queue_home_of(it->kind);
    Queue *q = &Q[qid];
    int at = q->cur >= 0 ? queue_add(q, it, true) : queue_insert(q, 0, it);
    if (at < 0) return;
    player_play_index(a, qid, at);
}

void player_next(App *a)
{
    Queue *q = AQ();
    if (!q->n) return;
    flush_progress(a);
    P.next_at = 0;
    int r = q->repeat;
    if (r == REPEAT_ONE) q->repeat = REPEAT_OFF;     /* the Next button always moves on */
    int n = queue_next(q);
    q->repeat = r;
    if (n < 0) {
        app_toast(a, "That was the last item in the queue");
        return;
    }
    if (!start_item(a, P.active, n)) advance(a, P.active);
}

void player_prev(App *a)
{
    Queue *q = AQ();
    if (!q->n) return;
    flush_progress(a);
    int n = queue_prev(q, pos_now(a), a->set.queue_prev_restarts);
    if (n < 0) return;
    if (n == q->cur && pos_now(a) > 3.0) {
        if (video_item_active(a)) video_seek_abs(&a->video, 0);
        else if (P.ab == BK_ENGINE) audio_seek(0);
        else if (P.ab == BK_MPV) video_seek_abs(&P.mpv, 0);
        return;
    }
    start_item(a, P.active, n);
}

static void set_paused(App *a, bool paused)
{
    if (video_item_active(a)) {
        video_set_pause(&a->video, paused);
        return;
    }
    if (P.ab == BK_NONE) {
        /* nothing is loaded: start the active queue from its current (or first) item */
        Queue *q = AQ();
        if (!paused && q->n) player_play_index(a, P.active, q->cur >= 0 ? q->cur : 0);
        return;
    }
    P.paused = paused;
    P.music_suspended = false;
    if (P.ab == BK_ENGINE) audio_set_paused(paused);
    else video_set_pause(&P.mpv, paused);
    if (paused) save_audio_progress(a, false);
}

void player_toggle_pause(App *a)
{
    set_paused(a, !is_paused(a) ? true : false);
}

static void seek_to(App *a, double s)
{
    if (s < 0) s = 0;
    double d = dur_now(a);
    if (d > 0 && s > d - 0.5) s = d - 0.5;
    if (video_item_active(a)) video_seek_abs(&a->video, s);
    else if (P.ab == BK_ENGINE) audio_seek(s);
    else if (P.ab == BK_MPV) video_seek_abs(&P.mpv, s);
}

void player_seek_relative(App *a, double seconds) { seek_to(a, pos_now(a) + seconds); }

/* Stop pauses; it never clears the queue. */
void player_stop(App *a)
{
    flush_progress(a);
    if (video_item_active(a)) video_set_pause(&a->video, true);
    else set_paused(a, true);
}

void player_set_volume(App *a, int percent)
{
    a->set.volume = SDL_clamp(percent, 0, 100);
    if (P.ab == BK_ENGINE) audio_set_volume(a->set.volume);
    if (P.ab == BK_MPV) video_set_volume(&P.mpv, a->set.volume);
}

/* What the system (media keys, now playing) may see and do. */
bool player_has_item(const App *a) { return have_item(a); }
bool player_is_paused(const App *a) { return is_paused(a); }
double player_position(const App *a) { return pos_now(a); }
double player_duration(const App *a) { return dur_now(a); }
const QueueItem *player_current(void) { return cur_item(); }

void player_clear_queue(App *a, int qid)
{
    Queue *q = &Q[qid];
    if (P.active == qid) {
        flush_progress(a);
        if (qid == QUEUE_VIDEO) stop_video_window(a); else stop_audio(a);
    }
    queue_clear(q);
}

/* The Stop button of the video pill: close the window; the video queue stays. */
void player_close_video(App *a) { stop_video_window(a); }

void queue_confirmed_clear(App *a, int queue) { player_clear_queue(a, queue); }

/* ------------------------------------------------------------------------ */
/* Items from the library                                                     */
/* ------------------------------------------------------------------------ */

static void strip(const char *name, char *out, size_t cap)
{
    const char *p = name;
    const char *q = p;
    while (*q >= '0' && *q <= '9') q++;
    if (q > p && q - p <= 3) {
        while (*q == ' ' || *q == '-' || *q == '.' || *q == '_') q++;
        if (*q) p = q;
    }
    SDL_strlcpy(out, p, cap);
    char *dot = SDL_strrchr(out, '.');
    if (dot && dot != out) *dot = 0;
}

/* Fill a queue item from a library file. `sub` and `art` are optional. */
void qitem_from_node(App *a, QueueItem *it, int root, const FsNode *f, const char *sub, const char *art)
{
    memset(it, 0, sizeof *it);
    it->kind = f->kind == MK_VIDEO ? QK_VIDEO : (root == ROOT_BOOKS || f->kind == MK_BOOK) ? QK_BOOK : QK_SONG;
    it->root = root;
    app_full_path(a, root, f, it->path, sizeof it->path);
    fs_relpath(f, it->rel, sizeof it->rel);
    it->size = f->size;
    it->mode = QMODE_WINDOW;
    strip(f->name, it->title, sizeof it->title);
    SDL_strlcpy(it->sub, sub ? sub : "", sizeof it->sub);
    SDL_strlcpy(it->art, art ? art : "", sizeof it->art);
    if (it->kind == QK_VIDEO) return;
    /* embedded tags win over folder and file names */
    const TagInfo *t = tag_get(a, root, f);
    if (t) {
        if (t->title[0]) SDL_strlcpy(it->title, t->title, sizeof it->title);
        const char *who = t->artist[0] ? t->artist : t->album_artist;
        if (who[0] && t->album[0]) SDL_snprintf(it->sub, sizeof it->sub, "%s \xC2\xB7 %s", who, t->album);
        else if (who[0]) SDL_strlcpy(it->sub, who, sizeof it->sub);
        else if (t->album[0]) SDL_strlcpy(it->sub, t->album, sizeof it->sub);
        char key[THC_PATH_MAX + 8];
        if (tag_art_key(a, root, f, key, sizeof key)) SDL_strlcpy(it->art, key, sizeof it->art);
    }
}

static bool playable(const FsNode *n, bool books)
{
    if (n->dir) return false;
    return n->kind == MK_AUDIO || n->kind == MK_AUDIO_MPV || (books && n->kind == MK_BOOK);
}

int music_tracks(const App *a, int root, const FsNode *dir, const FsNode **out, int max)
{
    bool books = root == ROOT_BOOKS;
    int n = 0;
    for (int i = 0; i < dir->nkids && n < max; i++)
        if (playable(dir->kids[i], books)) out[n++] = dir->kids[i];
    if (n < 2 || n > MAXQ) return n;
    /* tagged tracks by disc and track, then the untagged ones by file name (#20) */
    const TagInfo *t[MAXQ];
    int order[MAXQ];
    const FsNode *copy[MAXQ];
    for (int i = 0; i < n; i++) {
        t[i] = tag_get(a, root, out[i]);
        copy[i] = out[i];
    }
    tags_sort_order(t, n, order);
    for (int i = 0; i < n; i++) out[i] = copy[order[i]];
    return n;
}

/* The items of a folder (an album, a book) or a single file, in play order. Returns the count. */
int player_items_for(App *a, int root, const FsNode *dir, QueueItem *out, int max)
{
    char sub[256] = "", art[THC_PATH_MAX] = "";
    const FsNode *an = dir->dir ? fs_find_art(dir) : (dir->parent ? fs_find_art(dir->parent) : NULL);
    if (an) app_full_path(a, root, an, art, sizeof art);
    int n = 0;
    if (dir->dir) {
        if (dir->parent && dir->parent->parent) SDL_snprintf(sub, sizeof sub, "%s \xC2\xB7 %s", dir->parent->name, dir->name);
        else SDL_strlcpy(sub, dir->name, sizeof sub);
        const FsNode *list[MAXQ];
        int nl = music_tracks(a, root, dir, list, MAXQ);
        for (int i = 0; i < nl && n < max; i++) qitem_from_node(a, &out[n++], root, list[i], sub, art);
    } else if (max > 0) {
        if (dir->parent && dir->parent->parent) SDL_strlcpy(sub, dir->parent->name, sizeof sub);
        qitem_from_node(a, &out[n++], root, dir, sub, art);
    }
    return n;
}

void music_play_folder(App *a, int root, const FsNode *dir, int track_index)
{
    QueueItem *items = SDL_malloc(sizeof *items * MAXQ);
    if (!items) return;
    int n = player_items_for(a, root, dir, items, MAXQ);
    if (n > 0) player_play_items(a, QUEUE_MUSIC, items, n, dir->dir ? track_index : 0);
    SDL_free(items);
}

/* Play one video now (an episode, a movie). */
void app_play_video(App *a, int root, const FsNode *file)
{
    QueueItem it;
    qitem_from_node(a, &it, root, file, "", "");
    player_play_now(a, &it);
}

bool music_is_current(App *a, const FsNode *track)
{
    (void)a;
    const Queue *q = &Q[QUEUE_MUSIC];
    if (P.ab == BK_NONE || q->cur < 0 || q->cur >= q->n) return false;
    char rel[THC_PATH_MAX];
    fs_relpath(track, rel, sizeof rel);
    return !strcmp(rel, q->items[q->cur].rel) && track->size == q->items[q->cur].size;
}

/* ------------------------------------------------------------------------ */
/* Frame work                                                                 */
/* ------------------------------------------------------------------------ */

static void media_init(App *a);
static void media_pump(App *a);
static void media_shutdown(void);

void music_init(App *a)
{
    (void)a;
    queue_init(&Q[QUEUE_MUSIC], QUEUE_MUSIC);
    queue_init(&Q[QUEUE_VIDEO], QUEUE_VIDEO);
    for (int i = 0; i < QUEUE_COUNT; i++) {
        Q[i].repeat = a->set.queue_repeat;
        Q[i].shuffle = a->set.queue_shuffle;
    }
    P.seek_preview = -1;
    media_init(a);
    P.last_tick = SDL_GetTicks() / 1000.0;
}

void music_shutdown(App *a)
{
    flush_progress(a);
    media_shutdown();
    stop_video_window(a);
    stop_audio(a);
    audio_shutdown();
    queue_free(&Q[QUEUE_MUSIC]);
    queue_free(&Q[QUEUE_VIDEO]);
}

bool music_busy(App *a)
{
    return (P.ab != BK_NONE && !P.paused) || a->video.active || P.next_at > 0;
}

void music_pump(App *a)
{
    double now = SDL_GetTicks() / 1000.0;
    double dt = now - P.last_tick;
    P.last_tick = now;
    if (dt > 1.0) dt = 0;

    /* the video window */
    if (a->video.h) {
        int f = video_pump(&a->video);
        apply_ontop(a);
        QueueItem *it = cur_item();
        if (f & (VIDEO_EV_PAUSED | VIDEO_EV_ENDED)) save_video_progress(a);
        else if ((f & VIDEO_EV_PROGRESS) && !P.video_idle) save_video_progress(a);
        if (f & VIDEO_EV_FILE_ENDED) {
            if (a->video.failed) {
                app_toast(a, "%s", a->video.error);
                a->video.failed = false;
                P.video_idle = true;
                advance(a, QUEUE_VIDEO);
            } else {
                item_finished(a, QUEUE_VIDEO);
            }
        }
        if (f & VIDEO_EV_ENDED) {
            /* the window was closed (or mpv failed): playback stops, the queue stays */
            if (a->video.failed && a->video.pos <= 0) app_toast(a, "%s", a->video.error);
            a->play_key[0] = 0;
            P.video_idle = false;
            (void)it;
        }
    }
    /* the "next episode" countdown */
    if (P.next_at > 0 && now >= P.next_at) {
        int q = P.next_queue;
        P.next_at = 0;
        advance(a, q);
    }
    /* audio backends */
    if (P.ab == BK_ENGINE) {
        if (!P.paused) P.listened += dt;
        if (audio_take_finished()) item_finished(a, P.active);
    } else if (P.ab == BK_MPV) {
        int f = video_pump(&P.mpv);
        if (!P.paused) P.listened += dt;
        if (f & VIDEO_EV_PAUSED) save_audio_progress(a, false);
        if (f & VIDEO_EV_ENDED) {
            bool eof = P.mpv.reached_eof;
            if (eof) {
                P.ab = BK_NONE;
                item_finished(a, P.active);
            } else {
                save_audio_progress(a, false);
                P.ab = BK_NONE;
                P.paused = true;
            }
        }
    }
    /* a song counts as played at the threshold (a percentage of its length) */
    QueueItem *it = cur_item();
    if (it && it->kind == QK_SONG && P.ab != BK_NONE && !P.paused && !P.play_counted) {
        double d = dur_now(a);
        if (history_play_counts(P.listened, d, a->set.play_threshold_music)) count_play(a, it, P.listened);
    }
    if (P.ab != BK_NONE && !P.paused) {
        Uint64 tick = SDL_GetTicks();
        if (tick - P.last_save > 5000) {
            P.last_save = tick;
            save_audio_progress(a, false);
        }
        if (P.sleep >= SLEEP_15 && P.sleep <= SLEEP_60 && a->ui.time >= P.sleep_until) {
            P.sleep = SLEEP_OFF;
            set_paused(a, true);
            app_toast(a, "Sleep timer: paused");
        }
    }
    media_pump(a);
}

bool music_hotkey(App *a, Action act)
{
    if (!have_item(a) && !AQ()->n) return false;
    double skip = book_mode() ? a->set.skip_seconds : 10;
    switch (act) {
    case ACT_PLAY_PAUSE: set_paused(a, !is_paused(a)); return true;
    case ACT_NEXT_TRACK: player_next(a); return true;
    case ACT_PREV_TRACK: player_prev(a); return true;
    case ACT_SEEK_BACK: seek_to(a, pos_now(a) - skip); return true;
    case ACT_SEEK_FORWARD: seek_to(a, pos_now(a) + skip); return true;
    case ACT_VOLUME_UP:
    case ACT_VOLUME_DOWN:
        player_set_volume(a, a->set.volume + (act == ACT_VOLUME_UP ? 5 : -5));
        app_toast(a, "Volume %d%%", a->set.volume);
        return true;
    default: return false;
    }
}


/* ------------------------------------------------------------------------ */
/* Media keys and now playing                                                 */
/* ------------------------------------------------------------------------ */

static const MediaBackend *MB;
static App *mb_app;
static NowPlaying mb_last;
static bool mb_have_last;

static void media_action(int act, double arg, void *ud)
{
    App *a = ud;
    switch (act) {
    case MKA_PLAY: set_paused(a, false); break;
    case MKA_PAUSE: set_paused(a, true); break;
    case MKA_TOGGLE: set_paused(a, !is_paused(a)); break;
    case MKA_NEXT: player_next(a); break;
    case MKA_PREV: player_prev(a); break;
    case MKA_STOP: player_stop(a); break;
    case MKA_SEEK_REL: seek_to(a, pos_now(a) + arg); break;
    case MKA_SEEK_ABS: seek_to(a, arg); break;
    default: break;
    }
}

/* Keyboard media keys while THC's window has focus (every platform; the backend covers the rest). */
bool music_media_key(App *a, SDL_Keycode k)
{
    if (a->set.mediakeys_mode == KEYS_OFF || (!have_item(a) && !AQ()->n)) return false;
    int act = 0;
    switch (k) {
    case SDLK_MEDIA_PLAY: act = MKA_PLAY; break;
    case SDLK_MEDIA_PAUSE: act = MKA_PAUSE; break;
    case SDLK_MEDIA_PLAY_PAUSE: act = MKA_TOGGLE; break;
    case SDLK_MEDIA_NEXT_TRACK: act = MKA_NEXT; break;
    case SDLK_MEDIA_PREVIOUS_TRACK: act = MKA_PREV; break;
    case SDLK_MEDIA_STOP: act = MKA_STOP; break;
    case SDLK_MEDIA_FAST_FORWARD:
        seek_to(a, pos_now(a) + mediakeys_seek_seconds(&a->set, book_mode() ? KIND_BOOK : KIND_MUSIC));
        return true;
    case SDLK_MEDIA_REWIND:
        seek_to(a, pos_now(a) - mediakeys_seek_seconds(&a->set, book_mode() ? KIND_BOOK : KIND_MUSIC));
        return true;
    default: return false;
    }
    media_action(act, 0, a);
    return true;
}

static void media_init(App *a)
{
    MB = mediakeys_linux_backend();
    mb_app = a;
    if (MB && !MB->init(media_action, a)) MB = NULL;
    if (MB) SDL_Log("media keys: %s", MB->name);
}

static void media_pump(App *a)
{
    if (!MB) return;
    const QueueItem *it = cur_item();
    int kind = !it ? KIND_MUSIC : it->kind == QK_VIDEO ? KIND_VIDEO : it->kind == QK_BOOK ? KIND_BOOK : KIND_MUSIC;
    NowPlaying np;
    /* items keep "artist · album" in one field; the system wants them apart */
    char artist[160] = "", album[160] = "";
    if (it) {
        SDL_strlcpy(artist, it->sub, sizeof artist);
        char *sep = SDL_strstr(artist, " \xC2\xB7 ");
        if (sep) {
            *sep = 0;
            SDL_strlcpy(album, sep + 3, sizeof album);
        }
    }
    mediakeys_build(&np, &a->set, kind, have_item(a), !is_paused(a), it ? it->title : "", artist, album,
                    it ? it->art : "", pos_now(a), dur_now(a));
    if (a->set.mediakeys_mode != KEYS_GLOBAL) np.claimed = false;   /* "focused": only THC's own window */
    if (!mb_have_last || memcmp(&np, &mb_last, sizeof np) != 0) {
        MB->update(&np);
        mb_last = np;
        mb_have_last = true;
    }
    MB->pump();
}

static void media_shutdown(void)
{
    if (MB) MB->shutdown();
    MB = NULL;
    (void)mb_app;
}

/* ------------------------------------------------------------------------ */
/* Now-playing bar                                                            */
/* ------------------------------------------------------------------------ */

float music_bar_height(App *a)
{
    return have_item(a) && P.ab != BK_NONE ? dp(&a->ui, 96) : 0;
}

static void set_speed(App *a, double s)
{
    a->set.book_speed = s;
    if (P.ab == BK_MPV) video_set_speed(&P.mpv, s);
    if (P.ab == BK_ENGINE) audio_set_speed(book_mode() ? s : 1.0);
    app_save_settings(a);
}

void music_bar(App *a)
{
    if (!have_item(a) || P.ab == BK_NONE) return;
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float h = music_bar_height(a);
    SDL_FRect bar = { 0, ui->h - h, (float)ui->w, h };
    ui_fill(ui, bar, t->high_contrast ? t->bg : with_alpha(color_mix(t->bg, t->surface, 0.6f), 250), 0);
    ui_fill(ui, (SDL_FRect){ 0, bar.y, (float)ui->w, 1 }, with_alpha(t->border, 200), 0);
    QueueItem *it = cur_item();
    bool playing = P.ab != BK_NONE && !P.paused;
    float pad = dp(ui, 20);

    /* left: art and titles */
    float cs = h - dp(ui, 24);
    SDL_FRect cov = { pad, bar.y + dp(ui, 12), cs, cs };
    draw_art(a, cov, it->sub[0] ? it->sub : it->title, it->art[0] ? it->art : NULL, dp(ui, 10));
    if (!it->art[0]) ui_icon(ui, it->kind == QK_BOOK ? ICON_BOOK : ICON_NOTE,
                             (SDL_FRect){ cov.x + cs * 0.28f, cov.y + cs * 0.28f, cs * 0.44f, cs * 0.44f },
                             rgba(0xFFFFFF, 220));
    float lw = SDL_min(ui->w * 0.26f, dp(ui, 320));
    float tx = cov.x + cs + dp(ui, 14);
    float lh = ui_line_h(ui, TS_BODY);
    ui_text_fit(ui, FONT_BOLD, TS_BODY, tx, bar.y + h / 2 - lh, lw - cs - dp(ui, 14), t->text, it->title);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, tx, bar.y + h / 2, lw - cs - dp(ui, 14), t->text_dim, it->sub);

    /* center: transport and seek bar */
    /* room on the right: volume, plus speed and sleep timer for books */
    float right_w = dp(ui, book_mode() ? 330 : 180);
    float avail = ui->w - lw - right_w - 2 * pad;
    float cw = SDL_clamp(avail, dp(ui, 260), dp(ui, 620));
    float cx = lw + pad + SDL_max(0, (avail - cw) / 2);
    if (avail < dp(ui, 260)) {
        /* narrow window: drop the extras, keep the transport */
        right_w = 0;
        cw = ui->w - lw - 2 * pad;
        cx = lw + pad;
    }
    float bs = dp(ui, 40), big = dp(ui, 46);
    float by = bar.y + dp(ui, 8);
    float mid = cx + cw / 2;
    bool single_book = book_mode() && AQ()->n == 1 && P.ab == BK_MPV;
    if (ui_icon_button(ui, ui_id("pb-back"), (SDL_FRect){ mid - big / 2 - bs * 2 - dp(ui, 16), by + (big - bs) / 2, bs, bs },
                       ICON_REWIND, book_mode() ? "Skip back" : "Back 10 s"))
        music_hotkey(a, ACT_SEEK_BACK);
    if (ui_icon_button(ui, ui_id("pb-prev"), (SDL_FRect){ mid - big / 2 - bs - dp(ui, 8), by + (big - bs) / 2, bs, bs },
                       ICON_PREV, single_book ? "Previous chapter" : "Previous")) {
        if (single_book) video_command(&P.mpv, "add chapter -1");
        else music_hotkey(a, ACT_PREV_TRACK);
    }
    SDL_FRect pp = { mid - big / 2, by, big, big };
    UiItemState pst = ui_item(ui, ui_id("pb-play"), pp);
    ui_circle(ui, mid, by + big / 2, big / 2 * (pst.hover ? 1.06f : 1.0f), t->text);
    ui_icon(ui, playing ? ICON_PAUSE : ICON_PLAY,
            (SDL_FRect){ mid - big * 0.26f + (playing ? 0 : big * 0.03f), by + big * 0.24f, big * 0.52f, big * 0.52f }, t->bg2);
    if (pst.focused) ui_focus_ring(ui, pp, big / 2);
    if (pst.activated) set_paused(a, playing);
    if (ui_icon_button(ui, ui_id("pb-next"), (SDL_FRect){ mid + big / 2 + dp(ui, 8), by + (big - bs) / 2, bs, bs },
                       ICON_NEXT, single_book ? "Next chapter" : "Next")) {
        if (single_book) video_command(&P.mpv, "add chapter 1");
        else music_hotkey(a, ACT_NEXT_TRACK);
    }
    if (ui_icon_button(ui, ui_id("pb-fwd"), (SDL_FRect){ mid + big / 2 + bs + dp(ui, 16), by + (big - bs) / 2, bs, bs },
                       ICON_FORWARD, book_mode() ? "Skip forward" : "Forward 10 s"))
        music_hotkey(a, ACT_SEEK_FORWARD);

    double d = dur_now(a), p = P.seek_preview >= 0 ? P.seek_preview : pos_now(a);
    char ps[32], ds[32];
    fmt_time(p, ps, sizeof ps);
    fmt_time(d, ds, sizeof ds);
    float sy = by + big + dp(ui, 10);
    float tw = dp(ui, 56);
    ui_text_center(ui, FONT_REGULAR, TS_SMALL, (SDL_FRect){ cx, sy - dp(ui, 8), tw, dp(ui, 20) }, t->text_dim, ps, true);
    ui_text_center(ui, FONT_REGULAR, TS_SMALL, (SDL_FRect){ cx + cw - tw, sy - dp(ui, 8), tw, dp(ui, 20) }, t->text_dim,
                   d > 0 ? ds : "--:--", true);
    SDL_FRect sk = { cx + tw, sy - dp(ui, 8), cw - 2 * tw, dp(ui, 20) };
    Uint32 sid = ui_id("pb-seek");
    UiItemState sst = ui_item(ui, sid, sk);
    int lr = ui_capture_lr(ui, sid);
    if (lr) seek_to(a, pos_now(a) + lr * 5.0);
    float frac = d > 0 ? (float)(p / d) : 0;
    if (sst.pressed && d > 0) {
        float f = SDL_clamp((ui->mx - sk.x) / sk.w, 0, 1);
        P.seek_preview = f * d;
        frac = f;
    } else if (P.seek_preview >= 0) {
        seek_to(a, P.seek_preview);
        P.seek_preview = -1;
    }
    SDL_FRect track = { sk.x, sk.y + sk.h / 2 - dp(ui, 2), sk.w, dp(ui, 4) };
    ui_progress(ui, track, frac, t->accent);
    if (sst.hover || sst.pressed || sst.focused)
        ui_circle(ui, track.x + track.w * frac, track.y + track.h / 2, dp(ui, 7), t->text);
    if (sst.focused) ui_focus_ring(ui, sk, dp(ui, 10));

    /* right: book controls and volume */
    float rx = ui->w - pad;
    if (right_w > 0) {
        float vw = dp(ui, 100);
        SDL_FRect vr = { rx - vw, bar.y + h / 2 - dp(ui, 10), vw, dp(ui, 20) };
        Uint32 vid = ui_id("pb-vol");
        UiItemState vst = ui_item(ui, vid, vr);
        int vlr = ui_capture_lr(ui, vid);
        if (vlr) music_hotkey(a, vlr > 0 ? ACT_VOLUME_UP : ACT_VOLUME_DOWN);
        if (vst.pressed) {
            player_set_volume(a, (int)SDL_clamp((ui->mx - vr.x) / vr.w * 100, 0, 100));
        }
        SDL_FRect vt = { vr.x, vr.y + vr.h / 2 - dp(ui, 2), vr.w, dp(ui, 4) };
        ui_progress(ui, vt, a->set.volume / 100.0f, t->text_dim);
        if (vst.focused) ui_focus_ring(ui, vr, dp(ui, 10));
        float is = dp(ui, 22);
        ui_icon(ui, ICON_VOLUME, (SDL_FRect){ vr.x - is - dp(ui, 8), vr.y + (vr.h - is) / 2, is, is }, t->text_dim);
        rx = vr.x - is - dp(ui, 24);
    }
    if (book_mode() && right_w > 0) {
        char sp[16];
        SDL_snprintf(sp, sizeof sp, "%.2gx", a->set.book_speed);
        float w1 = dp(ui, 64);
        if (ui_pill(ui, ui_id("pb-speed"), (SDL_FRect){ rx - w1, bar.y + h / 2 - dp(ui, 17), w1, dp(ui, 34) }, sp, false)) {
            int i = 0;
            while (i < NSPEEDS && k_speeds[i] <= a->set.book_speed + 1e-6) i++;
            set_speed(a, k_speeds[i % NSPEEDS]);
            if (P.ab == BK_ENGINE && !a->have_mpv) app_toast(a, "Speed also changes pitch without libmpv");
        }
        rx -= w1 + dp(ui, 8);
        char sl[48];
        if (P.sleep == SLEEP_OFF) SDL_snprintf(sl, sizeof sl, "Sleep");
        else if (P.sleep == SLEEP_CHAPTER) SDL_snprintf(sl, sizeof sl, "Chapter end");
        else SDL_snprintf(sl, sizeof sl, "%d min", (int)((P.sleep_until - ui->time) / 60) + 1);
        float w2 = ui_text_w(ui, FONT_BOLD, TS_BODY, sl) + dp(ui, 28);
        if (ui_pill(ui, ui_id("pb-sleep"), (SDL_FRect){ rx - w2, bar.y + h / 2 - dp(ui, 17), w2, dp(ui, 34) },
                                         sl, P.sleep != SLEEP_OFF)) {
            P.sleep = (P.sleep + 1) % SLEEP_COUNT;
            static const int mins[] = { 0, 15, 30, 60, 0 };
            P.sleep_until = ui->time + mins[P.sleep] * 60.0;
            app_toast(a, "Sleep timer: %s", k_sleep_names[P.sleep]);
        }
    }
    if (playing) ui->needs_redraw = true;
}
