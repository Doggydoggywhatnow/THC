/* portal_video.c: the video portal ("Cinema" look). */
#include "app.h"

#include <string.h>

typedef struct WatchStat {
    int total, watched;
    float progress;      /* for single files */
    bool any_progress;
} WatchStat;

static void watch_stat(App *a, const FsNode *n, WatchStat *ws)
{
    if (!n->dir) {
        if (n->kind != MK_VIDEO) return;
        HistoryEntry e;
        ws->total++;
        if (app_history(a, n, &e)) {
            if (e.watched) ws->watched++;
            ws->progress = (float)history_progress(&e);
            if (e.position > 0) ws->any_progress = true;
        }
        return;
    }
    for (int i = 0; i < n->nkids; i++) watch_stat(a, n->kids[i], ws);
}

static void strip_ext(const char *name, char *out, size_t cap)
{
    SDL_strlcpy(out, name, cap);
    char *dot = SDL_strrchr(out, '.');
    if (dot && dot != out) *dot = 0;
}

/* A poster card for a folder (show, movie folder, collection). */
static bool folder_card(App *a, SDL_FRect r, const FsNode *n, int root)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    Uint32 id = ui_idp("vfolder", n);
    float ph = r.w * 1.5f;
    SDL_FRect poster = { r.x, r.y, r.w, ph };
    UiItemState st = ui_item(ui, id, poster);
    float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
    SDL_FRect p = { poster.x - lift * dp(ui, 4), poster.y - lift * dp(ui, 6), poster.w + lift * dp(ui, 8),
                    poster.h + lift * dp(ui, 8) };
    if (ui_visible(ui, (SDL_FRect){ r.x, r.y, r.w, r.h })) {
        ui_shadow(ui, p, dp(ui, 14), dp(ui, 18 + lift * 16), t->shadow);
        char art[THC_PATH_MAX] = "";
        const FsNode *an = fs_find_art(n);
        if (an) app_full_path(a, root, an, art, sizeof art);
        draw_cover(a, p, n->name, art[0] ? art : NULL, dp(ui, 14), fs_count(n, MK_VIDEO) > 1 ? ICON_TV : ICON_FILM);
        if (st.focused) ui_focus_ring(ui, p, dp(ui, 14));

        WatchStat ws = { 0 };
        watch_stat(a, n, &ws);
        float y = r.y + ph + dp(ui, 12);
        ui_text_fit(ui, FONT_BOLD, TS_BODY, r.x, y, r.w, t->text, n->name);
        y += ui_line_h(ui, TS_BODY);
        char meta[64];
        if (ws.total == 1) SDL_snprintf(meta, sizeof meta, "1 video");
        else SDL_snprintf(meta, sizeof meta, "%d videos", ws.total);
        if (ws.watched && ws.watched < ws.total)
            SDL_snprintf(meta + strlen(meta), sizeof meta - strlen(meta), "  \xC2\xB7  %d watched", ws.watched);
        ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x, y, r.w, t->text_dim, meta);
        y += ui_line_h(ui, TS_SMALL) + dp(ui, 6);
        float frac = ws.total ? (float)ws.watched / ws.total : 0;
        ui_watch_status(ui, (SDL_FRect){ r.x, y, r.w, dp(ui, 16) }, frac,
                        ws.total > 0 && ws.watched == ws.total, t->accent);
    }
    return st.activated;
}

/* A row for one video file: playback bar and checkmark under the title. */
static bool file_row(App *a, SDL_FRect r, const FsNode *f, int index)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    Uint32 id = ui_idp("vfile", f);
    UiItemState st = ui_item(ui, id, r);
    if (!ui_visible(ui, r)) return st.activated;
    float hv = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 16);
    ui_fill(ui, r, color_mix(t->surface, t->surface_hi, hv), dp(ui, 14));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 14), dp(ui, 1));
    if (st.focused) ui_focus_ring(ui, r, dp(ui, 14));

    HistoryEntry e;
    bool have = app_history(a, f, &e);
    float pad = dp(ui, 14);
    /* thumbnail tile with the number and a play glyph on hover */
    float th = r.h - 2 * pad, tw = th * 16 / 9;
    SDL_FRect tile = { r.x + pad, r.y + pad, tw, th };
    char title[256];
    strip_ext(f->name, title, sizeof title);
    Color c1, c2;
    art_colors(f->parent ? f->parent->name : title, &c1, &c2);
    if (t->high_contrast) ui_stroke(ui, tile, t->border, dp(ui, 10), dp(ui, 2));
    else ui_fill_grad_h(ui, tile, with_alpha(c1, 200), with_alpha(c2, 220), dp(ui, 10));
    char num[16];
    SDL_snprintf(num, sizeof num, "%d", index + 1);
    if (hv > 0.5f) {
        float is = th * 0.5f;
        ui_icon(ui, ICON_PLAY, (SDL_FRect){ tile.x + (tw - is) / 2, tile.y + (th - is) / 2, is, is },
                t->high_contrast ? t->text : rgb(0xFFFFFF));
    } else {
        ui_text_center(ui, FONT_BOLD, TS_H2, tile, t->high_contrast ? t->text : rgb(0xFFFFFF), num, true);
    }

    float x = tile.x + tw + dp(ui, 18);
    float right = dp(ui, 120);
    float w = r.x + r.w - pad - right - x;
    float y = r.y + pad;
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, x, y, w, t->text, title);
    y += ui_line_h(ui, TS_LABEL) + dp(ui, 2);
    char meta[96] = "";
    if (have && e.duration > 0) {
        char d[32], p[32];
        fmt_time(e.duration, d, sizeof d);
        if (e.watched) SDL_snprintf(meta, sizeof meta, "%s  \xC2\xB7  Watched", d);
        else if (e.position > 0) {
            fmt_time(e.duration - e.position, p, sizeof p);
            SDL_snprintf(meta, sizeof meta, "%s  \xC2\xB7  %s left", d, p);
        } else SDL_snprintf(meta, sizeof meta, "%s", d);
    } else {
        SDL_snprintf(meta, sizeof meta, "Not started");
    }
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, x, y, w, t->text_dim, meta);
    float barw = r.x + r.w - pad - x;
    ui_watch_status(ui, (SDL_FRect){ x, r.y + r.h - pad - dp(ui, 18), SDL_min(barw, dp(ui, 520)), dp(ui, 18) },
                    have ? (float)history_progress(&e) : 0, have && e.watched, t->accent);
    /* resume hint on the right */
    if (have && !e.watched && e.position > 5) {
        char rs[48], p[32];
        fmt_time(e.position, p, sizeof p);
        SDL_snprintf(rs, sizeof rs, "Resume %s", p);
        float rw = ui_text_w(ui, FONT_BOLD, TS_SMALL, rs);
        ui_text(ui, FONT_BOLD, TS_SMALL, r.x + r.w - pad - rw, r.y + pad, t->accent, rs);
    }
    return st.activated;
}

/* Grid of folders followed by a list of files. Returns the height used. */
static float folder_contents(App *a, const FsNode *dir, int root, float x, float y, float w, PortalNav *nav)
{
    Ui *ui = &a->ui;
    float y0 = y;
    int nfolders = 0, nfiles = 0;
    for (int i = 0; i < dir->nkids; i++) {
        const FsNode *k = dir->kids[i];
        if (k->dir && fs_count(k, MK_VIDEO) > 0) nfolders++;
        else if (!k->dir && k->kind == MK_VIDEO) nfiles++;
    }
    if (nfolders) {
        float gap = dp(ui, 22);
        float target = dp(ui, 172);
        int cols = SDL_max(2, (int)((w + gap) / (target + gap)));
        float cw = (w - gap * (cols - 1)) / cols;
        float ch = cw * 1.5f + dp(ui, 12) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL) + dp(ui, 30);
        int c = 0;
        for (int i = 0; i < dir->nkids; i++) {
            const FsNode *k = dir->kids[i];
            if (!k->dir || fs_count(k, MK_VIDEO) == 0) continue;
            SDL_FRect r = { x + c * (cw + gap), y, cw, ch };
            if (folder_card(a, r, k, root)) {
                char rel[THC_PATH_MAX];
                nav_push(a, nav, root, fs_relpath(k, rel, sizeof rel));
            }
            if (++c == cols) { c = 0; y += ch + gap; }
        }
        if (c) y += ch + gap;
    }
    if (nfiles) {
        if (nfolders) y += dp(ui, 12);
        float rh = dp(ui, 96);
        int idx = 0;
        for (int i = 0; i < dir->nkids; i++) {
            const FsNode *k = dir->kids[i];
            if (k->dir || k->kind != MK_VIDEO) continue;
            if (file_row(a, (SDL_FRect){ x, y, w, rh }, k, idx++)) app_play_video(a, root, k);
            y += rh + dp(ui, 10);
        }
    }
    return y - y0;
}

static void continue_row(App *a, float x, float *py, float w)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    HistoryRow rows[16];
    int n = history_in_progress(a->hist, rows, 16);
    const FsNode *found[16];
    int roots[16];
    int m = 0;
    for (int i = 0; i < n; i++) {
        FsNode *root = a->lib.root[ROOT_VIDEO];
        FsNode *f = root ? fs_find(root, rows[i].path) : NULL;
        int r = ROOT_VIDEO;
        for (int s = 0; !f && s < SECTION_COUNT; s++) {
            FsNode *sr = a->lib.root[ROOT_SECTION0 + s];
            if (sr) { f = fs_find(sr, rows[i].path); r = ROOT_SECTION0 + s; }
        }
        if (f && !f->dir && f->kind == MK_VIDEO && f->size == rows[i].size) {
            found[m] = f;
            roots[m] = r;
            m++;
        }
    }
    if (!m) return;
    float y = *py;
    draw_heading(a, x, y, "Continue watching", NULL);
    y += ui_line_h(ui, TS_TITLE) + dp(ui, 14);
    float cw = dp(ui, 300), ch = cw * 9 / 16, gap = dp(ui, 20);
    float rowh = ch + dp(ui, 16) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL) + dp(ui, 10);
    SDL_FRect view = { x - dp(ui, 12), y - dp(ui, 12), w + dp(ui, 24), rowh + dp(ui, 24) };
    float off = ui_scroll_begin(ui, ui_id("continue-row"), view, true);
    float cx = x - off;
    for (int i = 0; i < m; i++) {
        const FsNode *f = found[i];
        SDL_FRect card = { cx, y, cw, ch };
        Uint32 id = ui_idp("cont", f);
        UiItemState st = ui_item(ui, id, card);
        float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
        SDL_FRect c = { card.x - lift * dp(ui, 4), card.y - lift * dp(ui, 4), card.w + lift * dp(ui, 8),
                        card.h + lift * dp(ui, 8) };
        ui_shadow(ui, c, dp(ui, 14), dp(ui, 20), t->shadow);
        const char *show = f->parent && f->parent->parent ? f->parent->name : f->name;
        if (f->parent && f->parent->parent && f->parent->parent->parent) show = f->parent->parent->name;
        draw_cover(a, c, show, NULL, dp(ui, 14), ICON_PLAY);
        if (!t->high_contrast) {
            ui_fill_grad(ui, (SDL_FRect){ c.x, c.y + c.h * 0.45f, c.w, c.h * 0.55f }, rgba(0, 0), rgba(0, 150), dp(ui, 14));
        }
        float is = dp(ui, 44);
        ui_circle(ui, c.x + c.w / 2, c.y + c.h / 2, is / 2 * (1 + lift * 0.1f), with_alpha(t->accent, 230));
        ui_icon(ui, ICON_PLAY, (SDL_FRect){ c.x + c.w / 2 - is * 0.32f, c.y + c.h / 2 - is * 0.32f, is * 0.64f, is * 0.64f }, t->on_accent);
        if (st.focused) ui_focus_ring(ui, c, dp(ui, 14));
        HistoryEntry e;
        app_history(a, f, &e);
        float ty = card.y + ch + dp(ui, 12);
        char title[256];
        strip_ext(f->name, title, sizeof title);
        ui_text_fit(ui, FONT_BOLD, TS_BODY, card.x, ty, cw, t->text, title);
        ty += ui_line_h(ui, TS_BODY);
        char left[32], meta[64];
        fmt_time(e.duration - e.position, left, sizeof left);
        SDL_snprintf(meta, sizeof meta, "%s left", left);
        ui_text_fit(ui, FONT_REGULAR, TS_SMALL, card.x, ty, cw, t->text_dim, meta);
        ty += ui_line_h(ui, TS_SMALL) + dp(ui, 6);
        ui_watch_status(ui, (SDL_FRect){ card.x, ty, cw, dp(ui, 12) }, (float)history_progress(&e), false, t->accent);
        if (st.activated) app_play_video(a, roots[i], f);
        cx += cw + gap;
    }
    ui_scroll_end(ui, cx + off - x + dp(ui, 24));
    *py = y + rowh + dp(ui, 28);
}

void portal_video(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    PortalNav *nav = &a->vnav;
    FsNode *root = a->lib.root[ROOT_VIDEO];

    if (!a->set.video_dir[0]) {
        bool pick = false;
        draw_empty(a, area, ICON_FILM, "Add your videos",
                   "Pick the folder with your shows and movies. THC reads it as Show, Season, Episode.",
                   "Choose video folder", &pick);
        if (pick) app_pick_folder(a, 1, NULL);
        return;
    }
    if (!root) {
        bool pick = false;
        draw_empty(a, area, ICON_FILM, a->scanning ? "Reading your library" : "Folder not found",
                   a->scanning ? "This only takes a moment." : a->set.video_dir,
                   a->scanning ? NULL : "Choose another folder", &pick);
        if (pick) app_pick_folder(a, 1, a->set.video_dir);
        return;
    }

    FsNode *dir = nav->depth ? nav_node(a, nav) : root;
    if (!dir) {
        nav_reset(nav);
        dir = root;
    }
    int droot = nav->depth ? nav->root[nav->depth] : ROOT_VIDEO;

    float pad = SDL_max(dp(ui, 28), (area.w - dp(ui, 1400)) / 2);
    float x = area.x + pad, w = area.w - 2 * pad;
    float off = ui_scroll_begin(ui, ui_idi("page", nav->depth), area, false);
    float y = area.y + dp(ui, 28) - off;

    if (nav->depth == 0) {
        ui_text(ui, FONT_BOLD, TS_H1, x, y, t->text, "Video");
        y += ui_line_h(ui, TS_H1);
        ui_text_fit(ui, FONT_REGULAR, TS_BODY, x, y, w, t->text_dim, a->set.video_dir);
        y += ui_line_h(ui, TS_BODY) + dp(ui, 28);
        continue_row(a, x, &y, w);
        draw_heading(a, x, y, "Library", NULL);
        y += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
    } else {
        float bs = dp(ui, 44);
        if (ui_icon_button(ui, ui_id("vback"), (SDL_FRect){ x - dp(ui, 6), y, bs, bs }, ICON_BACK, "Back"))
            nav_pop(a, nav);
        ui_text_fit(ui, FONT_BOLD, TS_H1, x + bs + dp(ui, 8), y + (bs - ui_line_h(ui, TS_H1)) / 2, w - bs,
                    t->text, dir->name);
        y += bs + dp(ui, 24);
    }
    if (dir->nkids == 0 || fs_count(dir, MK_VIDEO) == 0) {
        ui_text(ui, FONT_REGULAR, TS_LABEL, x, y, t->text_dim, "No videos here yet.");
        y += ui_line_h(ui, TS_LABEL);
    } else {
        y += folder_contents(a, dir, droot, x, y, w, nav);
    }
    y += dp(ui, 40);
    ui_scroll_end(ui, y + off - area.y);
}
