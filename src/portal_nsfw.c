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
        for (int i = 0; i < SECTION_COUNT; i++) c += count_kind(a->nlib.root[ROOT_SECTION0 + i], false, false);
    } else if (sub == 1) {
        c += count_kind(a->nlib.root[ROOT_MUSIC], true, false);
    } else {
        c += count_kind(a->nlib.root[ROOT_BOOKS], false, true);
        c += count_kind(a->nlib.root[ROOT_BOOKS], true, false);
    }
    return c;
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
    /* sub-tabs */
    static const char *const names[] = { "Video", "Music", "Audiobooks", "Screenshots" };
    float pad = SDL_max(dp(ui, 32), (area.w - dp(ui, 1400)) / 2);
    float x = area.x + pad, y = area.y + dp(ui, 14);
    for (int i = 0; i < 4; i++) {
        char label[48];
        int n = i == 3 ? screenshots_nsfw_count(a) : items_in(a, i);
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
