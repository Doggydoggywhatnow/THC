/* portal_nsfw.c: the NSFW portal (#13). It reuses the Video, Music and Audiobooks screens: while one is
 * drawn, a->lib, a->vlib and the navigation are swapped for the NSFW ones, so the screens show only NSFW
 * items without a line of their own filtering code. The icon is a beer bottle with an XXX label. */
#include "app.h"
#include "pin.h"

static int count_kind(const FsNode *n, bool a_audio, bool b_book)
{
    if (!n) return 0;
    int c = fs_count(n, MK_VIDEO) * (a_audio || b_book ? 0 : 1);
    if (a_audio) c = fs_count(n, MK_AUDIO) + fs_count(n, MK_AUDIO_MPV);
    if (b_book) c = fs_count(n, MK_BOOK);
    return c;
}

static int items_in(const App *a, int sub)
{
    int c = 0;
    if (sub == 0) {
        c += count_kind(a->nlib.root[ROOT_VIDEO], false, false);
        c += count_kind(a->nlib.root[ROOT_NSFW], false, false);       /* videos from the NSFW folder (#34) */
        for (int i = 0; i < SECTION_COUNT; i++) c += count_kind(a->nlib.root[ROOT_SECTION0 + i], false, false);
    } else if (sub == 1) {
        c += count_kind(a->nlib.root[ROOT_MUSIC], true, false);
    } else {
        c += count_kind(a->nlib.root[ROOT_BOOKS], false, true);
        c += count_kind(a->nlib.root[ROOT_BOOKS], true, false);
    }
    return c;
}


/* ---- the Folders view (#34): what is in the NSFW folder, sorted by subfolder ---- */

bool nsfw_folders_back(App *a)
{
    if (a->nfold_unsorted) {
        a->nfold_unsorted = false;
        return true;
    }
    char *slash = SDL_strrchr(a->nfold_rel, '/');
    if (slash) {
        *slash = 0;
        return true;
    }
    if (a->nfold_rel[0]) {
        a->nfold_rel[0] = 0;
        return true;
    }
    return false;
}

static void folders_open(App *a, const FsNode *g_dir, const char *name, bool unsorted)
{
    if (unsorted) {
        a->nfold_unsorted = true;
        return;
    }
    char rel[THC_PATH_MAX];
    fs_relpath(g_dir, rel, sizeof rel);
    SDL_strlcpy(a->nfold_rel, rel[0] ? rel : name, sizeof a->nfold_rel);
}

static void folder_row_draw(App *a, SDL_FRect r, Uint32 id, UiIcon icon, const char *name, const char *meta, UiItemState *st)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    *st = ui_item(ui, id, r);
    float lift = ui_anim_id(ui, id, (st->hover || st->focused) ? 1.0f : 0.0f, 14);
    ui_fill(ui, r, lift > 0.5f ? t->surface_hi : t->surface, dp(ui, 12));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    float is = dp(ui, 24);
    ui_icon(ui, icon, (SDL_FRect){ r.x + dp(ui, 18), r.y + (r.h - is) / 2, is, is }, t->accent);
    float tx = r.x + dp(ui, 60);
    float mw = ui_text_w(ui, FONT_REGULAR, TS_SMALL, meta);
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, tx, r.y + (r.h - ui_line_h(ui, TS_LABEL)) / 2, r.w - dp(ui, 60) - mw - dp(ui, 40), t->text, name);
    ui_text(ui, FONT_REGULAR, TS_SMALL, r.x + r.w - mw - dp(ui, 18), r.y + (r.h - ui_line_h(ui, TS_SMALL)) / 2, t->text_dim, meta);
    if (st->focused) ui_focus_ring(ui, r, dp(ui, 12));
}

static void portal_folders(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    FsNode *top = a->lib.root[ROOT_NSFW];          /* drawn inside the swapped view: a->lib holds the NSFW trees */
    const FsNode *dir = top;
    if (top && a->nfold_rel[0]) dir = fs_find(top, a->nfold_rel);
    if (!dir) {                                    /* the folder changed under us (a rescan): back to the top */
        a->nfold_rel[0] = 0;
        a->nfold_unsorted = false;
        dir = top;
    }
    float pad = SDL_max(dp(ui, 32), (area.w - dp(ui, 1100)) / 2);
    float w = area.w - 2 * pad;
    float y = area.y + dp(ui, 8);
    char crumb[THC_PATH_MAX + 32];
    SDL_strlcpy(crumb, "All folders", sizeof crumb);
    if (a->nfold_rel[0]) SDL_snprintf(crumb, sizeof crumb, "%s", a->nfold_rel);
    if (a->nfold_unsorted) SDL_strlcat(crumb, a->nfold_rel[0] ? "  \xC2\xB7  Unsorted" : "Unsorted", sizeof crumb);
    float hx = area.x + pad;
    if (a->nfold_rel[0] || a->nfold_unsorted) {
        if (ui_button(ui, ui_id("nfold-back"), (SDL_FRect){ hx, y, dp(ui, 110), dp(ui, 40) }, "Back", ICON_BACK, false)) nsfw_folders_back(a);
        hx += dp(ui, 122);
    }
    ui_text_fit(ui, FONT_BOLD, TS_H2, hx, y + (dp(ui, 40) - ui_line_h(ui, TS_H2)) / 2, area.x + area.w - pad - hx, t->text, crumb);
    y += dp(ui, 56);
    SDL_FRect view = { area.x, y, area.w, area.y + area.h - y };
    float off = ui_scroll_begin(ui, ui_id("nfold-scroll"), view, false);
    float cy = y - off;
    float rh = dp(ui, 60), gap = dp(ui, 8);
    char meta[64];
    NsfwGroup g[256];
    int ng = dir ? nsfw_groups(dir, g, 256) : 0;
    /* a folder that holds only loose files lists them directly, instead of one "Unsorted" group */
    bool files = a->nfold_unsorted || (a->nfold_rel[0] && ng == 1 && g[0].dir == NULL);
    if (dir && files) {
        for (int i = 0; i < dir->nkids; i++) {
            const FsNode *f = dir->kids[i];
            if (f->dir || !nsfw_item_count(f)) continue;
            SDL_FRect r = { area.x + pad, cy, w, rh };
            UiItemState st = { 0 };
            UiIcon ic = f->kind == MK_VIDEO ? ICON_FILM : f->kind == MK_BOOK ? ICON_BOOK : ICON_NOTE;
            SDL_strlcpy(meta, f->kind == MK_VIDEO ? "Video" : f->kind == MK_BOOK ? "Audiobook" : "Music", sizeof meta);
            if (ui_visible(ui, r)) folder_row_draw(a, r, ui_idp("nfold-file", f), ic, f->name, meta, &st);
            else st = ui_item(ui, ui_idp("nfold-file", f), r);
            if (st.focused || st.hover) { a->focus_file = f; a->focus_root = ROOT_NSFW; }
            if (st.activated) {
                if (f->kind == MK_VIDEO) {
                    app_play_video(a, ROOT_NSFW, f);
                } else {
                    const FsNode *list[512];
                    int nl = music_tracks(a, ROOT_NSFW, dir, list, 512), idx = 0;
                    for (int k = 0; k < nl; k++)
                        if (list[k] == f) idx = k;
                    music_play_folder(a, ROOT_NSFW, dir, idx);
                }
            }
            cy += rh + gap;
        }
    } else if (dir) {
        for (int i = 0; i < ng; i++) {
            SDL_FRect r = { area.x + pad, cy, w, rh };
            SDL_snprintf(meta, sizeof meta, "%d item%s", g[i].items, g[i].items == 1 ? "" : "s");
            UiItemState st = { 0 };
            Uint32 id = ui_idi("nfold-grp", i);
            if (ui_visible(ui, r)) folder_row_draw(a, r, id, ICON_FOLDER, g[i].name, meta, &st);
            else st = ui_item(ui, id, r);
            if (st.activated) folders_open(a, g[i].dir, g[i].name, g[i].dir == NULL);
            cy += rh + gap;
        }
    }
    ui_scroll_end(ui, cy + off - y);
}

/* A locked portal shows only the bottle and a field: no titles, no pictures. */
void portal_nsfw_locked(App *a, SDL_FRect area);

void portal_nsfw(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    if (!a->nsfw_unlocked) {
        portal_nsfw_locked(a, area);
        return;
    }
    /* sub-tabs: Video, Music, Audiobooks, Folders (only with an NSFW folder), Screenshots */
    static const char *const names[] = { "Video", "Music", "Audiobooks", "Screenshots", "Folders" };
    static const int order[] = { 0, 1, 2, 4, 3 };
    bool has_dir = a->set.nsfw_dir[0] != 0;
    if (!has_dir && a->nsfw_sub == 4) a->nsfw_sub = 0;
    float pad = SDL_max(dp(ui, 32), (area.w - dp(ui, 1400)) / 2);
    float x = area.x + pad, y = area.y + dp(ui, 14);
    for (int k = 0; k < 5; k++) {
        int i = order[k];
        if (i == 4 && !has_dir) continue;
        char label[48];
        int n = i == 3 ? screenshots_nsfw_count(a) : i == 4 ? nsfw_item_count(a->nlib.root[ROOT_NSFW]) : items_in(a, i);
        SDL_snprintf(label, sizeof label, "%s  %d", names[i], n);
        float pw = ui_text_w(ui, FONT_BOLD, TS_BODY, label) + dp(ui, 34);
        if (ui_pill(ui, ui_idi("nsfw-sub", i), (SDL_FRect){ x, y, pw, dp(ui, 36) }, label, a->nsfw_sub == i)) a->nsfw_sub = i;
        x += pw + dp(ui, 8);
    }
    ui_fill(ui, (SDL_FRect){ area.x, y + dp(ui, 46), area.w, 1 }, with_alpha(t->border, 120), 0);
    SDL_FRect inner = { area.x, area.y + dp(ui, 62), area.w, area.h - dp(ui, 62) };
    if (a->nsfw_sub == 3) {
        portal_screenshots_nsfw(a, inner);
        return;
    }
    if (a->nsfw_sub == 4) {
        app_nsfw_view_enter(a);
        if (nsfw_item_count(a->lib.root[ROOT_NSFW]) == 0)
            draw_empty(a, inner, ICON_BEER, "Nothing in the NSFW folder", "THC found no videos, music or audiobooks in it yet.", NULL, NULL);
        else
            portal_folders(a, inner);
        app_nsfw_view_leave(a);
        return;
    }
    if (items_in(a, a->nsfw_sub) == 0) {
        draw_empty(a, inner, ICON_BEER, "Nothing marked NSFW here yet",
                   "Right-click a video, song, album or folder and choose Mark as NSFW (or press N while it is selected). "
                   "Marked items leave the normal portals and show up here.",
                   NULL, NULL);
        return;
    }
    app_nsfw_view_enter(a);
    switch (a->nsfw_sub) {
    case 0: portal_video(a, inner); break;
    case 1: portal_music(a, inner); break;
    default: portal_books(a, inner); break;
    }
    app_nsfw_view_leave(a);
}

void portal_nsfw_locked(App *a, SDL_FRect area)
{
    static char pin[64], msg[64];
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float cx = area.x + area.w / 2, cy = area.y + area.h / 2 - dp(ui, 90);
    float is = dp(ui, 96);
    ui_icon(ui, ICON_BEER, (SDL_FRect){ cx - is / 2, cy - is / 2, is, is }, t->accent);
    float y = cy + is / 2 + dp(ui, 12);
    ui_text_center(ui, FONT_BOLD, TS_H2, (SDL_FRect){ area.x, y, area.w, ui_line_h(ui, TS_H2) }, t->text, "Locked", true);
    y += ui_line_h(ui, TS_H2) + dp(ui, 14);
    float fw = SDL_min(dp(ui, 320), area.w - dp(ui, 64));
    SDL_FRect field = { cx - fw / 2, y, fw, dp(ui, 52) };
    bool enter = ui_passfield(ui, ui_id("nsfw-pin"), field, pin, sizeof pin, "PIN");
    y += dp(ui, 52) + dp(ui, 12);
    SDL_FRect b = { cx - dp(ui, 80), y, dp(ui, 160), dp(ui, 46) };
    if (ui_button(ui, ui_id("nsfw-unlock"), b, "Open", ICON_CHECK, true) || enter) {
        if (pin_verify(pin, a->set.nsfw_pin_salt, a->set.nsfw_pin_hash)) {
            a->nsfw_unlocked = true;
            msg[0] = 0;
        } else {
            SDL_strlcpy(msg, "That PIN is not right.", sizeof msg);
        }
        memset(pin, 0, sizeof pin);
    }
    if (msg[0]) ui_text_center(ui, FONT_REGULAR, TS_BODY, (SDL_FRect){ area.x, y + dp(ui, 56), area.w, ui_line_h(ui, TS_BODY) }, t->text_dim, msg, true);
    /* the field takes the keyboard as soon as the portal opens */
    if (ui->focus == 0) ui->focus = ui_id("nsfw-pin");
}
