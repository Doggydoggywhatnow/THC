/* chrome.c: pieces shared by every screen: background, top bar, cards. */
#include "app.h"
#include "tablayout.h"

#include <math.h>

void fmt_time(double secs, char *buf, size_t cap)
{
    if (secs < 0 || secs != secs) secs = 0;
    int s = (int)secs;
    if (s >= 3600) SDL_snprintf(buf, cap, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else SDL_snprintf(buf, cap, "%d:%02d", s / 60, s % 60);
}

void chrome_background(App *a)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    SDL_FRect full = { 0, 0, (float)ui->w, (float)ui->h };
    ui_fill_grad(ui, full, t->bg, t->bg2, 0);
    if (t->high_contrast) return;
    /* a soft glow in the portal's accent colors, like stage lighting */
    float r = dp(ui, 220);
    ui_shadow(ui, (SDL_FRect){ -r * 0.6f, -r * 1.1f, r * 2.4f, r * 2.0f }, r, dp(ui, 420),
              with_alpha(t->accent, t->dark ? 34 : 26));
    ui_shadow(ui, (SDL_FRect){ ui->w - r * 1.4f, -r * 1.3f, r * 2.0f, r * 1.8f }, r, dp(ui, 380),
              with_alpha(t->accent2, t->dark ? 26 : 18));
}

typedef struct TabDef { Screen s; const char *label; UiIcon icon; } TabDef;
static const TabDef k_tabs[] = {
    { SCREEN_VIDEO, "Video", ICON_FILM },
    { SCREEN_MUSIC, "Music", ICON_NOTE },
    { SCREEN_BOOKS, "Audiobooks", ICON_BOOK },
    { SCREEN_SHOTS, "Screenshots", ICON_CAMERA },
};
#define NTABS ((int)(sizeof k_tabs / sizeof k_tabs[0]))

void chrome_topbar(App *a)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float h = dp(ui, TOPBAR_DP);
    SDL_FRect bar = { 0, 0, (float)ui->w, h };
    /* frosted bar: background color, mostly opaque, fading at the bottom */
    ui_fill_grad(ui, bar, with_alpha(t->bg, 245), with_alpha(t->bg, t->high_contrast ? 255 : 215), 0);
    ui_fill(ui, (SDL_FRect){ 0, h - 1, (float)ui->w, 1 }, with_alpha(t->border, t->high_contrast ? 255 : 120), 0);

    /* wordmark: the logo is the way in to About and settings */
    float x = dp(ui, TOPBAR_EDGE_MARGIN);
    float lh = ui_line_h(ui, TS_H2);
    float lw0 = ui_text_w(ui, FONT_BOLD, TS_H2, "THC");
    SDL_FRect logo = { x - dp(ui, 10), (h - dp(ui, 48)) / 2, lw0 + dp(ui, 34), dp(ui, 48) };
    Uint32 logo_id = ui_id("logo-btn");
    UiItemState ls = ui_item(ui, logo_id, logo);
    bool on_about = a->screen == SCREEN_SETTINGS;
    float lhv = ui_anim_id(ui, logo_id, (ls.hover || on_about) ? 1.0f : 0.0f, 16);
    if (lhv > 0.01f) ui_fill(ui, logo, with_alpha(t->surface_hi, (Uint8)(255 * lhv)), logo.h / 2);
    float lw = ui_text(ui, FONT_BOLD, TS_H2, x, (h - lh) / 2, t->text, "THC");
    ui_circle(ui, x + lw + dp(ui, 5), (h + lh) / 2 - lh * 0.33f, dp(ui, 4), t->accent);
    if (ls.focused) ui_focus_ring(ui, logo, logo.h / 2);
    if (ls.hover || ls.focused) ui_tooltip(ui, logo, "About THC and settings");
    if (ls.activated) app_go(a, on_about ? (a->last_portal ? a->last_portal : SCREEN_VIDEO) : SCREEN_SETTINGS);
    x += lw + dp(ui, LOGO_TO_TABS_GAP);

    /* portal tabs */
    /* the NSFW tab is not here at all while the portal is off or the tab is hidden */
    TabDef tabs[NTABS + 1];
    int ntabs = 0;
    for (int i = 0; i < NTABS; i++) tabs[ntabs++] = k_tabs[i];
    if (app_nsfw_tab_visible(a)) tabs[ntabs++] = (TabDef){ SCREEN_NSFW, "NSFW", ICON_BEER };
    /* when the window is narrow only the selected tab keeps its label, the others are icons (tablayout.c) */
    float label_w[TAB_MAX];
    int selected = -1;
    for (int i = 0; i < ntabs; i++) {
        label_w[i] = ui_text_w(ui, FONT_BOLD, TS_BODY, tabs[i].label);
        if (a->screen == tabs[i].s) selected = i;
    }
    TabRect tr[TAB_MAX];
    tab_layout(label_w, ntabs, selected, x, (float)ui->w, ui->s, tr);
    for (int i = 0; i < ntabs; i++) {
        bool sel = i == selected;
        bool label = tr[i].labelled;
        SDL_FRect r = { tr[i].x, (h - dp(ui, TAB_H)) / 2, tr[i].w, dp(ui, TAB_H) };
        Uint32 id = ui_idi("tab", i);
        UiItemState st = ui_item(ui, id, r);
        float hv = ui_anim_id(ui, id, (st.hover || sel) ? 1.0f : 0.0f, 16);
        if (sel) ui_fill(ui, r, t->text, r.h / 2);
        else if (hv > 0.01f) ui_fill(ui, r, with_alpha(t->surface_hi, (Uint8)(255 * hv)), r.h / 2);
        Color fg = sel ? t->bg2 : t->text;
        float is = dp(ui, TAB_ICON);
        ui_icon(ui, tabs[i].icon, (SDL_FRect){ label ? r.x + dp(ui, TAB_PAD_X) : r.x + (r.w - is) / 2, r.y + (r.h - is) / 2, is, is }, fg);
        if (label)
            ui_text_center(ui, FONT_BOLD, TS_BODY, (SDL_FRect){ r.x + dp(ui, TAB_PAD_X + TAB_ICON + TAB_ICON_LABEL_GAP), r.y, r.w - dp(ui, TAB_PAD_X + TAB_ICON + TAB_ICON_LABEL_GAP + 8), r.h }, fg,
                           tabs[i].label, false);
        else
            { if (st.hover || st.focused) ui_tooltip(ui, r, tabs[i].label); }
        if (st.focused) ui_focus_ring(ui, r, r.h / 2);
        if (st.activated) app_go(a, tabs[i].s);
        if (st.context && tabs[i].s == SCREEN_NSFW) {
            float cx, cy;
            ctx_pos(ui, r, &cx, &cy);
            chrome_nsfw_tab_menu(a, cx, cy);
        }
    }

    /* right side: scan status */
    float rx = ui->w - dp(ui, TOPBAR_EDGE_MARGIN);
    /* the queue button, with the number of items waiting */
    {
        float bs = dp(ui, 44);
        SDL_FRect qb = { rx - bs, (h - bs) / 2, bs, bs };
        if (a->queue_open) ui_fill(ui, qb, t->surface_hi, bs / 2);
        if (ui_icon_button(ui, ui_id("queue-btn"), qb, ICON_QUEUE, "Queue")) a->queue_open = !a->queue_open;
        int waiting = player_queue(QUEUE_MUSIC)->n + player_queue(QUEUE_VIDEO)->n;
        if (waiting > 0) {
            char nb[16];
            SDL_snprintf(nb, sizeof nb, "%d", waiting > 99 ? 99 : waiting);
            float bw = SDL_max(dp(ui, 18), ui_text_w(ui, FONT_BOLD, TS_SMALL, nb) + dp(ui, 10));
            SDL_FRect badge = { qb.x + qb.w - bw + dp(ui, 2), qb.y, bw, dp(ui, 18) };
            ui_fill(ui, badge, t->accent, badge.h / 2);
            ui_text_center(ui, FONT_BOLD, TS_SMALL, badge, t->on_accent, nb, true);
        }
        rx -= bs + dp(ui, 12);
    }
    if (a->scanning) {
        const char *msg = "Scanning library";
        float tw = ui_text_w(ui, FONT_REGULAR, TS_SMALL, msg);
        float cx = rx - tw;
        ui_text(ui, FONT_REGULAR, TS_SMALL, cx, (h - ui_line_h(ui, TS_SMALL)) / 2, t->text_dim, msg);
        float ph = (float)fmod(ui->time * 2.0, 1.0);
        ui_circle(ui, cx - dp(ui, 12), h / 2, dp(ui, 3 + 2 * sinf(ph * 6.283f)), t->accent);
        ui->needs_redraw = true;
    }
}

void chrome_toast(App *a)
{
    Ui *ui = &a->ui;
    if (!a->toast[0] || ui->time > a->toast_until) {
        a->toast[0] = 0;
        return;
    }
    const Theme *t = &ui->th;
    float tw = ui_text_w(ui, FONT_BOLD, TS_BODY, a->toast) + dp(ui, 40);
    float th = dp(ui, 48);
    SDL_FRect r = { (ui->w - tw) / 2, ui->h - music_bar_height(a) - th - dp(ui, 28), tw, th };
    float ub = a->undo.active ? ui_text_w(ui, FONT_BOLD, TS_BODY, "Undo") + dp(ui, 36) : 0;
    r.x -= ub / 2;
    r.w += ub;
    ui_shadow(ui, r, th / 2, dp(ui, 24), t->shadow);
    ui_fill(ui, r, t->text, th / 2);
    ui_text_center(ui, FONT_BOLD, TS_BODY, (SDL_FRect){ r.x, r.y, r.w - ub, r.h }, t->bg2, a->toast, true);
    if (ub > 0) {                       /* an Undo button for the last mark (items just left the normal portals) */
        SDL_FRect u = { r.x + r.w - ub - dp(ui, 4), r.y + dp(ui, 6), ub, th - dp(ui, 12) };
        ui_fill(ui, u, t->accent, u.h / 2);
        ui_text_center(ui, FONT_BOLD, TS_BODY, u, t->on_accent, "Undo", false);
        UiItemState us = ui_item(ui, ui_id("toast-undo"), u);
        if (us.focused) ui_focus_ring(ui, u, u.h / 2);
        if (us.activated) app_nsfw_undo(a);
    }
    ui->needs_redraw = true;
}

void draw_art(App *a, SDL_FRect r, const char *seed, const char *art_path, float radius)
{
    Ui *ui = &a->ui;
    if (!ui_visible(ui, r)) return;
    SDL_Texture *tex = art_path ? art_get(a, art_path) : NULL;
    if (tex) {
        ui_image(ui, tex, r, radius, 255);
        return;
    }
    Color c1, c2;
    art_colors(seed, &c1, &c2);
    if (ui->th.high_contrast) {
        ui_fill(ui, r, ui->th.surface, radius);
        ui_stroke(ui, r, ui->th.border, radius, dp(ui, 2));
        return;
    }
    ui_fill_grad_h(ui, r, c2, c1, radius);
    ui_fill_grad(ui, r, rgba(0xFFFFFF, 30), rgba(0x000000, 40), radius);
}

void draw_cover(App *a, SDL_FRect r, const char *title, const char *art_path, float radius, UiIcon icon)
{
    Ui *ui = &a->ui;
    if (!ui_visible(ui, r)) return;
    SDL_Texture *tex = art_path ? art_get(a, art_path) : NULL;
    if (tex) {
        ui_image(ui, tex, r, radius, 255);
        return;
    }
    Color c1, c2;
    art_colors(title, &c1, &c2);
    if (ui->th.high_contrast) {
        ui_fill(ui, r, ui->th.surface, radius);
        ui_stroke(ui, r, ui->th.border, radius, dp(ui, 2));
    } else {
        ui_fill_grad(ui, r, c1, c2, radius);
        /* a soft sheen from the right and a vignette at the bottom, for depth */
        ui_fill_grad_h(ui, r, rgba(0xFFFFFF, 0), rgba(0xFFFFFF, 34), radius);
        ui_fill_grad(ui, r, rgba(0x000000, 0), rgba(0x000000, 70), radius);
    }
    Color fg = ui->th.high_contrast ? ui->th.text : rgb(0xFFFFFF);
    float pad = SDL_max(dp(ui, 8), r.w * 0.08f);
    if (icon != ICON_NONE) {
        float is = SDL_max(dp(ui, 14), r.w * 0.14f);
        ui_icon(ui, icon, (SDL_FRect){ r.x + pad, r.y + pad, is, is }, with_alpha(fg, 210));
    }
    if (radius >= r.w / 2 - 1) {
        /* round avatar: initials */
        char ini[16] = "";
        int n = 0;
        bool start = true;
        for (const char *p = title; *p && n < 2; p++) {
            if (*p == ' ' || *p == '-' || *p == '_') { start = true; continue; }
            if (start && (Uint8)*p < 0x80 && SDL_isalnum(*p)) { ini[n++] = (char)SDL_toupper(*p); ini[n] = 0; }
            start = false;
        }
        float size = SDL_clamp(r.w / ui->s / 3.2f, 14, 64) / ui->text_scale;
        ui_text_center(ui, FONT_BOLD, size, r, fg, ini[0] ? ini : "?", true);
        return;
    }
    /* title in the lower part, sized to the card */
    float size = SDL_clamp(r.w / ui->s / 9.0f, 12, 30) / ui->text_scale;
    ui_clip_push(ui, r);
    float lh = ui_line_h(ui, size);
    int lines = r.h > r.w * 1.2f ? 3 : 2;
    float y = r.y + r.h - pad - lh * lines;
    ui_text_wrap(ui, FONT_BOLD, size, r.x + pad, y, r.w - 2 * pad, fg, title, lines);
    ui_clip_pop(ui);
}

void draw_heading(App *a, float x, float y, const char *title, const char *note)
{
    Ui *ui = &a->ui;
    float w = ui_text(ui, FONT_BOLD, TS_TITLE, x, y, ui->th.text, title);
    if (note && *note) {
        float dy = ui_ascent(ui, TS_TITLE) - ui_ascent(ui, TS_BODY);
        ui_text(ui, FONT_REGULAR, TS_BODY, x + w + dp(ui, 12), y + dy, ui->th.text_dim, note);
    }
}

void draw_empty(App *a, SDL_FRect r, UiIcon icon, const char *title, const char *body,
                const char *button, bool *pressed)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float w = SDL_min(r.w - dp(ui, 48), dp(ui, 520));
    float x = r.x + (r.w - w) / 2;
    float y = r.y + SDL_max(dp(ui, 40), r.h * 0.18f);
    float is = dp(ui, 72);
    ui_circle(ui, x + w / 2, y + is / 2, is / 2, t->surface);
    ui_icon(ui, icon, (SDL_FRect){ x + w / 2 - is * 0.3f, y + is * 0.2f, is * 0.6f, is * 0.6f }, t->accent);
    y += is + dp(ui, 20);
    float tw = ui_text_w(ui, FONT_BOLD, TS_H2, title);
    ui_text(ui, FONT_BOLD, TS_H2, x + (w - SDL_min(tw, w)) / 2, y, t->text, title);
    y += ui_line_h(ui, TS_H2) + dp(ui, 8);
    float bw = ui_text_w(ui, FONT_REGULAR, TS_BODY, body);
    if (bw < w) {
        ui_text(ui, FONT_REGULAR, TS_BODY, x + (w - bw) / 2, y, t->text_dim, body);
        y += ui_line_h(ui, TS_BODY);
    } else {
        y += ui_text_wrap(ui, FONT_REGULAR, TS_BODY, x, y, w, t->text_dim, body, 4);
    }
    if (button) {
        y += dp(ui, 24);
        float bwid = ui_text_w(ui, FONT_BOLD, TS_LABEL, button) + dp(ui, 72);
        SDL_FRect b = { x + (w - bwid) / 2, y, bwid, dp(ui, 48) };
        bool p = ui_button(ui, ui_id(button), b, button, ICON_FOLDER, true);
        if (pressed) *pressed = p;
    }
}

/* ---------------------------------------------------------------- */

void nav_reset(PortalNav *n)
{
    SDL_zerop(n);
}

void nav_push(App *a, PortalNav *n, int root, const char *rel)
{
    nav_push_kind(a, n, 0, 0, root, rel);
}

void nav_push_kind(App *a, PortalNav *n, int kind, int arg, int root, const char *rel)
{
    if (n->depth >= NAV_DEPTH - 1) return;
    n->focus[n->depth] = a->ui.focus;
    n->depth++;
    n->kind[n->depth] = kind;
    n->arg[n->depth] = arg;
    n->root[n->depth] = root;
    SDL_strlcpy(n->rel[n->depth], rel, THC_PATH_MAX);
    n->focus[n->depth] = 0;
    a->ui.focus = 0;
    ui_scroll_reset(&a->ui, ui_idi("page", n->depth));
}

bool nav_pop(App *a, PortalNav *n)
{
    if (n->depth == 0) return false;
    n->depth--;
    if (n->focus[n->depth]) ui_focus(&a->ui, n->focus[n->depth]);
    return true;
}

FsNode *nav_node(App *a, const PortalNav *n)
{
    if (n->depth == 0) return NULL;
    FsNode *root = a->lib.root[n->root[n->depth]];
    return root ? fs_find(root, n->rel[n->depth]) : NULL;
}
