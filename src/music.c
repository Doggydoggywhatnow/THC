/* music.c: the music and audiobook player and its bottom bar.
 *
 * Two backends:
 *  - THC's engine (audio.c): lib-Peep with its WAV/FLAC decoders plus the
 *    MP3 and Ogg Vorbis codecs, played through SDL3.
 *  - libmpv without a window, for M4B/M4A/AAC/Opus and other formats, and
 *    for audiobooks when available (speed without pitch change, chapters).
 * Audiobook positions are kept in the same history database as videos. */
#include "app.h"
#include "audio.h"

#include <string.h>

#define MAXQ 512
enum { MODE_MUSIC = 0, MODE_BOOK = 1 };
enum { BK_NONE = 0, BK_ENGINE, BK_MPV };
enum { SLEEP_OFF = 0, SLEEP_15, SLEEP_30, SLEEP_60, SLEEP_CHAPTER, SLEEP_COUNT };
static const char *const k_sleep_names[SLEEP_COUNT] = { "Off", "15 min", "30 min", "60 min", "End of chapter" };
static const double k_speeds[] = { 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5 };
#define NSPEEDS ((int)(sizeof k_speeds / sizeof k_speeds[0]))

typedef struct QItem {
    char path[THC_PATH_MAX];
    char key[THC_PATH_MAX];  /* history key (books) */
    Sint64 size;
    char title[256];
    char sub[256];
    char art[THC_PATH_MAX];
} QItem;

static struct {
    QItem q[MAXQ];
    int n, cur;
    int mode;
    int backend;
    VideoSession mpv;
    bool paused;
    int sleep;
    double sleep_until;
    Uint64 last_save;
    double seek_preview;  /* while dragging the seek bar, else < 0 */
} P = { .seek_preview = -1 };

static bool book_mode(void) { return P.mode == MODE_BOOK; }

static double pos_now(void)
{
    if (P.backend == BK_ENGINE) return audio_position();
    if (P.backend == BK_MPV) return P.mpv.pos;
    return 0;
}

static double dur_now(void)
{
    if (P.backend == BK_ENGINE) return audio_duration();
    if (P.backend == BK_MPV) return P.mpv.duration;
    return 0;
}

static void save_book(App *a, bool reached_end)
{
    if (!book_mode() || P.n == 0 || !a->set.remember_history) return;
    QItem *it = &P.q[P.cur];
    double d = dur_now();
    double pos = reached_end && d > 0 ? d : pos_now();
    if (pos <= 0 && !reached_end) return;
    history_save(a->hist, it->key, it->size, pos, d, a->set.watched_threshold, reached_end);
    app_history_changed();
}

static void stop_backend(void)
{
    if (P.backend == BK_ENGINE) audio_unload();
    if (P.backend == BK_MPV) video_stop(&P.mpv);
    P.backend = BK_NONE;
}

static bool start_track(App *a, int i, double start)
{
    stop_backend();
    if (i < 0 || i >= P.n) return false;
    P.cur = i;
    QItem *it = &P.q[i];
    const char *name = SDL_strrchr(it->path, '/');
#ifdef _WIN32
    const char *bs = SDL_strrchr(it->path, '\\');
    if (bs > name) name = bs;
#endif
    name = name ? name + 1 : it->path;
    bool engine_ok = audio_engine_handles(name);
    /* Audiobooks prefer libmpv: speed changes keep the pitch, and M4B
     * chapters work. Music prefers THC's own engine. */
    bool use_mpv = a->have_mpv && (!engine_ok || book_mode());
    char err[256] = "";
    if (use_mpv) {
        VideoOptions o = { a->mpv_dir, start, true, a->mpv_event };
        if (video_play(&P.mpv, &a->mpv, it->path, &o)) {
            P.backend = BK_MPV;
            video_set_volume(&P.mpv, a->set.volume);
            if (book_mode()) video_set_speed(&P.mpv, a->set.book_speed);
        } else {
            SDL_strlcpy(err, P.mpv.error, sizeof err);
        }
    } else if (engine_ok) {
        audio_set_volume(a->set.volume);
        audio_set_speed(book_mode() ? a->set.book_speed : 1.0);
        if (audio_load(it->path, err, sizeof err)) {
            P.backend = BK_ENGINE;
            if (start > 0) audio_seek(start);
        }
    } else {
        SDL_snprintf(err, sizeof err, "This format needs libmpv, which was not found");
    }
    if (P.backend == BK_NONE) {
        app_toast(a, "Could not play %s: %s", it->title, err);
        return false;
    }
    P.paused = false;
    P.last_save = SDL_GetTicks();
    return true;
}

static void next_track(App *a, int dir)
{
    int i = P.cur + dir;
    if (i < 0) i = 0;
    if (i >= P.n) {
        stop_backend();
        P.paused = true;
        return;
    }
    double start = 0;
    if (book_mode()) {
        HistoryEntry e;
        if (history_get(a->hist, P.q[i].key, P.q[i].size, &e)) start = history_resume_point(&e, a->set.watched_threshold);
    }
    start_track(a, i, start);
}

static void set_paused(App *a, bool paused)
{
    if (P.backend == BK_NONE) {
        if (!paused && P.n) next_track(a, 0);
        return;
    }
    P.paused = paused;
    if (P.backend == BK_ENGINE) audio_set_paused(paused);
    else video_set_pause(&P.mpv, paused);
    if (paused) save_book(a, false);
}

static void seek_to(double s)
{
    if (s < 0) s = 0;
    double d = dur_now();
    if (d > 0 && s > d - 0.5) s = d - 0.5;
    if (P.backend == BK_ENGINE) audio_seek(s);
    else if (P.backend == BK_MPV) video_seek_abs(&P.mpv, s);
}

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

static void fill_item(App *a, QItem *it, int root, const FsNode *f, const char *sub, const char *art)
{
    app_full_path(a, root, f, it->path, sizeof it->path);
    fs_relpath(f, it->key, sizeof it->key);
    it->size = f->size;
    strip(f->name, it->title, sizeof it->title);
    SDL_strlcpy(it->sub, sub ? sub : "", sizeof it->sub);
    SDL_strlcpy(it->art, art ? art : "", sizeof it->art);
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
    if (n < 2) return n;
    /* order by disc and track number when every file has a track number;
     * otherwise keep the file-name order */
    const TagInfo *t[MAXQ];
    for (int i = 0; i < n && i < MAXQ; i++) {
        t[i] = tag_get(a, root, out[i]);
        if (!t[i] || t[i]->track <= 0) return n;
    }
    if (n > MAXQ) return n;
    for (int i = 1; i < n; i++) {   /* stable insertion sort */
        const FsNode *f = out[i];
        const TagInfo *ti = t[i];
        int j = i - 1;
        while (j >= 0 && tags_order(t[j], ti) > 0) {
            out[j + 1] = out[j];
            t[j + 1] = t[j];
            j--;
        }
        out[j + 1] = f;
        t[j + 1] = ti;
    }
    return n;
}

void music_play_folder(App *a, int root, const FsNode *dir, int track_index)
{
    if (P.backend != BK_NONE) {
        save_book(a, false);
        stop_backend();
    }
    bool books = root == ROOT_BOOKS;
    P.mode = books ? MODE_BOOK : MODE_MUSIC;
    P.n = 0;
    char sub[256] = "", art[THC_PATH_MAX] = "";
    const FsNode *an = dir->dir ? fs_find_art(dir) : (dir->parent ? fs_find_art(dir->parent) : NULL);
    if (an) app_full_path(a, root, an, art, sizeof art);
    if (dir->dir) {
        if (dir->parent && dir->parent->parent) SDL_snprintf(sub, sizeof sub, "%s \xC2\xB7 %s", dir->parent->name, dir->name);
        else SDL_strlcpy(sub, dir->name, sizeof sub);
        const FsNode *list[MAXQ];
        int nl = music_tracks(a, root, dir, list, MAXQ);
        for (int i = 0; i < nl && P.n < MAXQ; i++) fill_item(a, &P.q[P.n++], root, list[i], sub, art);
    } else {
        if (dir->parent && dir->parent->parent) SDL_strlcpy(sub, dir->parent->name, sizeof sub);
        fill_item(a, &P.q[P.n++], root, dir, sub, art);
        track_index = 0;
    }
    if (!P.n) return;
    if (track_index < 0 || track_index >= P.n) track_index = 0;
    double start = 0;
    if (books) {
        HistoryEntry e;
        if (history_get(a->hist, P.q[track_index].key, P.q[track_index].size, &e))
            start = history_resume_point(&e, a->set.watched_threshold);
    }
    start_track(a, track_index, start);
}

bool music_is_current(App *a, const FsNode *track)
{
    if (!P.n || P.backend == BK_NONE) return false;
    char rel[THC_PATH_MAX];
    fs_relpath(track, rel, sizeof rel);
    (void)a;
    return !strcmp(rel, P.q[P.cur].key) && track->size == P.q[P.cur].size;
}

void music_init(App *a)
{
    (void)a;
    P.seek_preview = -1;
}

void music_shutdown(App *a)
{
    save_book(a, false);
    stop_backend();
    audio_shutdown();
}

bool music_busy(App *a)
{
    (void)a;
    return P.backend != BK_NONE && !P.paused;
}

void music_pump(App *a)
{
    if (P.backend == BK_ENGINE) {
        if (audio_take_finished()) {
            if (P.sleep == SLEEP_CHAPTER) {
                P.sleep = SLEEP_OFF;
                save_book(a, true);
                stop_backend();
                P.paused = true;
                return;
            }
            save_book(a, true);
            next_track(a, 1);
        }
    } else if (P.backend == BK_MPV) {
        int f = video_pump(&P.mpv);
        if (f & VIDEO_EV_PAUSED) save_book(a, false);
        if (f & VIDEO_EV_ENDED) {
            bool eof = P.mpv.reached_eof;
            save_book(a, eof);
            P.backend = BK_NONE;
            if (eof && P.sleep != SLEEP_CHAPTER) next_track(a, 1);
            else {
                if (P.sleep == SLEEP_CHAPTER) P.sleep = SLEEP_OFF;
                P.paused = true;
            }
        }
    }
    if (P.backend != BK_NONE && !P.paused) {
        Uint64 now = SDL_GetTicks();
        if (now - P.last_save > 5000) {
            P.last_save = now;
            save_book(a, false);
        }
        if (P.sleep >= SLEEP_15 && P.sleep <= SLEEP_60 && a->ui.time >= P.sleep_until) {
            P.sleep = SLEEP_OFF;
            set_paused(a, true);
            app_toast(a, "Sleep timer: paused");
        }
    }
}

bool music_hotkey(App *a, Action act)
{
    if (!P.n) return false;
    double skip = book_mode() ? a->set.skip_seconds : 10;
    switch (act) {
    case ACT_PLAY_PAUSE: set_paused(a, P.backend != BK_NONE && !P.paused); return true;
    case ACT_NEXT_TRACK: save_book(a, false); next_track(a, 1); return true;
    case ACT_PREV_TRACK:
        if (pos_now() > 3) seek_to(0);
        else { save_book(a, false); next_track(a, -1); }
        return true;
    case ACT_SEEK_BACK: seek_to(pos_now() - skip); return true;
    case ACT_SEEK_FORWARD: seek_to(pos_now() + skip); return true;
    case ACT_VOLUME_UP:
    case ACT_VOLUME_DOWN:
        a->set.volume = SDL_clamp(a->set.volume + (act == ACT_VOLUME_UP ? 5 : -5), 0, 100);
        if (P.backend == BK_ENGINE) audio_set_volume(a->set.volume);
        if (P.backend == BK_MPV) video_set_volume(&P.mpv, a->set.volume);
        app_toast(a, "Volume %d%%", a->set.volume);
        return true;
    default: return false;
    }
}

/* ------------------------------------------------------------------------ */
/* Now-playing bar                                                            */
/* ------------------------------------------------------------------------ */

float music_bar_height(App *a)
{
    return P.n ? dp(&a->ui, 96) : 0;
}

static void set_speed(App *a, double s)
{
    a->set.book_speed = s;
    if (P.backend == BK_MPV) video_set_speed(&P.mpv, s);
    if (P.backend == BK_ENGINE) audio_set_speed(book_mode() ? s : 1.0);
    app_save_settings(a);
}

void music_bar(App *a)
{
    if (!P.n) return;
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float h = music_bar_height(a);
    SDL_FRect bar = { 0, ui->h - h, (float)ui->w, h };
    ui_fill(ui, bar, t->high_contrast ? t->bg : with_alpha(color_mix(t->bg, t->surface, 0.6f), 250), 0);
    ui_fill(ui, (SDL_FRect){ 0, bar.y, (float)ui->w, 1 }, with_alpha(t->border, 200), 0);
    QItem *it = &P.q[P.cur];
    bool playing = P.backend != BK_NONE && !P.paused;
    float pad = dp(ui, 20);

    /* left: art and titles */
    float cs = h - dp(ui, 24);
    SDL_FRect cov = { pad, bar.y + dp(ui, 12), cs, cs };
    draw_art(a, cov, it->sub[0] ? it->sub : it->title, it->art[0] ? it->art : NULL, dp(ui, 10));
    if (!it->art[0]) ui_icon(ui, book_mode() ? ICON_BOOK : ICON_NOTE,
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
    bool single_book = book_mode() && P.n == 1 && P.backend == BK_MPV;
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

    double d = dur_now(), p = P.seek_preview >= 0 ? P.seek_preview : pos_now();
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
    if (lr) seek_to(pos_now() + lr * 5.0);
    float frac = d > 0 ? (float)(p / d) : 0;
    if (sst.pressed && d > 0) {
        float f = SDL_clamp((ui->mx - sk.x) / sk.w, 0, 1);
        P.seek_preview = f * d;
        frac = f;
    } else if (P.seek_preview >= 0) {
        seek_to(P.seek_preview);
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
            a->set.volume = (int)SDL_clamp((ui->mx - vr.x) / vr.w * 100, 0, 100);
            if (P.backend == BK_ENGINE) audio_set_volume(a->set.volume);
            if (P.backend == BK_MPV) video_set_volume(&P.mpv, a->set.volume);
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
            if (P.backend == BK_ENGINE && !a->have_mpv) app_toast(a, "Speed also changes pitch without libmpv");
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
