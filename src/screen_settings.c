/* screen_settings.c: placeholder until the settings slice. */
#include "app.h"

bool settings_capture_key(App *a, const SDL_KeyboardEvent *k) { (void)a; (void)k; return false; }

void screen_settings(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    float x = area.x + dp(ui, 40), y = area.y + dp(ui, 32), w = SDL_min(area.w - dp(ui, 80), dp(ui, 760));
    ui_text(ui, FONT_BOLD, TS_H1, x, y, ui->th.text, "Settings");
    y += ui_line_h(ui, TS_H1) + dp(ui, 24);
    float rh = dp(ui, 64);
    if (ui_button(ui, ui_id("s-music"), (SDL_FRect){ x, y, w, rh }, a->set.music_dir[0] ? a->set.music_dir : "Choose music folder", ICON_NOTE, false))
        app_pick_folder(a, 0, a->set.music_dir);
    y += rh + dp(ui, 12);
    if (ui_button(ui, ui_id("s-video"), (SDL_FRect){ x, y, w, rh }, a->set.video_dir[0] ? a->set.video_dir : "Choose video folder", ICON_FILM, false))
        app_pick_folder(a, 1, a->set.video_dir);
}
