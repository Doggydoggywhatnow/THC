/* portal_music.c: the music portal ("Vinyl" look): Artist -> Album -> Track. */
#include "app.h"

#include <string.h>

static void title_of(const char *name, char *out, size_t cap)
{
    /* "01 - Song.flac" -> "Song"; "03. Song.mp3" -> "Song" */
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

static int count_audio(const FsNode *n)
{
    return fs_count(n, MK_AUDIO) + fs_count(n, MK_AUDIO_MPV);
}

static bool is_audio(const FsNode *n)
{
    return !n->dir && (n->kind == MK_AUDIO || n->kind == MK_AUDIO_MPV);
}

/* First cover image found under a folder (album art for an artist). */
static const FsNode *deep_art(const FsNode *n, int depth)
{
    const FsNode *a = fs_find_art(n);
    if (a || depth > 2) return a;
    for (int i = 0; i < n->nkids; i++)
        if (n->kids[i]->dir && (a = deep_art(n->kids[i], depth + 1))) return a;
    return NULL;
}

/* Embedded cover of the first track with one, looking inside a folder (and, for
 * an artist, inside its albums). Embedded art comes before folder images. */
static bool embedded_art(const App *a, const FsNode *n, int depth, char *buf, size_t cap)
{
    for (int i = 0; i < n->nkids; i++)
        if (is_audio(n->kids[i]) && tag_art_key(a, ROOT_MUSIC, n->kids[i], buf, cap)) return true;
    if (depth < 2)
        for (int i = 0; i < n->nkids; i++)
            if (n->kids[i]->dir && embedded_art(a, n->kids[i], depth + 1, buf, cap)) return true;
    return false;
}


/* ---- listening history: rows of tracks you played ---- */

static void ago(Sint64 then, char *out, size_t cap)
{
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    Sint64 d = (Sint64)(now / SDL_NS_PER_SECOND) - then;
    if (d < 60) SDL_snprintf(out, cap, "just now");
    else if (d < 3600) SDL_snprintf(out, cap, "%d min ago", (int)(d / 60));
    else if (d < 86400) SDL_snprintf(out, cap, "%d h ago", (int)(d / 3600));
    else SDL_snprintf(out, cap, "%d day%s ago", (int)(d / 86400), d / 86400 == 1 ? "" : "s");
}

static void history_section(App *a, float x, float *py, float w, const char *title, int mode)
{
    if (!a->hist) return;
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    PlayRow rows[16];
    int n = history_plays_list(a->hist, KIND_MUSIC, mode, rows, 16);
    FsNode *root = a->lib.root[ROOT_MUSIC];
    const FsNode *nodes[16];
    PlayRow keep[16];
    int m = 0;
    for (int i = 0; i < n; i++) {
        FsNode *f = root ? fs_find(root, rows[i].path) : NULL;
        if (f && !f->dir && f->size == rows[i].size) { nodes[m] = f; keep[m] = rows[i]; m++; }
    }
    if (!m) return;
    draw_heading(a, x, *py, title, NULL);
    *py += ui_line_h(ui, TS_TITLE) + dp(ui, 14);
    float cs = dp(ui, 150), gap = dp(ui, 18);
    float rowh = cs + dp(ui, 10) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL) * 2 + dp(ui, 8);
    float mg = dp(ui, 14);
    SDL_FRect view = { x - mg, *py - mg, w + 2 * mg, rowh + 2 * mg };
    char sid[32];
    SDL_snprintf(sid, sizeof sid, "hist-row-%d", mode);
    float off = ui_scroll_begin(ui, ui_id(sid), view, true);
    float cx = x - off;
    for (int i = 0; i < m; i++) {
        const FsNode *f = nodes[i];
        SDL_FRect card = { cx, *py, cs, cs };
        Uint32 id = ui_idi(sid, i);
        UiItemState st = ui_item(ui, id, card);
        if (st.focused || st.hover) { a->focus_file = f; a->focus_root = ROOT_MUSIC; }
        if (st.context) {
            float qx, qy;
            ctx_pos(ui, card, &qx, &qy);
            queue_menu_item(a, qx, qy, ROOT_MUSIC, f);
        }
        if (ui_visible(ui, (SDL_FRect){ cx - 8, *py - 8, cs + 16, rowh + 16 })) {
            const TagInfo *tg = tag_get(a, ROOT_MUSIC, f);
            char art[THC_PATH_MAX + 8] = "";
            if (!tag_art_key(a, ROOT_MUSIC, f, art, sizeof art) && f->parent) {
                const FsNode *an = fs_find_art(f->parent);
                if (an) app_full_path(a, ROOT_MUSIC, an, art, sizeof art);
            }
            float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
            SDL_FRect c = { card.x - lift * dp(ui, 3), card.y - lift * dp(ui, 3), card.w + lift * dp(ui, 6), card.h + lift * dp(ui, 6) };
            ui_shadow(ui, c, dp(ui, 12), dp(ui, 14), t->shadow);
            char title2[256];
            SDL_strlcpy(title2, tg && tg->title[0] ? tg->title : f->name, sizeof title2);
            draw_cover(a, c, f->parent ? f->parent->name : title2, art[0] ? art : NULL, dp(ui, 12), ICON_NOTE);
            if (st.focused) ui_focus_ring(ui, c, dp(ui, 12));
            float ty = card.y + cs + dp(ui, 10);
            ui_text_fit(ui, FONT_BOLD, TS_BODY, card.x, ty, cs, t->text, title2);
            ty += ui_line_h(ui, TS_BODY);
            const char *who = tg ? (tg->artist[0] ? tg->artist : tg->album_artist) : "";
            ui_text_fit(ui, FONT_REGULAR, TS_SMALL, card.x, ty, cs, t->text_dim, who[0] ? who : (f->parent ? f->parent->name : ""));
            ty += ui_line_h(ui, TS_SMALL);
            char meta[64];
            if (mode == PLAYS_MOST) SDL_snprintf(meta, sizeof meta, "%d play%s", keep[i].play_count, keep[i].play_count == 1 ? "" : "s");
            else if (mode == PLAYS_IN_PROGRESS) {
                char left[32];
                fmt_time(keep[i].duration - keep[i].position, left, sizeof left);
                SDL_snprintf(meta, sizeof meta, "%s left", left);
            } else ago(keep[i].last_played, meta, sizeof meta);
            ui_text_fit(ui, FONT_REGULAR, TS_SMALL, card.x, ty, cs, t->text_dim, meta);
        }
        if (st.activated) music_play_folder(a, ROOT_MUSIC, f, 0);
        cx += cs + gap;
    }
    ui_scroll_end(ui, cx + off - x + dp(ui, 14));
    *py += rowh + dp(ui, 30);
}

/* "3 of 12 tracks played" for an album, counted once per history change */
static const char *album_played_text(App *a, const FsNode *dir, char *buf, size_t cap)
{
    static const FsNode *c_dir;
    static Uint64 c_ver;
    static int c_played, c_total;
    if (c_dir != dir || c_ver != app_history_version()) {
        c_dir = dir;
        c_ver = app_history_version();
        c_played = c_total = 0;
        for (int i = 0; i < dir->nkids; i++) {
            const FsNode *k = dir->kids[i];
            if (k->dir || (k->kind != MK_AUDIO && k->kind != MK_AUDIO_MPV)) continue;
            char rel[THC_PATH_MAX];
            c_total++;
            if (history_play_count(a->hist, fs_relpath(k, rel, sizeof rel), k->size) > 0) c_played++;
        }
    }
    if (!c_total || !c_played) return NULL;
    SDL_snprintf(buf, cap, "%d of %d tracks played", c_played, c_total);
    return buf;
}

static bool cover_card(App *a, SDL_FRect r, const FsNode *n, bool round, const char *meta)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    Uint32 id = ui_idp("mcard", n);
    SDL_FRect cover = { r.x, r.y, r.w, r.w };
    UiItemState st = ui_item(ui, id, cover);
    if (st.context) {
        float cx, cy;
        ctx_pos(ui, cover, &cx, &cy);
        queue_menu_dir(a, cx, cy, ROOT_MUSIC, n);
    }
    if (!ui_visible(ui, r)) return st.activated;
    float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
    float rad = round ? r.w / 2 : dp(ui, 12);
    SDL_FRect c = { cover.x - lift * dp(ui, 4), cover.y - lift * dp(ui, 4), cover.w + lift * dp(ui, 8),
                    cover.h + lift * dp(ui, 8) };
    if (round) rad = c.w / 2;
    ui_shadow(ui, c, rad, dp(ui, 16 + lift * 14), t->shadow);
    char art[THC_PATH_MAX] = "";
    if (!embedded_art(a, n, round ? 0 : 1, art, sizeof art)) {
        const FsNode *an = round ? deep_art(n, 0) : fs_find_art(n);
        if (an) app_full_path(a, ROOT_MUSIC, an, art, sizeof art);
    }
    draw_cover(a, c, n->name, art[0] ? art : NULL, rad, round ? ICON_NONE : ICON_NOTE);
    if (!t->high_contrast && lift > 0.01f && !round) {
        /* a play button appears on hover, like a record sleeve */
        float bs = dp(ui, 40);
        ui_circle(ui, c.x + c.w - bs * 0.75f, c.y + c.h - bs * 0.75f, bs / 2, with_alpha(t->accent, (Uint8)(255 * lift)));
        ui_icon(ui, ICON_PLAY, (SDL_FRect){ c.x + c.w - bs * 1.08f, c.y + c.h - bs * 1.08f, bs * 0.66f, bs * 0.66f },
                with_alpha(t->on_accent, (Uint8)(255 * lift)));
    }
    if (st.focused) ui_focus_ring(ui, c, rad);
    float y = r.y + r.w + dp(ui, 12);
    if (round) {
        float tw = SDL_min(r.w, ui_text_w(ui, FONT_BOLD, TS_BODY, n->name));
        ui_text_fit(ui, FONT_BOLD, TS_BODY, r.x + (r.w - tw) / 2, y, r.w, t->text, n->name);
        y += ui_line_h(ui, TS_BODY);
        float mw = SDL_min(r.w, ui_text_w(ui, FONT_REGULAR, TS_SMALL, meta));
        ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + (r.w - mw) / 2, y, r.w, t->text_dim, meta);
    } else {
        ui_text_fit(ui, FONT_BOLD, TS_BODY, r.x, y, r.w, t->text, n->name);
        y += ui_line_h(ui, TS_BODY);
        ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x, y, r.w, t->text_dim, meta);
    }
    return st.activated;
}

static float card_grid(App *a, FsNode *dir, float x, float y, float w, bool round, int depth_kind)
{
    Ui *ui = &a->ui;
    float y0 = y;
    float gap = dp(ui, 24);
    float target = dp(ui, round ? 168 : 186);
    int cols = SDL_max(2, (int)((w + gap) / (target + gap)));
    float cw = (w - gap * (cols - 1)) / cols;
    float ch = cw + dp(ui, 12) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL) + dp(ui, 8);
    int c = 0;
    for (int i = 0; i < dir->nkids; i++) {
        FsNode *k = dir->kids[i];
        if (!k->dir || count_audio(k) == 0) continue;
        char meta[64];
        int albums = 0;
        for (int j = 0; j < k->nkids; j++) albums += k->kids[j]->dir && count_audio(k->kids[j]) > 0;
        int tracks = count_audio(k);
        if (depth_kind == 0 && albums > 0)
            SDL_snprintf(meta, sizeof meta, albums == 1 ? "%d album" : "%d albums", albums);
        else
            SDL_snprintf(meta, sizeof meta, tracks == 1 ? "%d track" : "%d tracks", tracks);
        if (cover_card(a, (SDL_FRect){ x + c * (cw + gap), y, cw, ch }, k, round, meta)) {
            char rel[THC_PATH_MAX];
            nav_push(a, &a->mnav, ROOT_MUSIC, fs_relpath(k, rel, sizeof rel));
        }
        if (++c == cols) { c = 0; y += ch + gap; }
    }
    if (c) y += ch + gap;
    return y - y0;
}

static float track_list(App *a, const FsNode *dir, float x, float y, float w)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float y0 = y;
    float rh = dp(ui, 52);
    const FsNode *list[512];
    int nl = music_tracks(a, ROOT_MUSIC, dir, list, 512);
    const char *folder_artist = dir->parent && dir->parent->parent ? dir->parent->name : "";
    for (int idx = 0; idx < nl; idx++) {
        const FsNode *k = list[idx];
        const TagInfo *tg = tag_get(a, ROOT_MUSIC, k);
        SDL_FRect r = { x, y, w, rh };
        Uint32 id = ui_idp("track", k);
        UiItemState st = ui_item(ui, id, r);
        if (st.focused || st.hover) { a->focus_file = k; a->focus_root = ROOT_MUSIC; }
        if (st.context) {
            float cx, cy;
            ctx_pos(ui, r, &cx, &cy);
            queue_menu_item(a, cx, cy, ROOT_MUSIC, k);
        }
        bool cur = music_is_current(a, k);
        if (ui_visible(ui, r)) {
            float hv = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 16);
            if (cur) ui_fill(ui, r, with_alpha(t->accent, t->dark ? 38 : 30), dp(ui, 10));
            else if (hv > 0.01f) ui_fill(ui, r, with_alpha(t->surface_hi, (Uint8)(255 * hv)), dp(ui, 10));
            if (st.focused) ui_focus_ring(ui, r, dp(ui, 10));
            float nx = r.x + dp(ui, 16);
            SDL_FRect nb = { nx, r.y, dp(ui, 28), rh };
            if (cur || hv > 0.5f) {
                float is = dp(ui, 16);
                ui_icon(ui, ICON_PLAY, (SDL_FRect){ nb.x + (nb.w - is) / 2, r.y + (rh - is) / 2, is, is },
                        cur ? t->accent : t->text);
            } else {
                char num[12];
                SDL_snprintf(num, sizeof num, "%d", tg && tg->track > 0 ? tg->track : idx + 1);
                ui_text_center(ui, FONT_REGULAR, TS_BODY, nb, t->text_dim, num, true);
            }
            char title[256];
            if (tg && tg->title[0]) SDL_strlcpy(title, tg->title, sizeof title);
            else title_of(k->name, title, sizeof title);
            /* show the artist when it differs from the folder's (compilations) */
            const char *who = tg ? (tg->artist[0] ? tg->artist : tg->album_artist) : "";
            bool show_who = who[0] && SDL_strcasecmp(who, folder_artist) != 0;
            float tx = nx + dp(ui, 44);
            const char *ext = path_ext(k->name);
            char fmt[16];
            SDL_strlcpy(fmt, ext, sizeof fmt);
            for (char *p = fmt; *p; p++) *p = (char)SDL_toupper(*p);
            float fw = ui_text_w(ui, FONT_BOLD, TS_SMALL, fmt) + dp(ui, 16);
            float avail = r.x + r.w - tx - fw - dp(ui, 28);
            if (show_who) {
                float lh = ui_line_h(ui, TS_BODY);
                float y0 = r.y + (rh - lh - ui_line_h(ui, TS_SMALL)) / 2;
                ui_text_fit(ui, FONT_BOLD, TS_BODY, tx, y0, avail, cur ? t->accent : t->text, title);
                ui_text_fit(ui, FONT_REGULAR, TS_SMALL, tx, y0 + lh, avail, t->text_dim, who);
            } else {
                ui_text_center(ui, FONT_BOLD, TS_BODY, (SDL_FRect){ tx, r.y, avail, rh },
                               cur ? t->accent : t->text, title, false);
            }
            SDL_FRect chip = { r.x + r.w - fw - dp(ui, 16), r.y + (rh - dp(ui, 22)) / 2, fw, dp(ui, 22) };
            ui_stroke(ui, chip, with_alpha(t->text_dim, 120), chip.h / 2, dp(ui, 1));
            ui_text_center(ui, FONT_BOLD, TS_SMALL, chip, t->text_dim, fmt, true);
        }
        if (st.activated) music_play_folder(a, ROOT_MUSIC, dir, idx);
        y += rh + dp(ui, 2);
    }
    return y - y0;
}

void portal_music(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    PortalNav *nav = &a->mnav;
    FsNode *root = a->lib.root[ROOT_MUSIC];
    if (!a->set.music_dir[0]) {
        bool pick = false;
        draw_empty(a, area, ICON_NOTE, "Add your music",
                   "Pick your music folder. THC reads it as Artist, Album, Track.",
                   "Choose music folder", &pick);
        if (pick) app_pick_folder(a, 0, NULL);
        return;
    }
    if (!root) {
        bool pick = false;
        draw_empty(a, area, ICON_NOTE, a->scanning ? "Reading your library" : "Folder not found",
                   a->scanning ? "This only takes a moment." : a->set.music_dir,
                   a->scanning ? NULL : "Choose another folder", &pick);
        if (pick) app_pick_folder(a, 0, a->set.music_dir);
        return;
    }
    FsNode *dir = nav->depth ? nav_node(a, nav) : root;
    if (!dir) { nav_reset(nav); dir = root; }

    float pad = SDL_max(dp(ui, 28), (area.w - dp(ui, 1400)) / 2);
    float x = area.x + pad, w = area.w - 2 * pad;
    float off = ui_scroll_begin(ui, ui_idi("mpage", nav->depth), area, false);
    float y = area.y + dp(ui, 28) - off;

    int albums_here = 0;
    for (int i = 0; i < dir->nkids; i++) albums_here += dir->kids[i]->dir && count_audio(dir->kids[i]) > 0;
    int tracks_here = 0;
    for (int i = 0; i < dir->nkids; i++) tracks_here += is_audio(dir->kids[i]);

    if (nav->depth == 0) {
        ui_text(ui, FONT_BOLD, TS_H1, x, y, t->text, "Music");
        y += ui_line_h(ui, TS_H1);
        char sub[96];
        SDL_snprintf(sub, sizeof sub, "%d artists  \xC2\xB7  %d tracks", albums_here, count_audio(root));
        ui_text(ui, FONT_REGULAR, TS_BODY, x, y, t->text_dim, sub);
        y += ui_line_h(ui, TS_BODY) + dp(ui, 28);
        history_section(a, x, &y, w, "Continue listening", PLAYS_IN_PROGRESS);
        history_section(a, x, &y, w, "Recently played", PLAYS_RECENT);
        history_section(a, x, &y, w, "Most played", PLAYS_MOST);
        if (albums_here) {
            draw_heading(a, x, y, "Artists", NULL);
            y += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
            y += card_grid(a, dir, x, y, w, true, 0);
        }
        if (tracks_here) {
            draw_heading(a, x, y, "Songs", NULL);
            y += ui_line_h(ui, TS_TITLE) + dp(ui, 12);
            y += track_list(a, dir, x, y, w);
        }
    } else if (albums_here && !tracks_here) {
        /* artist page */
        float bs = dp(ui, 44);
        if (ui_icon_button(ui, ui_id("mback"), (SDL_FRect){ x - dp(ui, 6), y, bs, bs }, ICON_BACK, "Back"))
            nav_pop(a, nav);
        y += bs + dp(ui, 12);
        float av = dp(ui, 120);
        char art[THC_PATH_MAX] = "";
        if (!embedded_art(a, dir, 0, art, sizeof art)) {
            const FsNode *an = deep_art(dir, 0);
            if (an) app_full_path(a, ROOT_MUSIC, an, art, sizeof art);
        }
        ui_shadow(ui, (SDL_FRect){ x, y, av, av }, av / 2, dp(ui, 24), t->shadow);
        draw_cover(a, (SDL_FRect){ x, y, av, av }, dir->name, art[0] ? art : NULL, av / 2, ICON_NONE);
        float tx = x + av + dp(ui, 28);
        ui_text(ui, FONT_BOLD, TS_SMALL, tx, y + dp(ui, 22), t->accent, "ARTIST");
        ui_text_fit(ui, FONT_BOLD, TS_HERO, tx, y + dp(ui, 22) + ui_line_h(ui, TS_SMALL), w - av, t->text, dir->name);
        y += av + dp(ui, 32);
        draw_heading(a, x, y, "Albums", NULL);
        y += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
        y += card_grid(a, dir, x, y, w, false, 1);
    } else {
        /* album page: big cover on the left, tracks on the right */
        float bs = dp(ui, 44);
        if (ui_icon_button(ui, ui_id("mback"), (SDL_FRect){ x - dp(ui, 6), y, bs, bs }, ICON_BACK, "Back"))
            nav_pop(a, nav);
        y += bs + dp(ui, 12);
        bool side = w > dp(ui, 760);
        float cs = side ? SDL_min(dp(ui, 300), w * 0.34f) : SDL_min(dp(ui, 260), w);
        char art[THC_PATH_MAX] = "";
        if (!embedded_art(a, dir, 2, art, sizeof art)) {
            const FsNode *an = fs_find_art(dir);
            if (an) app_full_path(a, ROOT_MUSIC, an, art, sizeof art);
        }
        SDL_FRect cr = { x, y, cs, cs };
        ui_shadow(ui, cr, dp(ui, 16), dp(ui, 40), t->shadow);
        draw_cover(a, cr, dir->name, art[0] ? art : NULL, dp(ui, 16), ICON_NOTE);
        float tx = side ? x + cs + dp(ui, 36) : x;
        float ty = side ? y : y + cs + dp(ui, 24);
        float tw = side ? w - cs - dp(ui, 36) : w;
        ui_text(ui, FONT_BOLD, TS_SMALL, tx, ty, t->accent, "ALBUM");
        ty += ui_line_h(ui, TS_SMALL);
        ty += ui_text_wrap(ui, FONT_BOLD, TS_H1, tx, ty, tw, t->text, dir->name, 2);
        /* album details from the first track's tags: artist, year, genre */
        const TagInfo *ft = NULL;
        for (int i = 0; i < dir->nkids && !ft; i++)
            if (is_audio(dir->kids[i])) ft = tag_get(a, ROOT_MUSIC, dir->kids[i]);
        const char *who = ft ? (ft->album_artist[0] ? ft->album_artist : ft->artist) : "";
        if (!who[0] && dir->parent && dir->parent->parent) who = dir->parent->name;
        if (who[0]) {
            ui_text_fit(ui, FONT_REGULAR, TS_LABEL, tx, ty, tw, t->text_dim, who);
            ty += ui_line_h(ui, TS_LABEL);
        }
        if (ft && (ft->year || ft->genre[0])) {
            char info[160] = "";
            if (ft->year) SDL_snprintf(info, sizeof info, "%d", ft->year);
            if (ft->genre[0]) {
                size_t l = SDL_strlen(info);
                SDL_snprintf(info + l, sizeof info - l, "%s%s", l ? "  \xC2\xB7  " : "", ft->genre);
            }
            ui_text_fit(ui, FONT_REGULAR, TS_BODY, tx, ty, tw, t->text_dim, info);
            ty += ui_line_h(ui, TS_BODY);
        }
        {
            char pb[64];
            const char *pt = tracks_here ? album_played_text(a, dir, pb, sizeof pb) : NULL;
            if (pt) {
                ui_text_fit(ui, FONT_BOLD, TS_SMALL, tx, ty, tw, t->accent, pt);
                ty += ui_line_h(ui, TS_SMALL);
            }
        }
        ty += dp(ui, 16);
        if (tracks_here) {
            if (ui_button(ui, ui_id("album-play"), (SDL_FRect){ tx, ty, dp(ui, 150), dp(ui, 46) }, "Play", ICON_PLAY, true))
                music_play_folder(a, ROOT_MUSIC, dir, 0);
            if (ui_button(ui, ui_id("album-add"), (SDL_FRect){ tx + dp(ui, 162), ty, dp(ui, 190), dp(ui, 46) }, "Add to queue", ICON_QUEUE, false))
                queue_add_dir(a, ROOT_MUSIC, dir, false);
            ty += dp(ui, 46) + dp(ui, 20);
            ty += track_list(a, dir, tx, ty, tw);
        }
        if (albums_here) {
            ty += dp(ui, 16);
            draw_heading(a, tx, ty, "More", NULL);
            ty += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
            ty += card_grid(a, dir, tx, ty, tw, false, 1);
        }
        y = SDL_max(ty, y + cs);
    }
    y += dp(ui, 140); /* room for the now-playing bar */
    ui_scroll_end(ui, y + off - area.y);
}
