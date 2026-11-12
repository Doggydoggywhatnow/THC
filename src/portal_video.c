/* portal_video.c: the video portal ("Cinema" look).
 *
 * Home: a featured banner, Continue watching, Up next, one row per section
 * (Anime, Television, Cartoons, Movies) and Recently added. A section page
 * shows everything in it with a sort choice; a show page has season tabs and
 * episodes with their playback bar and checkmark. "Browse folders" shows the
 * folders exactly as they are on disk. */
#include "app.h"

#include <stdlib.h>
#include <string.h>

enum { VK_HOME = 0, VK_SECTION, VK_SHOW, VK_FOLDER };

/* ------------------------------------------------------------------------ */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------ */

static void mark_focus(App *a, const FsNode *f, int root, UiItemState st)
{
    if (st.focused || st.hover) {
        a->focus_file = f;
        a->focus_root = root;
    }
}

static void art_path(App *a, int root, const FsNode *art, char *buf, size_t cap)
{
    buf[0] = 0;
    if (art) app_full_path(a, root, art, buf, cap);
}

typedef struct ShowStat {
    int total, watched;
    const Episode *next;      /* what "Play" should start */
    int next_season;
    bool next_in_progress;
    Sint64 last_updated;
} ShowStat;

static void show_stat(App *a, const Show *sh, ShowStat *st)
{
    SDL_zerop(st);
    const Episode *last_watched = NULL, *in_prog = NULL;
    int lw_season = 0, ip_season = 0;
    Sint64 lw_time = -1, ip_time = -1;
    for (int s = 0; s < sh->nseasons; s++)
        for (int e = 0; e < sh->seasons[s].neps; e++) {
            const Episode *ep = &sh->seasons[s].eps[e];
            HistoryEntry h;
            st->total++;
            if (!app_history(a, ep->file, &h)) continue;
            if (h.updated > st->last_updated) st->last_updated = h.updated;
            if (h.watched) {
                st->watched++;
                if (h.updated >= lw_time) { lw_time = h.updated; last_watched = ep; lw_season = s; }
            }
            if (!h.watched && h.position > 5 && h.updated > ip_time) {
                ip_time = h.updated;
                in_prog = ep;
                ip_season = s;
            }
        }
    if (in_prog && ip_time >= lw_time) {
        st->next = in_prog;
        st->next_season = ip_season;
        st->next_in_progress = true;
        return;
    }
    /* the first unwatched episode after the last one watched */
    bool after = last_watched == NULL;
    for (int s = 0; s < sh->nseasons; s++)
        for (int e = 0; e < sh->seasons[s].neps; e++) {
            const Episode *ep = &sh->seasons[s].eps[e];
            if (ep == last_watched) { after = true; continue; }
            if (!after) continue;
            HistoryEntry h;
            if (!app_history(a, ep->file, &h) || !h.watched) {
                st->next = ep;
                st->next_season = s;
                return;
            }
        }
    (void)lw_season;
    if (sh->nseasons && sh->seasons[0].neps) {
        st->next = &sh->seasons[0].eps[0];
        st->next_season = 0;
    }
}

static void episode_line(const Show *sh, int season, const Episode *ep, char *buf, size_t cap)
{
    const Season *s = &sh->seasons[season];
    if (s->number > 0) {
        if (ep->title[0]) SDL_snprintf(buf, cap, "S%d \xC2\xB7 E%d  %s", s->number, ep->number, ep->title);
        else SDL_snprintf(buf, cap, "Season %d \xC2\xB7 %s", s->number, ep->label);
    } else {
        if (ep->title[0]) SDL_snprintf(buf, cap, "%s \xC2\xB7 %s", ep->label, ep->title);
        else SDL_snprintf(buf, cap, "%s \xC2\xB7 %s", s->name, ep->label);
    }
}

static void open_show(App *a, const Show *sh, int season)
{
    nav_push_kind(a, &a->vnav, VK_SHOW, season, sh->root, sh->key);
}

static const Show *show_by_key(App *a, const char *key)
{
    for (int i = 0; i < a->vlib.nshows; i++)
        if (!strcmp(a->vlib.shows[i].key, key)) return &a->vlib.shows[i];
    return NULL;
}

void portal_video_open_show(App *a, const char *show_key)
{
    const Show *sh = show_by_key(a, show_key);
    if (!sh) return;
    app_go(a, SCREEN_VIDEO);
    nav_reset(&a->vnav);
    open_show(a, sh, -1);
}

/* ------------------------------------------------------------------------ */
/* Cards                                                                      */
/* ------------------------------------------------------------------------ */

typedef struct Entry {
    const Show *show;
    const Movie *movie;
} Entry;

static const char *entry_title(const Entry *e) { return e->show ? e->show->title : e->movie->title; }

/* Poster card (2:3) with title, details and the watch status under it. */
static bool poster_card(App *a, SDL_FRect r, const Entry *en)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    const void *key = en->show ? (const void *)en->show : (const void *)en->movie;
    Uint32 id = ui_idp("poster", key);
    float ph = r.w * 1.5f;
    SDL_FRect poster = { r.x, r.y, r.w, ph };
    UiItemState st = ui_item(ui, id, poster);
    if (en->movie) mark_focus(a, en->movie->file, en->movie->root, st);
    if (st.context) {
        float cx, cy;
        ctx_pos(ui, poster, &cx, &cy);
        if (en->movie) queue_menu_item(a, cx, cy, en->movie->root, en->movie->file);
        else queue_menu_show(a, cx, cy, en->show->key, -1);
    }
    if (!ui_visible(ui, (SDL_FRect){ r.x - dp(ui, 10), r.y - dp(ui, 10), r.w + dp(ui, 20), r.h + dp(ui, 20) }))
        return st.activated;
    float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
    SDL_FRect p = { poster.x - lift * dp(ui, 4), poster.y - lift * dp(ui, 6), poster.w + lift * dp(ui, 8),
                    poster.h + lift * dp(ui, 8) };
    ui_shadow(ui, p, dp(ui, 14), dp(ui, 18 + lift * 18), t->shadow);
    char art[THC_PATH_MAX];
    int root = en->show ? en->show->root : en->movie->root;
    art_path(a, root, en->show ? en->show->art : en->movie->art, art, sizeof art);
    if (!art[0]) {
        /* no poster: use a frame of the movie, or of the show's first episode */
        const FsNode *first = en->movie ? en->movie->file
                              : (en->show->nseasons && en->show->seasons[0].neps ? en->show->seasons[0].eps[0].file : NULL);
        if (first) thumb_get(a, root, first, art, sizeof art);
    }
    draw_cover(a, p, entry_title(en), art[0] ? art : NULL, dp(ui, 14), en->show ? ICON_TV : ICON_FILM);
    if (st.focused) ui_focus_ring(ui, p, dp(ui, 14));

    float y = r.y + ph + dp(ui, 12);
    ui_text_fit(ui, FONT_BOLD, TS_BODY, r.x, y, r.w, t->text, entry_title(en));
    y += ui_line_h(ui, TS_BODY);
    char meta[96];
    float frac = 0;
    bool done = false;
    if (en->show) {
        ShowStat ss;
        show_stat(a, en->show, &ss);
        int ns = en->show->nseasons;
        if (ss.watched && ss.watched < ss.total)
            SDL_snprintf(meta, sizeof meta, "%d of %d watched", ss.watched, ss.total);
        else
            SDL_snprintf(meta, sizeof meta, "%d season%s \xC2\xB7 %d ep", ns, ns == 1 ? "" : "s", ss.total);
        frac = ss.total ? (float)ss.watched / ss.total : 0;
        done = ss.total && ss.watched == ss.total;
    } else {
        HistoryEntry h;
        bool have = app_history(a, en->movie->file, &h);
        if (en->movie->year) SDL_snprintf(meta, sizeof meta, "%d \xC2\xB7 Movie", en->movie->year);
        else SDL_snprintf(meta, sizeof meta, "Movie");
        if (have && h.duration > 0) {
            char d[32];
            fmt_time(h.duration, d, sizeof d);
            SDL_snprintf(meta + strlen(meta), sizeof meta - strlen(meta), " \xC2\xB7 %s", d);
        }
        frac = have ? (float)history_progress(&h) : 0;
        done = have && h.watched;
    }
    {
        const FsNode *wn = en->movie ? en->movie->file : en->show->dir;
        if (!wn && en->show && en->show->nseasons && en->show->seasons[0].neps) wn = en->show->seasons[0].eps[0].file;
        app_nsfw_why(a, root, wn, st.hover || st.focused, poster, meta, sizeof meta);
    }
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x, y, r.w, t->text_dim, meta);
    y += ui_line_h(ui, TS_SMALL) + dp(ui, 6);
    ui_watch_status(ui, (SDL_FRect){ r.x, y, r.w, dp(ui, 16) }, frac, done, t->accent);
    return st.activated;
}

static float poster_h(Ui *ui, float w)
{
    return w * 1.5f + dp(ui, 12) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL) + dp(ui, 26);
}

static void activate_entry(App *a, const Entry *en)
{
    if (en->show) open_show(a, en->show, -1);
    else app_play_video(a, en->movie->root, en->movie->file);
}

/* Landscape card for an episode or movie in progress ("Continue watching",
 * "Up next"). */
static bool wide_card(App *a, SDL_FRect r, Uint32 id, const char *cover_title, const char *art,
                      const char *line1, const char *line2, float frac, bool show_bar, const FsNode *file, int root)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float ch = r.w * 9 / 16;
    SDL_FRect card = { r.x, r.y, r.w, ch };
    UiItemState st = ui_item(ui, id, card);
    mark_focus(a, file, root, st);
    if (st.context && file) {
        float cx, cy;
        ctx_pos(ui, card, &cx, &cy);
        queue_menu_item(a, cx, cy, root, file);
    }
    if (!ui_visible(ui, (SDL_FRect){ r.x - dp(ui, 10), r.y - dp(ui, 10), r.w + dp(ui, 20), r.h + dp(ui, 20) }))
        return st.activated;
    float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
    SDL_FRect c = { card.x - lift * dp(ui, 4), card.y - lift * dp(ui, 4), card.w + lift * dp(ui, 8),
                    card.h + lift * dp(ui, 8) };
    ui_shadow(ui, c, dp(ui, 14), dp(ui, 20), t->shadow);
    char tpath[THC_PATH_MAX];
    if (file && thumb_get(a, root, file, tpath, sizeof tpath)) art = tpath;   /* this episode's own frame */
    draw_art(a, c, cover_title, art, dp(ui, 14));
    if (!t->high_contrast) ui_fill_grad(ui, c, rgba(0, 0), rgba(0, 120), dp(ui, 14));
    float is = dp(ui, 46) * (1 + lift * 0.08f);
    ui_circle(ui, c.x + c.w / 2, c.y + c.h / 2, is / 2, with_alpha(t->accent, 235));
    ui_icon(ui, ICON_PLAY, (SDL_FRect){ c.x + c.w / 2 - is * 0.3f, c.y + c.h / 2 - is * 0.3f, is * 0.6f, is * 0.6f },
            t->on_accent);
    if (show_bar)
        ui_progress(ui, (SDL_FRect){ c.x + dp(ui, 12), c.y + c.h - dp(ui, 14), c.w - dp(ui, 24), dp(ui, 4) }, frac,
                    t->accent);
    if (st.focused) ui_focus_ring(ui, c, dp(ui, 14));
    float y = r.y + ch + dp(ui, 12);
    ui_text_fit(ui, FONT_BOLD, TS_BODY, r.x, y, r.w, t->text, line1);
    y += ui_line_h(ui, TS_BODY);
    char l2[300];
    SDL_strlcpy(l2, line2, sizeof l2);
    app_nsfw_why(a, root, file, st.hover || st.focused, card, l2, sizeof l2);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x, y, r.w, t->text_dim, l2);
    return st.activated;
}

static float wide_h(Ui *ui, float w) { return w * 9 / 16 + dp(ui, 12) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL); }

/* A horizontal row that scrolls on its own (mouse wheel sideways, or by
 * keyboard focus). Call row_begin, draw cards at row_x(), row_end. */
typedef struct Row {
    float x0, off, x, y, h;
} Row;

static void row_begin(App *a, Row *row, const char *id, float x, float y, float w, float h)
{
    Ui *ui = &a->ui;
    float m = dp(ui, 14);
    SDL_FRect view = { x - m, y - m, w + 2 * m, h + 2 * m };
    row->off = ui_scroll_begin(ui, ui_id(id), view, true);
    row->x0 = x;
    row->x = x - row->off;
    row->y = y;
    row->h = h;
}

static void row_end(App *a, Row *row)
{
    ui_scroll_end(&a->ui, row->x + row->off - row->x0 + dp(&a->ui, 14));
}

/* ------------------------------------------------------------------------ */
/* Sorting                                                                    */
/* ------------------------------------------------------------------------ */

static App *g_sort_app;
static int g_sort_mode;

static Sint64 entry_added(const Entry *e) { return e->show ? e->show->added : e->movie->added; }

static Sint64 entry_watched(const Entry *e)
{
    if (e->show) {
        ShowStat s;
        show_stat(g_sort_app, e->show, &s);
        return s.last_updated;
    }
    HistoryEntry h;
    return app_history(g_sort_app, e->movie->file, &h) ? h.updated : 0;
}

static int entry_cmp(const void *pa, const void *pb)
{
    const Entry *a = pa, *b = pb;
    switch (g_sort_mode) {
    case SORT_NAME: return natcmp(entry_title(a), entry_title(b));
    case SORT_RECENT_ADDED: {
        Sint64 x = entry_added(a), y = entry_added(b);
        return x == y ? natcmp(entry_title(a), entry_title(b)) : (x > y ? -1 : 1);
    }
    case SORT_RECENT_WATCHED: {
        Sint64 x = entry_watched(a), y = entry_watched(b);
        return x == y ? natcmp(entry_title(a), entry_title(b)) : (x > y ? -1 : 1);
    }
    default: {
        /* folder order: as the folders sit on disk */
        const FsNode *na = a->show ? a->show->dir : (a->movie->dir ? a->movie->dir : a->movie->file);
        const FsNode *nb = b->show ? b->show->dir : (b->movie->dir ? b->movie->dir : b->movie->file);
        return natcmp(na->name, nb->name);
    }
    }
}

/* Everything in a section (section < 0: everything), sorted. */
static int section_entries(App *a, int section, int mode, Entry *out, int cap)
{
    int n = 0;
    for (int i = 0; i < a->vlib.nshows && n < cap; i++)
        if (section < 0 || a->vlib.shows[i].section == section) out[n++] = (Entry){ &a->vlib.shows[i], NULL };
    for (int i = 0; i < a->vlib.nmovies && n < cap; i++)
        if (section < 0 || a->vlib.movies[i].section == section) out[n++] = (Entry){ NULL, &a->vlib.movies[i] };
    g_sort_app = a;
    g_sort_mode = mode;
    qsort(out, (size_t)n, sizeof *out, entry_cmp);
    return n;
}

#define MAX_ENTRIES 4096
static Entry g_entries[MAX_ENTRIES];

/* ------------------------------------------------------------------------ */
/* Home                                                                       */
/* ------------------------------------------------------------------------ */

typedef struct Resume {
    const FsNode *file;
    int root;
    const Show *show;
    int season;
    const Episode *ep;
    const Movie *movie;
    HistoryEntry h;
} Resume;

/* Files in progress that are still in the library, newest first. */
static int resume_list(App *a, Resume *out, int cap)
{
    HistoryRow rows[32];
    int n = history_in_progress(a->hist, rows, 32);
    int m = 0;
    for (int i = 0; i < n && m < cap; i++) {
        const FsNode *f = NULL;
        int root = -1;
        for (int r = 0; r < ROOT_COUNT && !f; r++) {
            if (r == ROOT_MUSIC || r == ROOT_BOOKS || !a->lib.root[r]) continue;
            FsNode *x = fs_find(a->lib.root[r], rows[i].path);
            if (x && !x->dir && x->kind == MK_VIDEO && x->size == rows[i].size) { f = x; root = r; }
        }
        if (!f) continue;
        Resume *rs = &out[m];
        SDL_zerop(rs);
        rs->file = f;
        rs->root = root;
        rs->h = rows[i].e;
        int s, e;
        rs->show = library_show_of(&a->vlib, f, &s, &e);
        if (rs->show) {
            rs->season = s;
            rs->ep = &rs->show->seasons[s].eps[e];
        } else {
            rs->movie = library_movie_of(&a->vlib, f);
        }
        m++;
    }
    return m;
}

static void resume_text(const Resume *r, char *title, size_t tcap, char *line, size_t lcap)
{
    if (r->show) {
        SDL_strlcpy(title, r->show->title, tcap);
        episode_line(r->show, r->season, r->ep, line, lcap);
    } else if (r->movie) {
        SDL_strlcpy(title, r->movie->title, tcap);
        if (r->movie->year) SDL_snprintf(line, lcap, "%d \xC2\xB7 Movie", r->movie->year);
        else SDL_strlcpy(line, "Movie", lcap);
    } else {
        SDL_strlcpy(title, r->file->name, tcap);
        line[0] = 0;
    }
}

static void hero(App *a, float x, float *py, float w, const Resume *r, const Entry *fresh)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float h = SDL_clamp(w * 0.32f, dp(ui, 240), dp(ui, 340));
    SDL_FRect box = { x, *py, w, h };
    char title[256], line[300], art[THC_PATH_MAX] = "";
    const char *kicker;
    if (r) {
        resume_text(r, title, sizeof title, line, sizeof line);
        art_path(a, r->root, r->show ? r->show->art : r->movie ? r->movie->art : NULL, art, sizeof art);
        kicker = "CONTINUE WATCHING";
    } else {
        SDL_strlcpy(title, entry_title(fresh), sizeof title);
        if (fresh->show)
            SDL_snprintf(line, sizeof line, "%d season%s \xC2\xB7 %d episodes", fresh->show->nseasons,
                         fresh->show->nseasons == 1 ? "" : "s", fresh->show->nepisodes);
        else if (fresh->movie->year)
            SDL_snprintf(line, sizeof line, "%d \xC2\xB7 Movie", fresh->movie->year);
        else
            SDL_strlcpy(line, "Movie", sizeof line);
        int root = fresh->show ? fresh->show->root : fresh->movie->root;
        art_path(a, root, fresh->show ? fresh->show->art : fresh->movie->art, art, sizeof art);
        kicker = "NEW IN YOUR LIBRARY";
    }
    ui_shadow(ui, box, dp(ui, 24), dp(ui, 40), t->shadow);
    Color c1, c2;
    art_colors(title, &c1, &c2);
    if (t->high_contrast) {
        ui_fill(ui, box, t->surface, dp(ui, 24));
        ui_stroke(ui, box, t->border, dp(ui, 24), dp(ui, 2));
    } else {
        ui_fill_grad_h(ui, box, color_mix(c2, t->bg2, 0.35f), c1, dp(ui, 24));
        SDL_Texture *tex = art[0] ? art_get(a, art) : NULL;
        if (tex) {
            float aw = h * 2 / 3;
            ui_image(ui, tex, (SDL_FRect){ box.x + box.w - aw - dp(ui, 28), box.y + dp(ui, 24), aw - dp(ui, 16),
                                           h - dp(ui, 48) }, dp(ui, 14), 255);
        }
        /* readable text side */
        ui_fill_grad_h(ui, box, with_alpha(t->bg2, 235), with_alpha(t->bg2, 0), dp(ui, 24));
    }
    float pad = dp(ui, 36);
    float tx = box.x + pad, ty = box.y + pad;
    float tw = box.w * 0.62f;
    ui_text(ui, FONT_BOLD, TS_SMALL, tx, ty, t->accent, kicker);
    ty += ui_line_h(ui, TS_SMALL) + dp(ui, 6);
    ty += ui_text_wrap(ui, FONT_BOLD, TS_HERO, tx, ty, tw, t->text, title, 2);
    ui_text_fit(ui, FONT_REGULAR, TS_LABEL, tx, ty, tw, t->text_dim, line);
    ty += ui_line_h(ui, TS_LABEL) + dp(ui, 14);
    if (r && r->h.duration > 0) {
        char left[32], msg[64];
        fmt_time(r->h.duration - r->h.position, left, sizeof left);
        SDL_snprintf(msg, sizeof msg, "%s left", left);
        float bw = SDL_min(tw, dp(ui, 260));
        ui_progress(ui, (SDL_FRect){ tx, ty + dp(ui, 6), bw, dp(ui, 5) }, (float)history_progress(&r->h), t->accent);
        ui_text(ui, FONT_REGULAR, TS_SMALL, tx + bw + dp(ui, 12), ty, t->text_dim, msg);
    }
    float by = box.y + box.h - pad - dp(ui, 48);
    float bw = dp(ui, 170);
    if (ui_button(ui, ui_id("hero-play"), (SDL_FRect){ tx, by, bw, dp(ui, 48) }, r ? "Resume" : "Play", ICON_PLAY, true)) {
        if (r) app_play_video(a, r->root, r->file);
        else if (fresh->movie) app_play_video(a, fresh->movie->root, fresh->movie->file);
        else {
            ShowStat ss;
            show_stat(a, fresh->show, &ss);
            if (ss.next) app_play_video(a, fresh->show->root, ss.next->file);
        }
    }
    const Show *sh = r ? r->show : fresh->show;
    if (sh && ui_button(ui, ui_id("hero-details"), (SDL_FRect){ tx + bw + dp(ui, 12), by, bw, dp(ui, 48) },
                        "Episodes", ICON_TV, false))
        open_show(a, sh, r ? r->season : -1);
    *py += h + dp(ui, 40);
}

static void heading_with_more(App *a, float x, float y, float w, const char *title, const char *note,
                              Uint32 more_id, bool *more)
{
    Ui *ui = &a->ui;
    draw_heading(a, x, y, title, note);
    if (more) {
        const char *lbl = "See all";
        float bw = ui_text_w(ui, FONT_BOLD, TS_BODY, lbl) + dp(ui, 32);
        float bh = dp(ui, 34);
        SDL_FRect b = { x + w - bw, y + (ui_line_h(ui, TS_TITLE) - bh) / 2, bw, bh };
        *more = ui_pill(ui, more_id, b, lbl, false);
    }
}

static void home(App *a, float x, float *py, float w)
{
    Ui *ui = &a->ui;
    Resume res[16];
    int nres = resume_list(a, res, 16);

    /* featured: the most recent thing in progress, else the newest addition */
    Entry fresh = { 0 };
    int nall = section_entries(a, -1, SORT_RECENT_ADDED, g_entries, MAX_ENTRIES);
    if (nall) fresh = g_entries[0];
    if (nres || nall) hero(a, x, py, w, nres ? &res[0] : NULL, &fresh);

    float cw = SDL_clamp(w / 4.4f, dp(ui, 240), dp(ui, 320));
    float gap = dp(ui, 20);
    if (nres) {
        draw_heading(a, x, *py, "Continue watching", NULL);
        *py += ui_line_h(ui, TS_TITLE) + dp(ui, 14);
        Row row;
        row_begin(a, &row, "row-continue", x, *py, w, wide_h(ui, cw));
        for (int i = 0; i < nres; i++) {
            char title[256], line[300], art[THC_PATH_MAX];
            resume_text(&res[i], title, sizeof title, line, sizeof line);
            art_path(a, res[i].root, res[i].show ? res[i].show->art : res[i].movie ? res[i].movie->art : NULL, art,
                     sizeof art);
            char left[32], l2[340];
            fmt_time(res[i].h.duration - res[i].h.position, left, sizeof left);
            SDL_snprintf(l2, sizeof l2, "%s \xC2\xB7 %s left", line, left);
            if (wide_card(a, (SDL_FRect){ row.x, row.y, cw, row.h }, ui_idp("cont", res[i].file), title,
                          art[0] ? art : NULL, title, l2, (float)history_progress(&res[i].h), true, res[i].file,
                          res[i].root))
                app_play_video(a, res[i].root, res[i].file);
            row.x += cw + gap;
        }
        row_end(a, &row);
        *py += row.h + dp(ui, 36);
    }

    /* Up next: the next episode of shows you have been watching */
    {
        const Show *shows[24];
        ShowStat stats[24];
        int n = 0;
        for (int i = 0; i < a->vlib.nshows && n < 24; i++) {
            ShowStat ss;
            show_stat(a, &a->vlib.shows[i], &ss);
            if (ss.watched > 0 && ss.watched < ss.total && ss.next && !ss.next_in_progress) {
                shows[n] = &a->vlib.shows[i];
                stats[n] = ss;
                n++;
            }
        }
        /* most recently watched first */
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && stats[j].last_updated > stats[j - 1].last_updated; j--) {
                ShowStat ts = stats[j]; stats[j] = stats[j - 1]; stats[j - 1] = ts;
                const Show *tp = shows[j]; shows[j] = shows[j - 1]; shows[j - 1] = tp;
            }
        if (n) {
            draw_heading(a, x, *py, "Up next", NULL);
            *py += ui_line_h(ui, TS_TITLE) + dp(ui, 14);
            Row row;
            row_begin(a, &row, "row-upnext", x, *py, w, wide_h(ui, cw));
            for (int i = 0; i < n; i++) {
                char line[300], art[THC_PATH_MAX];
                episode_line(shows[i], stats[i].next_season, stats[i].next, line, sizeof line);
                art_path(a, shows[i]->root, shows[i]->art, art, sizeof art);
                if (wide_card(a, (SDL_FRect){ row.x, row.y, cw, row.h }, ui_idp("upnext", shows[i]), shows[i]->title,
                              art[0] ? art : NULL, shows[i]->title, line, 0, false, stats[i].next->file,
                              shows[i]->root))
                    app_play_video(a, shows[i]->root, stats[i].next->file);
                row.x += cw + gap;
            }
            row_end(a, &row);
            *py += row.h + dp(ui, 36);
        }
    }

    /* one row per section */
    float pw = SDL_clamp(w / 7.2f, dp(ui, 140), dp(ui, 190));
    for (int s = 0; s < SECTION_COUNT; s++) {
        int n = section_entries(a, s, a->set.sort_mode, g_entries, MAX_ENTRIES);
        if (!n) continue;
        char note[32];
        SDL_snprintf(note, sizeof note, "%d", n);
        bool more = false;
        heading_with_more(a, x, *py, w, k_section_names[s], note, ui_idi("more", s), &more);
        if (more) nav_push_kind(a, &a->vnav, VK_SECTION, s, 0, "");
        *py += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
        Row row;
        char rid[32];
        SDL_snprintf(rid, sizeof rid, "row-sec-%d", s);
        row_begin(a, &row, rid, x, *py, w, poster_h(ui, pw));
        for (int i = 0; i < n; i++) {
            if (poster_card(a, (SDL_FRect){ row.x, row.y, pw, row.h }, &g_entries[i])) activate_entry(a, &g_entries[i]);
            row.x += pw + gap;
        }
        row_end(a, &row);
        *py += row.h + dp(ui, 32);
    }

    /* recently added, across sections */
    if (nall > 1) {
        nall = section_entries(a, -1, SORT_RECENT_ADDED, g_entries, MAX_ENTRIES);
        if (nall > 12) nall = 12;
        draw_heading(a, x, *py, "Recently added", NULL);
        *py += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
        Row row;
        row_begin(a, &row, "row-recent", x, *py, w, poster_h(ui, pw));
        for (int i = 0; i < nall; i++) {
            if (poster_card(a, (SDL_FRect){ row.x, row.y, pw, row.h }, &g_entries[i])) activate_entry(a, &g_entries[i]);
            row.x += pw + gap;
        }
        row_end(a, &row);
        *py += row.h + dp(ui, 32);
    }

    if (ui_button(ui, ui_id("browse-folders"), (SDL_FRect){ x, *py, dp(ui, 220), dp(ui, 46) }, "Browse folders",
                  ICON_FOLDER, false))
        nav_push_kind(a, &a->vnav, VK_FOLDER, 0, ROOT_VIDEO, "");
    *py += dp(ui, 46);
}

/* ------------------------------------------------------------------------ */
/* Section page                                                               */
/* ------------------------------------------------------------------------ */

static void section_page(App *a, int s, float x, float *py, float w)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float bs = dp(ui, 44);
    if (ui_icon_button(ui, ui_id("vback"), (SDL_FRect){ x - dp(ui, 6), *py, bs, bs }, ICON_BACK, "Back"))
        nav_pop(a, &a->vnav);
    ui_text(ui, FONT_BOLD, TS_H1, x + bs + dp(ui, 8), *py + (bs - ui_line_h(ui, TS_H1)) / 2, t->text,
            k_section_names[s]);
    *py += bs + dp(ui, 20);
    int mode = a->set.sort_mode;
    float sw = SDL_min(w, dp(ui, 720));
    if (ui_segmented(ui, ui_id("sort"), (SDL_FRect){ x, *py, sw, dp(ui, 56) }, "Sort", k_sort_names, SORT_COUNT, &mode)) {
        a->set.sort_mode = mode;
        app_save_settings(a);
    }
    *py += dp(ui, 56) + dp(ui, 24);
    int n = section_entries(a, s, a->set.sort_mode, g_entries, MAX_ENTRIES);
    float gap = dp(ui, 22);
    int cols = SDL_max(2, (int)((w + gap) / (dp(ui, 172) + gap)));
    float cw = (w - gap * (cols - 1)) / cols;
    float ch = poster_h(ui, cw);
    for (int i = 0; i < n; i++) {
        SDL_FRect r = { x + (i % cols) * (cw + gap), *py + (i / cols) * (ch + gap), cw, ch };
        if (poster_card(a, r, &g_entries[i])) activate_entry(a, &g_entries[i]);
    }
    *py += ((n + cols - 1) / cols) * (ch + gap);
}

/* ------------------------------------------------------------------------ */
/* Show page                                                                  */
/* ------------------------------------------------------------------------ */

static bool episode_row(App *a, SDL_FRect r, const Show *sh, const Season *se, const Episode *ep)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    Uint32 id = ui_idp("ep", ep->file);
    UiItemState st = ui_item(ui, id, r);
    mark_focus(a, ep->file, sh->root, st);
    if (st.context) {
        float cx, cy;
        ctx_pos(ui, r, &cx, &cy);
        queue_menu_item(a, cx, cy, sh->root, ep->file);
    }
    if (!ui_visible(ui, r)) return st.activated;
    float hv = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 16);
    ui_fill(ui, r, color_mix(t->surface, t->surface_hi, hv), dp(ui, 14));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 14), dp(ui, 1));
    if (st.focused) ui_focus_ring(ui, r, dp(ui, 14));

    HistoryEntry e;
    bool have = app_history(a, ep->file, &e);
    float pad = dp(ui, 14);
    float th = r.h - 2 * pad, tw = th * 16 / 9;
    SDL_FRect tile = { r.x + pad, r.y + pad, tw, th };
    Color c1, c2;
    art_colors(sh->title, &c1, &c2);
    Color fg = t->high_contrast ? t->text : rgb(0xFFFFFF);
    char tpath[THC_PATH_MAX];
    bool has_thumb = thumb_get(a, sh->root, ep->file, tpath, sizeof tpath) && art_get(a, tpath);
    if (has_thumb) {
        draw_art(a, tile, sh->title, tpath, dp(ui, 10));
        if (!t->high_contrast) ui_fill(ui, tile, rgba(0, hv > 0.5f ? 110 : 0), dp(ui, 10));
    } else if (t->high_contrast) {
        ui_stroke(ui, tile, t->border, dp(ui, 10), dp(ui, 2));
    } else {
        ui_fill_grad_h(ui, tile, color_mix(c1, c2, 0.2f), c2, dp(ui, 10));
    }
    if (hv > 0.5f) {
        float is = th * 0.46f;
        ui_icon(ui, ICON_PLAY, (SDL_FRect){ tile.x + (tw - is) / 2, tile.y + (th - is) / 2, is, is }, fg);
    } else if (!has_thumb) {
        char num[16];
        SDL_snprintf(num, sizeof num, "%d", ep->number);
        ui_text_center(ui, FONT_BOLD, TS_H2, tile, fg, num, true);
    }
    if (have && (e.watched || e.position > 0) && !t->high_contrast)
        ui_progress(ui, (SDL_FRect){ tile.x + dp(ui, 8), tile.y + th - dp(ui, 10), tw - dp(ui, 16), dp(ui, 3) },
                    e.watched ? 1.0f : (float)history_progress(&e), e.watched ? t->watched : t->accent);

    float x = tile.x + tw + dp(ui, 18);
    float right = dp(ui, 130);
    float w = r.x + r.w - pad - right - x;
    float y = r.y + pad;
    char line[300];
    if (ep->title[0]) SDL_snprintf(line, sizeof line, "%s", ep->title);
    else SDL_snprintf(line, sizeof line, "%s", ep->label);
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, x, y, w, t->text, line);
    y += ui_line_h(ui, TS_LABEL) + dp(ui, 2);
    char meta[128];
    int off = SDL_snprintf(meta, sizeof meta, "%s", ep->title[0] ? ep->label : se->name);
    if (have && e.duration > 0) {
        char d[32], p[32];
        fmt_time(e.duration, d, sizeof d);
        off += SDL_snprintf(meta + off, sizeof meta - off, " \xC2\xB7 %s", d);
        if (!e.watched && e.position > 0) {
            fmt_time(e.duration - e.position, p, sizeof p);
            SDL_snprintf(meta + off, sizeof meta - off, " \xC2\xB7 %s left", p);
        }
    }
    app_nsfw_why(a, sh->root, ep->file, st.hover || st.focused, r, meta, sizeof meta);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, x, y, w, t->text_dim, meta);
    float barw = SDL_min(r.x + r.w - pad - x, dp(ui, 520));
    ui_watch_status(ui, (SDL_FRect){ x, r.y + r.h - pad - dp(ui, 18), barw, dp(ui, 18) },
                    have ? (float)history_progress(&e) : 0, have && e.watched, t->accent);
    if (have && !e.watched && e.position > 5) {
        char rs[48], p[32];
        fmt_time(e.position, p, sizeof p);
        SDL_snprintf(rs, sizeof rs, "Resume %s", p);
        float rw = ui_text_w(ui, FONT_BOLD, TS_SMALL, rs);
        ui_text(ui, FONT_BOLD, TS_SMALL, r.x + r.w - pad - rw, r.y + pad, t->accent, rs);
    }
    return st.activated;
}

static void set_season_watched(App *a, const Season *se, bool watched)
{
    for (int i = 0; i < se->neps; i++) {
        char rel[THC_PATH_MAX];
        if (!app_history_ok_node(a, KIND_VIDEO, se->eps[i].file)) continue;       /* NSFW: nothing is recorded */
        fs_relpath(se->eps[i].file, rel, sizeof rel);
        history_set_watched(a->hist, rel, se->eps[i].file->size, watched);
    }
    app_history_changed();
}

static void show_page(App *a, const Show *sh, float x, float *py, float w)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    PortalNav *nav = &a->vnav;
    float bs = dp(ui, 44);
    if (ui_icon_button(ui, ui_id("vback"), (SDL_FRect){ x - dp(ui, 6), *py, bs, bs }, ICON_BACK, "Back"))
        nav_pop(a, nav);
    *py += bs + dp(ui, 8);

    ShowStat ss;
    show_stat(a, sh, &ss);
    int *season = &nav->arg[nav->depth];
    if (*season < 0 || *season >= sh->nseasons) *season = ss.next ? ss.next_season : 0;

    /* header: poster and details */
    bool side = w > dp(ui, 680);
    float pw = side ? SDL_min(dp(ui, 220), w * 0.24f) : dp(ui, 150);
    float ph = pw * 1.5f;
    char art[THC_PATH_MAX];
    art_path(a, sh->root, sh->art, art, sizeof art);
    if (!art[0] && sh->nseasons && sh->seasons[0].neps)
        thumb_get(a, sh->root, sh->seasons[0].eps[0].file, art, sizeof art);   /* no poster: a frame */
    SDL_FRect pr = { x, *py, pw, ph };
    ui_shadow(ui, pr, dp(ui, 16), dp(ui, 36), t->shadow);
    draw_cover(a, pr, sh->title, art[0] ? art : NULL, dp(ui, 16), ICON_TV);
    float tx = x + pw + dp(ui, 32), tw = w - pw - dp(ui, 32);
    float ty = *py + dp(ui, 4);
    ui_text(ui, FONT_BOLD, TS_SMALL, tx, ty, t->accent, k_section_names[sh->section]);
    ty += ui_line_h(ui, TS_SMALL);
    ty += ui_text_wrap(ui, FONT_BOLD, TS_HERO, tx, ty, tw, t->text, sh->title, 2);
    char meta[160];
    int off = 0;
    if (sh->year) off += SDL_snprintf(meta + off, sizeof meta - off, "%d \xC2\xB7 ", sh->year);
    SDL_snprintf(meta + off, sizeof meta - off, "%d season%s \xC2\xB7 %d episodes \xC2\xB7 %d watched", sh->nseasons,
                 sh->nseasons == 1 ? "" : "s", ss.total, ss.watched);
    ui_text_fit(ui, FONT_REGULAR, TS_LABEL, tx, ty, tw, t->text_dim, meta);
    ty += ui_line_h(ui, TS_LABEL) + dp(ui, 10);
    ui_watch_status(ui, (SDL_FRect){ tx, ty, SDL_min(tw, dp(ui, 360)), dp(ui, 18) },
                    ss.total ? (float)ss.watched / ss.total : 0, ss.total && ss.watched == ss.total, t->accent);
    ty += dp(ui, 34);

    float bx = tx;
    if (ss.next) {
        char lbl[64];
        const Season *ns = &sh->seasons[ss.next_season];
        if (ns->number > 0) SDL_snprintf(lbl, sizeof lbl, "%s S%d E%d", ss.next_in_progress ? "Resume" : "Play",
                                         ns->number, ss.next->number);
        else SDL_snprintf(lbl, sizeof lbl, "%s %s", ss.next_in_progress ? "Resume" : "Play", ss.next->label);
        float bw = ui_text_w(ui, FONT_BOLD, TS_LABEL, lbl) + dp(ui, 70);
        if (ui_button(ui, ui_id("show-play"), (SDL_FRect){ bx, ty, bw, dp(ui, 48) }, lbl, ICON_PLAY, true))
            app_play_video(a, sh->root, ss.next->file);
        bx += bw + dp(ui, 12);
    }
    {
        float aw = dp(ui, 190);
        if (ui_button(ui, ui_id("show-add"), (SDL_FRect){ bx, ty, aw, dp(ui, 48) }, "Add to queue", ICON_QUEUE, false))
            queue_add_show(a, sh->key, -1, false);
        bx += aw + dp(ui, 12);
    }
    /* Move to another section: presentation only, folders stay put. */
    char sec[64];
    SDL_snprintf(sec, sizeof sec, "In %s", k_section_names[sh->section]);
    float sbw = ui_text_w(ui, FONT_BOLD, TS_LABEL, sec) + dp(ui, 64);
    if (ui_button(ui, ui_id("show-section"), (SDL_FRect){ bx, ty, sbw, dp(ui, 48) }, sec, ICON_SORT, false)) {
        char key[THC_PATH_MAX];
        SDL_strlcpy(key, sh->key, sizeof key);
        int next = (sh->section + 1) % SECTION_COUNT;
        if (next == SECTION_MOVIES) next = (next + 1) % SECTION_COUNT;
        settings_set_override(&a->set, key, next);
        app_save_settings(a);
        app_rebuild_library(a);
        app_toast(a, "Now shown in %s. Your folders are unchanged.", k_section_names[next]);
        *py += ph;
        return; /* the show pointer moved; draw again next frame */
    }
    *py += SDL_max(ph, ty + dp(ui, 48) - *py) + dp(ui, 32);

    /* season tabs */
    if (sh->nseasons > 1) {
        Row row;
        float ph2 = dp(ui, 40);
        row_begin(a, &row, "season-tabs", x, *py, w, ph2);
        for (int i = 0; i < sh->nseasons; i++) {
            float pwid = ui_text_w(ui, FONT_BOLD, TS_BODY, sh->seasons[i].name) + dp(ui, 36);
            if (ui_pill(ui, ui_idi("season", i), (SDL_FRect){ row.x, row.y, pwid, ph2 }, sh->seasons[i].name, i == *season))
                *season = i;
            row.x += pwid + dp(ui, 10);
        }
        row_end(a, &row);
        *py += ph2 + dp(ui, 24);
    }
    const Season *se = &sh->seasons[*season];
    int sw = 0;
    for (int i = 0; i < se->neps; i++) {
        HistoryEntry h;
        if (app_history(a, se->eps[i].file, &h) && h.watched) sw++;
    }
    char note[64];
    SDL_snprintf(note, sizeof note, "%d episode%s \xC2\xB7 %d watched", se->neps, se->neps == 1 ? "" : "s", sw);
    draw_heading(a, x, *py, se->name, note);
    {
        const char *al = "Add season";
        float aw2 = ui_text_w(ui, FONT_BOLD, TS_BODY, al) + dp(ui, 36);
        float mw0 = ui_text_w(ui, FONT_BOLD, TS_BODY, sw == se->neps ? "Mark unwatched" : "Mark season watched") + dp(ui, 36);
        SDL_FRect ar = { x + w - mw0 - dp(ui, 10) - aw2, *py - dp(ui, 4), aw2, dp(ui, 36) };
        if (ui_pill(ui, ui_id("season-add"), ar, al, false)) queue_add_show(a, sh->key, *season, false);
    }
    const char *lbl = sw == se->neps ? "Mark unwatched" : "Mark season watched";
    float mw = ui_text_w(ui, FONT_BOLD, TS_BODY, lbl) + dp(ui, 36);
    if (ui_pill(ui, ui_id("season-mark"), (SDL_FRect){ x + w - mw, *py - dp(ui, 4), mw, dp(ui, 36) }, lbl, false))
        set_season_watched(a, se, sw != se->neps);
    *py += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
    float rh = dp(ui, 96);
    for (int i = 0; i < se->neps; i++) {
        if (episode_row(a, (SDL_FRect){ x, *py, w, rh }, sh, se, &se->eps[i]))
            app_play_video(a, sh->root, se->eps[i].file);
        *py += rh + dp(ui, 10);
    }
}

/* ------------------------------------------------------------------------ */
/* Folder browser: the folders exactly as they are on disk                    */
/* ------------------------------------------------------------------------ */

static bool folder_card(App *a, SDL_FRect r, const FsNode *n, int root)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    Uint32 id = ui_idp("vfolder", n);
    SDL_FRect box = { r.x, r.y, r.w, dp(ui, 76) };
    UiItemState st = ui_item(ui, id, box);
    if (!ui_visible(ui, box)) return st.activated;
    float hv = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 16);
    ui_fill(ui, box, color_mix(t->surface, t->surface_hi, hv), dp(ui, 14));
    if (t->high_contrast) ui_stroke(ui, box, t->border, dp(ui, 14), dp(ui, 1));
    if (st.focused) ui_focus_ring(ui, box, dp(ui, 14));
    float is = dp(ui, 40);
    ui_fill(ui, (SDL_FRect){ box.x + dp(ui, 16), box.y + (box.h - is) / 2, is, is }, with_alpha(t->accent, 40), dp(ui, 10));
    ui_icon(ui, ICON_FOLDER, (SDL_FRect){ box.x + dp(ui, 24), box.y + (box.h - is) / 2 + dp(ui, 8), is - dp(ui, 16),
                                          is - dp(ui, 16) }, t->accent);
    float tx = box.x + dp(ui, 72);
    ui_text_fit(ui, FONT_BOLD, TS_BODY, tx, box.y + dp(ui, 16), box.w - dp(ui, 88), t->text, n->name);
    char meta[96];
    SDL_snprintf(meta, sizeof meta, "%d video%s", fs_count(n, MK_VIDEO), fs_count(n, MK_VIDEO) == 1 ? "" : "s");
    app_nsfw_why(a, root, n, false, box, meta, sizeof meta);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, tx, box.y + dp(ui, 16) + ui_line_h(ui, TS_BODY), box.w - dp(ui, 88),
                t->text_dim, meta);
    app_nsfw_why(a, root, n, st.hover || st.focused, box, NULL, 0);
    return st.activated;
}

static bool file_row(App *a, SDL_FRect r, const FsNode *f, int root)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    Uint32 id = ui_idp("vfile", f);
    UiItemState st = ui_item(ui, id, r);
    mark_focus(a, f, root, st);
    if (st.context) {
        float cx, cy;
        ctx_pos(ui, r, &cx, &cy);
        queue_menu_item(a, cx, cy, root, f);
    }
    if (!ui_visible(ui, r)) return st.activated;
    float hv = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 16);
    ui_fill(ui, r, color_mix(t->surface, t->surface_hi, hv), dp(ui, 12));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    if (st.focused) ui_focus_ring(ui, r, dp(ui, 12));
    HistoryEntry e;
    bool have = app_history(a, f, &e);
    float is = dp(ui, 20);
    ui_icon(ui, ICON_PLAY, (SDL_FRect){ r.x + dp(ui, 18), r.y + dp(ui, 14), is, is }, hv > 0.5f ? t->accent : t->text_dim);
    float tx = r.x + dp(ui, 52);
    char why[96] = "";
    app_nsfw_why(a, root, f, st.hover || st.focused, r, why, sizeof why);
    float whw = why[0] ? SDL_min(ui_text_w(ui, FONT_REGULAR, TS_SMALL, why), r.w * 0.4f) + dp(ui, 12) : 0;
    ui_text_fit(ui, FONT_BOLD, TS_BODY, tx, r.y + dp(ui, 12), r.w - dp(ui, 70) - whw, t->text, f->name);
    if (why[0]) ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + r.w - dp(ui, 18) - whw + dp(ui, 12), r.y + dp(ui, 14), whw - dp(ui, 12), t->text_dim, why);
    ui_watch_status(ui, (SDL_FRect){ tx, r.y + r.h - dp(ui, 26), SDL_min(r.w - dp(ui, 70), dp(ui, 420)), dp(ui, 16) },
                    have ? (float)history_progress(&e) : 0, have && e.watched, t->accent);
    return st.activated;
}

static void folder_page(App *a, float x, float *py, float w)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    PortalNav *nav = &a->vnav;
    int root = nav->root[nav->depth];
    FsNode *rn = a->lib.root[root];
    FsNode *dir = rn ? fs_find(rn, nav->rel[nav->depth]) : NULL;
    float bs = dp(ui, 44);
    if (ui_icon_button(ui, ui_id("vback"), (SDL_FRect){ x - dp(ui, 6), *py, bs, bs }, ICON_BACK, "Back")) nav_pop(a, nav);
    if (!dir) return;
    const char *name = dir == rn ? "Folders" : dir->name;
    ui_text_fit(ui, FONT_BOLD, TS_H1, x + bs + dp(ui, 8), *py + (bs - ui_line_h(ui, TS_H1)) / 2, w - bs, t->text, name);
    *py += bs + dp(ui, 8);
    char rel[THC_PATH_MAX];
    fs_relpath(dir, rel, sizeof rel);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, x, *py, w, t->text_dim, dir == rn ? a->lib.path[root] : rel);
    *py += ui_line_h(ui, TS_SMALL) + dp(ui, 20);

    float gap = dp(ui, 14);
    int cols = SDL_max(1, (int)((w + gap) / (dp(ui, 300) + gap)));
    float cw = (w - gap * (cols - 1)) / cols;
    int c = 0;
    for (int i = 0; i < dir->nkids; i++) {
        const FsNode *k = dir->kids[i];
        if (!k->dir || fs_count(k, MK_VIDEO) == 0) continue;
        if (folder_card(a, (SDL_FRect){ x + c * (cw + gap), *py, cw, dp(ui, 76) }, k, root)) {
            char r2[THC_PATH_MAX];
            nav_push_kind(a, nav, VK_FOLDER, 0, root, fs_relpath(k, r2, sizeof r2));
        }
        if (++c == cols) { c = 0; *py += dp(ui, 76) + gap; }
    }
    if (c) *py += dp(ui, 76) + gap;
    *py += dp(ui, 8);
    for (int i = 0; i < dir->nkids; i++) {
        const FsNode *k = dir->kids[i];
        if (k->dir || k->kind != MK_VIDEO) continue;
        if (file_row(a, (SDL_FRect){ x, *py, w, dp(ui, 72) }, k, root)) app_play_video(a, root, k);
        *py += dp(ui, 72) + dp(ui, 8);
    }
}

/* ------------------------------------------------------------------------ */

void portal_video(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    PortalNav *nav = &a->vnav;
    bool any_root = a->lib.root[ROOT_VIDEO] != NULL;
    for (int i = 0; i < SECTION_COUNT; i++) any_root |= a->lib.root[ROOT_SECTION0 + i] != NULL;
    bool any_dir = a->set.video_dir[0] != 0;
    for (int i = 0; i < SECTION_COUNT; i++) any_dir |= a->set.section_dir[i][0] != 0;

    if (!any_dir) {
        bool pick = false;
        draw_empty(a, area, ICON_FILM, "Add your videos",
                   "Pick the folder with your shows and movies. THC reads it as Show, Season, Episode.",
                   "Choose video folder", &pick);
        if (pick) app_pick_folder(a, 1, NULL);
        return;
    }
    if (!any_root) {
        bool pick = false;
        draw_empty(a, area, ICON_FILM, a->scanning ? "Reading your library" : "Folder not found",
                   a->scanning ? "This only takes a moment." : a->set.video_dir,
                   a->scanning ? NULL : "Choose another folder", &pick);
        if (pick) app_pick_folder(a, 1, a->set.video_dir);
        return;
    }

    float pad = SDL_max(dp(ui, 32), (area.w - dp(ui, 1500)) / 2);
    float x = area.x + pad, w = area.w - 2 * pad;
    float off = ui_scroll_begin(ui, ui_idi("page", nav->depth), area, false);
    float y = area.y + dp(ui, 28) - off;

    int kind = nav->depth ? nav->kind[nav->depth] : VK_HOME;
    switch (kind) {
    case VK_SECTION:
        section_page(a, nav->arg[nav->depth], x, &y, w);
        break;
    case VK_SHOW: {
        const Show *sh = show_by_key(a, nav->rel[nav->depth]);
        if (sh) show_page(a, sh, x, &y, w);
        else nav_pop(a, nav);
        break;
    }
    case VK_FOLDER:
        folder_page(a, x, &y, w);
        break;
    default:
        if (a->vlib.nshows + a->vlib.nmovies == 0) {
            ui_text(ui, FONT_BOLD, TS_H1, x, y, t->text, "Video");
            y += ui_line_h(ui, TS_H1) + dp(ui, 8);
            ui_text(ui, FONT_REGULAR, TS_LABEL, x, y, t->text_dim, "No videos found yet. Add folders in Settings.");
            y += ui_line_h(ui, TS_LABEL);
        } else {
            home(a, x, &y, w);
        }
        break;
    }
    y += dp(ui, 48);
    ui_scroll_end(ui, y + off - area.y);
}
