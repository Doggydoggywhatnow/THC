/* screen_setup.c: first launch. Pick a music folder and a video folder, or
 * skip either; both can be changed later in Settings. */
#include "app.h"

static void folder_card(App *a, SDL_FRect r, int target, UiIcon icon, const char *title,
                        const char *layout, char *path, Color tint)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    ui_shadow(ui, r, dp(ui, 22), dp(ui, 30), t->shadow);
    ui_fill(ui, r, t->surface, dp(ui, 22));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 22), dp(ui, 2));
    else {
        ui_clip_push(ui, (SDL_FRect){ r.x, r.y, r.w, dp(ui, 5) });
        ui_fill_grad_h(ui, r, tint, t->accent2, dp(ui, 22));
        ui_clip_pop(ui);
    }

    float pad = dp(ui, 28);
    float x = r.x + pad, y = r.y + pad;
    float is = dp(ui, 56);
    ui_fill_grad(ui, (SDL_FRect){ x, y, is, is }, t->high_contrast ? t->accent : tint,
                 t->high_contrast ? t->accent : t->accent2, dp(ui, 16));
    ui_icon(ui, icon, (SDL_FRect){ x + is * 0.2f, y + is * 0.2f, is * 0.6f, is * 0.6f }, t->on_accent);
    float set_w = 0;
    if (path[0]) {
        const char *ok = "Ready";
        set_w = ui_text_w(ui, FONT_BOLD, TS_SMALL, ok) + dp(ui, 36);
        SDL_FRect chip = { r.x + r.w - pad - set_w, y + dp(ui, 14), set_w, dp(ui, 28) };
        ui_fill(ui, chip, with_alpha(t->watched, 40), chip.h / 2);
        ui_icon(ui, ICON_CHECK, (SDL_FRect){ chip.x + dp(ui, 8), chip.y + dp(ui, 6), dp(ui, 16), dp(ui, 16) }, t->watched);
        ui_text(ui, FONT_BOLD, TS_SMALL, chip.x + dp(ui, 28), chip.y + (chip.h - ui_line_h(ui, TS_SMALL)) / 2,
                t->watched, ok);
    }
    y += is + dp(ui, 18);
    ui_text(ui, FONT_BOLD, TS_TITLE, x, y, t->text, title);
    y += ui_line_h(ui, TS_TITLE) + dp(ui, 4);
    y += ui_text_wrap(ui, FONT_REGULAR, TS_BODY, x, y, r.w - 2 * pad, t->text_dim, layout, 3);
    y += dp(ui, 16);

    float fh = dp(ui, 46);
    SDL_FRect field = { x, y, r.w - 2 * pad, fh };
    ui_textfield(ui, ui_idi("setup-path", target), field, path, THC_PATH_MAX, "Type a folder path, or browse");
    y += fh + dp(ui, 14);

    float bw = (r.w - 2 * pad - dp(ui, 12)) / 2;
    if (ui_button(ui, ui_idi("setup-browse", target), (SDL_FRect){ x, y, bw, dp(ui, 44) }, "Browse",
                  ICON_FOLDER, false))
        app_pick_folder(a, target, path);
    if (ui_button(ui, ui_idi("setup-skip", target), (SDL_FRect){ x + bw + dp(ui, 12), y, bw, dp(ui, 44) },
                  path[0] ? "Clear" : "Skip", ICON_NONE, false)) {
        path[0] = 0;
        app_toast(a, "%s skipped. You can add it later in Settings.", title);
    }
}

void screen_setup(App *a)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    SDL_FRect view = { 0, 0, (float)ui->w, (float)ui->h };
    float off = ui_scroll_begin(ui, ui_id("setup-scroll"), view, false);

    float maxw = dp(ui, 980);
    float w = SDL_min(maxw, ui->w - dp(ui, 48));
    float x = (ui->w - w) / 2;
    float y = dp(ui, 56) - off;

    /* hero */
    const char *logo = "THC";
    float lw = ui_text_w(ui, FONT_BOLD, TS_HERO * 1.6f, logo);
    ui_text(ui, FONT_BOLD, TS_HERO * 1.6f, (ui->w - lw) / 2, y, t->text, logo);
    y += ui_line_h(ui, TS_HERO * 1.6f);
    float barw = dp(ui, 96);
    ui_fill_grad_h(ui, (SDL_FRect){ (ui->w - barw) / 2, y, barw, dp(ui, 6) }, t->accent, t->accent2, dp(ui, 3));
    y += dp(ui, 28);
    const char *tag = "Your music and videos, on your device.";
    float tw = ui_text_w(ui, FONT_BOLD, TS_H2, tag);
    if (tw < w) ui_text(ui, FONT_BOLD, TS_H2, (ui->w - tw) / 2, y, t->text, tag);
    else ui_text_wrap(ui, FONT_BOLD, TS_H2, x, y, w, t->text, tag, 2);
    y += ui_line_h(ui, TS_H2) * (tw < w ? 1 : 2) + dp(ui, 8);
    const char *sub = "Choose where your media lives, or skip and add it later. Each folder gets its own portal.";
    float sw = ui_text_w(ui, FONT_REGULAR, TS_LABEL, sub);
    if (sw < w) {
        ui_text(ui, FONT_REGULAR, TS_LABEL, (ui->w - sw) / 2, y, t->text_dim, sub);
        y += ui_line_h(ui, TS_LABEL);
    } else {
        y += ui_text_wrap(ui, FONT_REGULAR, TS_LABEL, x, y, w, t->text_dim, sub, 3);
    }
    y += dp(ui, 40);

    /* the two folder cards, side by side when there is room */
    bool row = w > dp(ui, 700);
    float gap = dp(ui, 24);
    float cw = row ? (w - gap) / 2 : w;
    float ch = dp(ui, 312);
    Theme music = theme_make(PORTAL_MUSIC, t->dark, t->high_contrast, a->set.accent);
    Theme video = theme_make(PORTAL_VIDEO, t->dark, t->high_contrast, a->set.accent);
    folder_card(a, (SDL_FRect){ x, y, cw, ch }, 0, ICON_NOTE, "Music folder",
                "Organized as Artist, then Album, then Track. Loose songs work too.", a->set.music_dir,
                music.accent);
    if (row) x += cw + gap;
    else y += ch + gap;
    folder_card(a, (SDL_FRect){ x, y, cw, ch }, 1, ICON_FILM, "Video folder",
                "Shows as Show, then Season, then Episode. Movies as one file or one folder each.",
                a->set.video_dir, video.accent);
    x = (ui->w - w) / 2;
    y += ch + dp(ui, 36);

    float bw = dp(ui, 260);
    bool any = a->set.music_dir[0] || a->set.video_dir[0];
    if (ui_button(ui, ui_id("setup-go"), (SDL_FRect){ (ui->w - bw) / 2, y, bw, dp(ui, 54) },
                  any ? "Start browsing" : "Skip for now", ICON_PLAY, true)) {
        a->set.setup_done = true;
        app_save_settings(a);
        app_rescan(a);
        app_go(a, a->set.video_dir[0] || !a->set.music_dir[0] ? SCREEN_VIDEO : SCREEN_MUSIC);
    }
    y += dp(ui, 54) + dp(ui, 20);
    const char *note = "THC only reads these folders. It never moves, renames or changes your files.";
    float nw = ui_text_w(ui, FONT_REGULAR, TS_SMALL, note);
    float ix = (ui->w - nw - dp(ui, 22)) / 2;
    ui_icon(ui, ICON_SHIELD, (SDL_FRect){ ix, y, dp(ui, 16), dp(ui, 16) }, t->text_dim);
    ui_text(ui, FONT_REGULAR, TS_SMALL, ix + dp(ui, 22), y, t->text_dim, note);
    y += dp(ui, 60);

    ui_scroll_end(ui, y + off);
    if (!ui->focus) ui_focus(ui, ui_idi("setup-browse", 0));
}
