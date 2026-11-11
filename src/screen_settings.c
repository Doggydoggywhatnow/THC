/* screen_settings.c: everything the user can change, grouped by topic. */
#include "app.h"
#include "hotkeys.h"

#include <string.h>

enum { CAT_LIBRARY = 0, CAT_PLAYBACK, CAT_APPEARANCE, CAT_ACCESS, CAT_HOTKEYS, CAT_MPV, CAT_PRIVACY, CAT_COUNT };
static const char *const k_cat_names[CAT_COUNT] = { "Library", "Playback", "Appearance", "Accessibility",
                                                    "Hotkeys", "MPV", "Privacy" };

static int g_cat;
static int g_capture = -1;        /* action waiting for a key press */
static double g_clear_armed;      /* "clear history" needs a second press */
static char g_font_field[THC_PATH_MAX];
static char g_mpv_field[THC_PATH_MAX];
static bool g_fields_init;
static bool g_jump_settings;      /* scroll down to the settings after this frame */

bool settings_capture_key(App *a, const SDL_KeyboardEvent *k)
{
    if (g_capture < 0) return false;
    if (k->key == SDLK_ESCAPE) {
        g_capture = -1;
        return true;
    }
    char spec[32];
    if (!hotkey_from_event(k->key, k->mod, spec, sizeof spec)) return true; /* lone modifier: keep waiting */
    SDL_strlcpy(a->set.hotkey[g_capture], spec, sizeof a->set.hotkey[0]);
    /* a key can only do one thing: clear it from any other action */
    for (int i = 0; i < ACT_COUNT; i++)
        if (i != g_capture && !SDL_strcasecmp(a->set.hotkey[i], spec)) {
            app_toast(a, "%s was also used for \"%s\"; that one is now unset", spec, k_actions[i].label);
            a->set.hotkey[i][0] = 0;
        }
    g_capture = -1;
    app_save_settings(a);
    return true;
}

/* ------------------------------------------------------------------------ */

typedef struct Layout {
    float x, y, w;
    float row;   /* row height */
    float gap;
} Layout;

static void group(App *a, Layout *L, const char *title, const char *hint)
{
    Ui *ui = &a->ui;
    L->y += dp(ui, 12);
    ui_text(ui, FONT_BOLD, TS_TITLE, L->x, L->y, ui->th.text, title);
    L->y += ui_line_h(ui, TS_TITLE);
    if (hint) {
        L->y += ui_text_wrap(ui, FONT_REGULAR, TS_BODY, L->x, L->y, L->w, ui->th.text_dim, hint, 4);
    }
    L->y += dp(ui, 12);
}

static SDL_FRect next_row(App *a, Layout *L, float h)
{
    SDL_FRect r = { L->x, L->y, L->w, h > 0 ? h : L->row };
    L->y += r.h + L->gap;
    (void)a;
    return r;
}

static void note(App *a, Layout *L, UiIcon icon, const char *text)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float pad = dp(ui, 16);
    float tw = L->w - 2 * pad - dp(ui, 30);
    /* measure: draw off-screen is not possible, so estimate by wrapping */
    float lh = ui_line_h(ui, TS_BODY);
    int lines = (int)(ui_text_w(ui, FONT_REGULAR, TS_BODY, text) / tw) + 1;
    float h = lines * lh + 2 * pad;
    SDL_FRect r = { L->x, L->y, L->w, h };
    ui_fill(ui, r, with_alpha(t->accent, t->high_contrast ? 0 : 26), dp(ui, 12));
    ui_stroke(ui, r, with_alpha(t->accent, 120), dp(ui, 12), dp(ui, 1));
    ui_icon(ui, icon, (SDL_FRect){ r.x + pad, r.y + pad, dp(ui, 20), dp(ui, 20) }, t->accent);
    ui_text_wrap(ui, FONT_REGULAR, TS_BODY, r.x + pad + dp(ui, 30), r.y + pad, tw, t->text, text, 8);
    L->y += h + L->gap;
}

/* A feature that is planned but not in this version: shown, not switchable. */
static void planned_row(App *a, Layout *L, const char *label, const char *hint)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    SDL_FRect r = next_row(a, L, dp(ui, 72));
    ui_fill(ui, r, with_alpha(t->surface, 160), dp(ui, 12));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    float pad = dp(ui, 16);
    float l1 = ui_line_h(ui, TS_LABEL), l2 = ui_line_h(ui, TS_SMALL);
    float y = r.y + (r.h - l1 - l2) / 2;
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, r.x + pad, y, r.w - dp(ui, 160), t->text_dim, label);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + pad, y + l1, r.w - dp(ui, 160), t->text_dim, hint);
    const char *chip = "Planned";
    float cw = ui_text_w(ui, FONT_BOLD, TS_SMALL, chip) + dp(ui, 24);
    SDL_FRect c = { r.x + r.w - pad - cw, r.y + (r.h - dp(ui, 26)) / 2, cw, dp(ui, 26) };
    ui_stroke(ui, c, t->text_dim, c.h / 2, dp(ui, 1));
    ui_text_center(ui, FONT_BOLD, TS_SMALL, c, t->text_dim, chip, true);
}

static void open_folder(App *a, const char *path)
{
    char url[THC_PATH_MAX + 16];
#ifdef _WIN32
    SDL_snprintf(url, sizeof url, "file:///%s", path);
    for (char *p = url; *p; p++) if (*p == '\\') *p = '/';
#else
    SDL_snprintf(url, sizeof url, "file://%s", path);
#endif
    if (!SDL_OpenURL(url)) app_toast(a, "Could not open %s", path);
}

/* A folder setting: name, path, Change and Remove. */
static void folder_row(App *a, Layout *L, int target, const char *label, char *path, UiIcon icon)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    SDL_FRect r = next_row(a, L, dp(ui, 72));
    ui_fill(ui, r, t->surface, dp(ui, 12));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    float is = dp(ui, 22);
    ui_icon(ui, icon, (SDL_FRect){ r.x + dp(ui, 18), r.y + (r.h - is) / 2, is, is }, t->accent);
    float bw = dp(ui, 104), bh = dp(ui, 40);
    float tx = r.x + dp(ui, 56), tw = r.w - dp(ui, 56) - bw * 2 - dp(ui, 40);
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, tx, r.y + dp(ui, 12), tw, t->text, label);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, tx, r.y + dp(ui, 14) + ui_line_h(ui, TS_LABEL), tw, t->text_dim,
                path[0] ? path : "Not set");
    float bx = r.x + r.w - dp(ui, 14) - bw;
    if (path[0] && ui_button(ui, ui_idi("folder-rm", target), (SDL_FRect){ bx, r.y + (r.h - bh) / 2, bw, bh }, "Remove",
                             ICON_NONE, false)) {
        path[0] = 0;
        app_save_settings(a);
        app_rescan(a);
    }
    bx -= bw + dp(ui, 10);
    if (ui_button(ui, ui_idi("folder-ch", target), (SDL_FRect){ bx, r.y + (r.h - bh) / 2, bw, bh },
                  path[0] ? "Change" : "Choose", ICON_NONE, !path[0]))
        app_pick_folder(a, target, path);
}

static void page_library(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    group(a, L, "Main folders", "THC reads these folders and never changes, moves or renames anything in them.");
    folder_row(a, L, 1, "Video", a->set.video_dir, ICON_FILM);
    folder_row(a, L, 0, "Music", a->set.music_dir, ICON_NOTE);
    folder_row(a, L, 2, "Audiobooks", a->set.books_dir, ICON_BOOK);
    group(a, L, "Sections",
          "Optional extra folders for each section. Folders named Anime, TV, Cartoons or Movies inside your "
          "video folder are picked up automatically.");
    static const UiIcon icons[SECTION_COUNT] = { ICON_STAR, ICON_TV, ICON_SPARK, ICON_FILM };
    for (int i = 0; i < SECTION_COUNT; i++)
        folder_row(a, L, 3 + i, k_section_names[i], a->set.section_dir[i], icons[i]);
    group(a, L, "Presentation", NULL);
    int mode = a->set.sort_mode;
    if (ui_segmented(ui, ui_id("set-sort"), next_row(a, L, 0), "Sort shows and movies", k_sort_names, SORT_COUNT, &mode)) {
        a->set.sort_mode = mode;
        app_save_settings(a);
    }
    if (a->set.noverrides > 0) {
        char lbl[96];
        SDL_snprintf(lbl, sizeof lbl, "%d show%s moved between sections", a->set.noverrides, a->set.noverrides == 1 ? "" : "s");
        SDL_FRect r = next_row(a, L, 0);
        ui_fill(ui, r, ui->th.surface, dp(ui, 12));
        ui_text_center(ui, FONT_BOLD, TS_LABEL, (SDL_FRect){ r.x + dp(ui, 16), r.y, r.w - dp(ui, 200), r.h }, ui->th.text, lbl, false);
        if (ui_button(ui, ui_id("set-ovr-reset"), (SDL_FRect){ r.x + r.w - dp(ui, 170), r.y + dp(ui, 10), dp(ui, 156), r.h - dp(ui, 20) },
                      "Reset", ICON_NONE, false)) {
            a->set.noverrides = 0;
            app_save_settings(a);
            app_rebuild_library(a);
        }
    }
    group(a, L, "Thumbnails",
          "Video thumbnails come from a picture next to the video, or a frame THC grabs from the video itself. "
          "Frames are cached in THC's own folder, never next to your media.");
    if (ui_toggle(ui, ui_id("set-thumbs"), next_row(a, L, dp(ui, 72)), "Make video thumbnails",
                  a->have_mpv ? "Uses a little CPU and disk while you browse" : "Needs libmpv, which was not found",
                  &a->set.video_thumbs))
        app_save_settings(a);
    if (ui_button(ui, ui_id("set-thumbs-clear"), (SDL_FRect){ L->x, L->y, dp(ui, 280), dp(ui, 46) },
                  "Clear thumbnail cache", ICON_CLOSE, false)) {
        thumbs_clear(a);
        app_toast(a, "Thumbnail cache cleared");
    }
    L->y += dp(ui, 46) + L->gap;
    group(a, L, "Library", NULL);
    if (ui_button(ui, ui_id("set-rescan"), (SDL_FRect){ L->x, L->y, dp(ui, 200), dp(ui, 46) }, "Rescan now", ICON_SORT, false)) {
        app_rescan(a);
        app_toast(a, "Rescanning your library");
    }
    L->y += dp(ui, 46) + L->gap;
}

static void page_playback(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    group(a, L, "Watching", "A video counts as watched once you pass this point, so skipping the credits still counts.");
    float thr = (float)(a->set.watched_threshold * 100);
    if (ui_slider(ui, ui_id("set-thr"), next_row(a, L, 0), "Watched threshold", &thr, 50, 100, 1, "%.0f%%")) {
        a->set.watched_threshold = thr / 100.0;
        app_save_settings(a);
    }
    group(a, L, "Listening", NULL);
    float vol = (float)a->set.volume;
    if (ui_slider(ui, ui_id("set-vol"), next_row(a, L, 0), "Volume", &vol, 0, 100, 5, "%.0f%%")) {
        a->set.volume = (int)vol;
        app_save_settings(a);
    }
    float skip = (float)a->set.skip_seconds;
    if (ui_slider(ui, ui_id("set-skip"), next_row(a, L, 0), "Audiobook skip", &skip, 5, 120, 5, "%.0f s")) {
        a->set.skip_seconds = (int)skip;
        app_save_settings(a);
    }
    float spd = (float)a->set.book_speed;
    if (ui_slider(ui, ui_id("set-speed"), next_row(a, L, 0), "Audiobook speed", &spd, 0.5f, 3.0f, 0.05f, "%.2fx")) {
        a->set.book_speed = spd;
        app_save_settings(a);
    }
    note(a, L, ICON_SPARK, "Audio effect plugins are planned for a later release.");
}

static void page_appearance(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    group(a, L, "Theme", NULL);
    static const char *const themes[] = { "System", "Dark", "Light" };
    int th = a->set.theme;
    if (ui_segmented(ui, ui_id("set-theme"), next_row(a, L, 0), "Light and dark mode", themes, 3, &th)) {
        a->set.theme = th;
        app_apply_appearance(a);
        app_save_settings(a);
    }
    /* accent swatches */
    {
        SDL_FRect r = next_row(a, L, dp(ui, 72));
        const Theme *t = &ui->th;
        ui_fill(ui, r, t->surface, dp(ui, 12));
        if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
        ui_text_center(ui, FONT_BOLD, TS_LABEL, (SDL_FRect){ r.x + dp(ui, 16), r.y, dp(ui, 140), r.h }, t->text, "Accent", false);
        float s = dp(ui, 40), x = r.x + r.w - dp(ui, 16) - ACCENT_PRESET_COUNT * (s + dp(ui, 10));
        for (int i = 0; i < ACCENT_PRESET_COUNT; i++) {
            SDL_FRect sw = { x + i * (s + dp(ui, 10)), r.y + (r.h - s) / 2, s, s };
            Uint32 id = ui_idi("accent", i);
            UiItemState st = ui_item(ui, id, sw);
            Theme pv = theme_make(i == 0 ? PORTAL_VIDEO : PORTAL_VIDEO, a->dark, false, i);
            if (i == 0) {
                Theme m = theme_make(PORTAL_MUSIC, a->dark, false, 0);
                ui_fill_grad_h(ui, sw, pv.accent, m.accent, s / 2);
            } else {
                ui_fill_grad_h(ui, sw, pv.accent, pv.accent2, s / 2);
            }
            if (a->set.accent == i) ui_stroke(ui, (SDL_FRect){ sw.x - dp(ui, 4), sw.y - dp(ui, 4), s + dp(ui, 8), s + dp(ui, 8) },
                                              t->text, s / 2 + dp(ui, 4), dp(ui, 2));
            if (st.focused) ui_focus_ring(ui, sw, s / 2);
            if (st.hover || st.focused) {
                float tw = ui_text_w(ui, FONT_REGULAR, TS_SMALL, k_accent_names[i]);
                ui_text(ui, FONT_REGULAR, TS_SMALL, SDL_min(sw.x + s / 2 - tw / 2, r.x + r.w - tw - dp(ui, 8)),
                        r.y + r.h - ui_line_h(ui, TS_SMALL) + dp(ui, 2), t->text_dim, k_accent_names[i]);
            }
            if (st.activated) {
                a->set.accent = i;
                app_save_settings(a);
            }
        }
    }
    group(a, L, "Size", "Text size and the size of everything else can be changed separately.");
    float ts = a->set.text_scale * 100;
    if (ui_slider(ui, ui_id("set-text"), next_row(a, L, 0), "Text size", &ts, 75, 200, 5, "%.0f%%")) {
        a->set.text_scale = ts / 100;
        app_apply_appearance(a);
        app_save_settings(a);
    }
    float us = a->set.ui_scale * 100;
    if (ui_slider(ui, ui_id("set-uiscale"), next_row(a, L, 0), "Interface scale", &us, 75, 200, 5, "%.0f%%")) {
        a->set.ui_scale = us / 100;
        app_apply_appearance(a);
        app_save_settings(a);
    }
    group(a, L, "Font",
          "THC uses Atkinson Hyperlegible, a typeface made for low-vision readers. You can use any TrueType or "
          "OpenType font file instead.");
    if (!g_fields_init) {
        SDL_strlcpy(g_font_field, a->set.font_path, sizeof g_font_field);
        SDL_strlcpy(g_mpv_field, a->set.libmpv_path, sizeof g_mpv_field);
        g_fields_init = true;
    }
    SDL_FRect r = next_row(a, L, dp(ui, 48));
    float bw = dp(ui, 110);
    bool enter = ui_textfield(ui, ui_id("set-font"), (SDL_FRect){ r.x, r.y, r.w - 2 * bw - dp(ui, 20), r.h }, g_font_field,
                              sizeof g_font_field, "Path to a .ttf or .otf file (empty = Atkinson Hyperlegible)");
    if (ui_button(ui, ui_id("set-font-apply"), (SDL_FRect){ r.x + r.w - 2 * bw - dp(ui, 10), r.y, bw, r.h }, "Apply", ICON_NONE, true) || enter) {
        if (ui_set_font(ui, g_font_field)) {
            SDL_strlcpy(a->set.font_path, g_font_field, sizeof a->set.font_path);
            app_save_settings(a);
            app_toast(a, g_font_field[0] ? "Font changed" : "Using Atkinson Hyperlegible");
        } else {
            app_toast(a, "That font could not be loaded");
        }
    }
    if (ui_button(ui, ui_id("set-font-reset"), (SDL_FRect){ r.x + r.w - bw, r.y, bw, r.h }, "Default", ICON_NONE, false)) {
        g_font_field[0] = 0;
        ui_set_font(ui, NULL);
        a->set.font_path[0] = 0;
        app_save_settings(a);
    }
}

static void page_access(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    group(a, L, "Seeing", NULL);
    if (ui_toggle(ui, ui_id("set-hc"), next_row(a, L, dp(ui, 72)), "High contrast",
                  "Black and white with strong outlines and a bright focus ring", &a->set.high_contrast))
        app_save_settings(a);
    float ts = a->set.text_scale * 100;
    if (ui_slider(ui, ui_id("set-text2"), next_row(a, L, 0), "Text size", &ts, 75, 200, 5, "%.0f%%")) {
        a->set.text_scale = ts / 100;
        app_apply_appearance(a);
        app_save_settings(a);
    }
    group(a, L, "Motion", NULL);
    if (ui_toggle(ui, ui_id("set-rm"), next_row(a, L, dp(ui, 72)), "Reduce motion",
                  "Turn off animations, hover lifts and smooth scrolling", &a->set.reduced_motion)) {
        app_apply_appearance(a);
        app_save_settings(a);
    }
    group(a, L, "Keyboard", NULL);
    note(a, L, ICON_KEYBOARD,
         "Everything in THC works from the keyboard. Arrow keys move between items, Tab and Shift+Tab step "
         "through them, Enter opens or plays, and Escape or Backspace goes back. Shortcuts can be changed under "
         "Hotkeys.");
    group(a, L, "Screen readers", NULL);
    planned_row(a, L, "Screen reader support", "Coming in a later release");
    group(a, L, "Subtitles", NULL);
    note(a, L, ICON_INFO,
         "Subtitle font, size, color and background are set through MPV (sub-font, sub-font-size, sub-color, "
         "sub-back-color in mpv.conf). See the MPV page for THC's own MPV folder.");
}

static void page_hotkeys(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    group(a, L, "Shortcuts", "Choose a shortcut, then press the new key. Escape cancels.");
    for (int i = 0; i < ACT_COUNT; i++) {
        SDL_FRect r = next_row(a, L, dp(ui, 56));
        Uint32 id = ui_idi("hk", i);
        UiItemState st = ui_item(ui, id, r);
        ui_fill(ui, r, st.hover ? t->surface_hi : t->surface, dp(ui, 12));
        if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
        if (st.focused) ui_focus_ring(ui, r, dp(ui, 12));
        ui_text_center(ui, FONT_BOLD, TS_BODY, (SDL_FRect){ r.x + dp(ui, 16), r.y, r.w * 0.6f, r.h }, t->text,
                       k_actions[i].label, false);
        const char *key = g_capture == i ? "Press a key" : (a->set.hotkey[i][0] ? a->set.hotkey[i] : "Not set");
        float kw = ui_text_w(ui, FONT_BOLD, TS_BODY, key) + dp(ui, 28);
        SDL_FRect chip = { r.x + r.w - kw - dp(ui, 14), r.y + (r.h - dp(ui, 34)) / 2, kw, dp(ui, 34) };
        if (g_capture == i) ui_fill(ui, chip, t->accent, chip.h / 2);
        else ui_stroke(ui, chip, t->text_dim, dp(ui, 8), dp(ui, 1));
        ui_text_center(ui, FONT_BOLD, TS_BODY, chip, g_capture == i ? t->on_accent : t->text, key, true);
        if (st.activated) g_capture = g_capture == i ? -1 : i;
    }
    if (ui_button(ui, ui_id("hk-reset"), (SDL_FRect){ L->x, L->y, dp(ui, 220), dp(ui, 46) }, "Reset to defaults", ICON_NONE, false)) {
        for (int i = 0; i < ACT_COUNT; i++) SDL_strlcpy(a->set.hotkey[i], k_actions[i].def, sizeof a->set.hotkey[i]);
        app_save_settings(a);
    }
    L->y += dp(ui, 46) + L->gap;
    note(a, L, ICON_FILM, "Keys inside the video window come from MPV. Change them in input.conf in THC's MPV folder.");
}

static void write_template(App *a, const char *name, const char *body)
{
    char path[THC_PATH_MAX];
    SDL_snprintf(path, sizeof path, "%s/%s", a->mpv_dir, name);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info)) {
        app_toast(a, "%s already exists", name);
        return;
    }
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        app_toast(a, "Could not create %s", name);
        return;
    }
    SDL_WriteIO(io, body, strlen(body));
    SDL_CloseIO(io);
    app_toast(a, "Created %s", name);
}

static void page_mpv(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    group(a, L, "THC's own MPV",
          "THC plays video through MPV with its own configuration folder. It never reads or changes your "
          "standalone MPV setup, and your standalone MPV never sees THC's. Every MPV option works here: "
          "mpv.conf, input.conf, scripts, shaders and profiles.");
    SDL_FRect r = next_row(a, L, dp(ui, 72));
    ui_fill(ui, r, t->surface, dp(ui, 12));
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, r.x + dp(ui, 16), r.y + dp(ui, 12), r.w - dp(ui, 200), t->text, "Configuration folder");
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + dp(ui, 16), r.y + dp(ui, 14) + ui_line_h(ui, TS_LABEL), r.w - dp(ui, 200),
                t->text_dim, a->mpv_dir);
    if (ui_button(ui, ui_id("mpv-open"), (SDL_FRect){ r.x + r.w - dp(ui, 164), r.y + dp(ui, 16), dp(ui, 150), dp(ui, 40) },
                  "Open folder", ICON_FOLDER, false))
        open_folder(a, a->mpv_dir);
    note(a, L, ICON_INFO,
         "THC's watch history is the only record of where you stopped, so THC controls resume-playback, "
         "save-position-on-quit and watch-later-directory. Setting them in mpv.conf has no effect.");
    float bw = dp(ui, 210);
    if (ui_button(ui, ui_id("mpv-conf"), (SDL_FRect){ L->x, L->y, bw, dp(ui, 46) }, "Create mpv.conf", ICON_PLUS, false))
        write_template(a, "mpv.conf",
                       "# THC's own mpv.conf. Your standalone MPV does not read this file.\n"
                       "# Any MPV option works here, for example:\n"
                       "# profile=gpu-hq\n"
                       "# hwdec=auto-safe\n"
                       "# sub-font-size=46\n"
                       "# sub-color=\"#FFFFFF\"\n"
                       "# sub-back-color=\"#C0000000\"\n"
                       "# save-position-on-quit=no   # THC already remembers your place\n");
    if (ui_button(ui, ui_id("mpv-input"), (SDL_FRect){ L->x + bw + dp(ui, 12), L->y, bw, dp(ui, 46) }, "Create input.conf",
                  ICON_PLUS, false))
        write_template(a, "input.conf",
                       "# THC's own input.conf: key bindings inside the video window.\n"
                       "# Examples:\n"
                       "# RIGHT seek 10\n"
                       "# LEFT seek -10\n");
    L->y += dp(ui, 46) + L->gap * 2;
    note(a, L, ICON_INFO, "To start from your existing MPV setup, copy its files into this folder. Scripts go in "
                          "a scripts folder and shaders in a shaders folder, as with MPV itself.");

    group(a, L, "libmpv", NULL);
    SDL_FRect s = next_row(a, L, dp(ui, 72));
    ui_fill(ui, s, t->surface, dp(ui, 12));
    char st[THC_PATH_MAX + 32];
    if (a->have_mpv) SDL_snprintf(st, sizeof st, "Loaded from %s", a->mpv.path);
    else SDL_snprintf(st, sizeof st, "Not found. Install MPV, or point THC at libmpv below.");
    ui_icon(ui, a->have_mpv ? ICON_CHECK : ICON_CLOSE, (SDL_FRect){ s.x + dp(ui, 16), s.y + dp(ui, 24), dp(ui, 24), dp(ui, 24) },
            a->have_mpv ? t->watched : t->accent);
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, s.x + dp(ui, 52), s.y + dp(ui, 12), s.w - dp(ui, 70), t->text,
                a->have_mpv ? "Video playback ready" : "Video playback unavailable");
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, s.x + dp(ui, 52), s.y + dp(ui, 14) + ui_line_h(ui, TS_LABEL), s.w - dp(ui, 70),
                t->text_dim, st);
    SDL_FRect f = next_row(a, L, dp(ui, 48));
    bool enter = ui_textfield(ui, ui_id("set-libmpv"), (SDL_FRect){ f.x, f.y, f.w - dp(ui, 130), f.h }, g_mpv_field,
                              sizeof g_mpv_field, "Custom libmpv path (optional)");
    if (ui_button(ui, ui_id("set-libmpv-apply"), (SDL_FRect){ f.x + f.w - dp(ui, 120), f.y, dp(ui, 120), f.h }, "Load", ICON_NONE,
                  true) || enter) {
        SDL_strlcpy(a->set.libmpv_path, g_mpv_field, sizeof a->set.libmpv_path);
        app_save_settings(a);
        if (!a->video.h) {
            mpv_dyn_unload(&a->mpv);
            a->have_mpv = mpv_dyn_load(&a->mpv, a->set.libmpv_path);
            app_toast(a, a->have_mpv ? "libmpv loaded" : "libmpv could not be loaded from there");
        } else {
            app_toast(a, "Saved. It will be used after the current video.");
        }
    }
}

static void page_privacy(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    note(a, L, ICON_SHIELD,
         "Everything stays on this device. THC makes no network requests, has no accounts and sends no "
         "telemetry. What you watch is your business.");
    group(a, L, "Watch history", NULL);
    if (ui_toggle(ui, ui_id("set-hist"), next_row(a, L, dp(ui, 72)), "Remember where I stopped",
                  "Resume positions, playback bars and checkmarks", &a->set.remember_history))
        app_save_settings(a);
    bool armed = ui->time < g_clear_armed;
    if (ui_button(ui, ui_id("set-clear"), (SDL_FRect){ L->x, L->y, dp(ui, 280), dp(ui, 46) },
                  armed ? "Press again to clear" : "Clear watch history", ICON_CLOSE, armed)) {
        if (armed) {
            history_clear(a->hist);
            video_clear_watch_later(a->mpv_dir);
            app_history_changed();
            g_clear_armed = 0;
            app_toast(a, "Watch history cleared");
        } else {
            g_clear_armed = ui->time + 4;
        }
    }
    if (armed) ui->needs_redraw = true;
    L->y += dp(ui, 46) + L->gap;
    group(a, L, "Your data", "Settings (settings.ini), the history database (history.db) and THC's MPV folder live here. "
                             "You can inspect or delete them at any time.");
    SDL_FRect r = next_row(a, L, dp(ui, 64));
    ui_fill(ui, r, t->surface, dp(ui, 12));
    ui_text_center(ui, FONT_REGULAR, TS_BODY, (SDL_FRect){ r.x + dp(ui, 16), r.y, r.w - dp(ui, 200), r.h }, t->text, a->data_dir, false);
    if (ui_button(ui, ui_id("data-open"), (SDL_FRect){ r.x + r.w - dp(ui, 164), r.y + dp(ui, 12), dp(ui, 150), dp(ui, 40) },
                  "Open folder", ICON_FOLDER, false))
        open_folder(a, a->data_dir);
    group(a, L, "Episode descriptions", NULL);
    planned_row(a, L, "Fetch episode and season descriptions",
                "Would be strictly opt-in and off by default. Not in this version.");
}

#define THC_REPO_URL "https://github.com/Doggydoggywhatnow/THC"

/* The About block at the top of the page: logo, version, what THC is, links
 * and credits. Settings start right below it. */
static void about_block(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float lw = ui_text(ui, FONT_BOLD, TS_HERO, L->x, L->y, t->text, "THC");
    float lh = ui_line_h(ui, TS_HERO);
    ui_circle(ui, L->x + lw + dp(ui, 8), L->y + lh * 0.78f, dp(ui, 6), t->accent);
    /* the version is a small addition next to the logo */
    char ver[32];
    SDL_snprintf(ver, sizeof ver, "v%s", THC_VERSION);
    float vw = ui_text_w(ui, FONT_BOLD, TS_SMALL, ver) + dp(ui, 20);
    SDL_FRect vr = { L->x + lw + dp(ui, 28), L->y + lh * 0.55f, vw, ui_line_h(ui, TS_SMALL) + dp(ui, 8) };
    ui_fill(ui, vr, t->surface, vr.h / 2);
    ui_text_center(ui, FONT_BOLD, TS_SMALL, vr, t->text_dim, ver, true);
    L->y += lh + dp(ui, 8);
    L->y += ui_text_wrap(ui, FONT_REGULAR, TS_LABEL, L->x, L->y, L->w, t->text,
                         "A lightweight multimedia player for the music and videos you already have. Visually "
                         "pleasant, useful, accessible and private. Everything stays on your device.", 4);
    L->y += dp(ui, 16);

    SDL_FRect br = { L->x, L->y, dp(ui, 220), dp(ui, 44) };
    if (ui_button(ui, ui_id("about-repo"), br, "Project page", ICON_INFO, false)) SDL_OpenURL(THC_REPO_URL);
    SDL_FRect bs = { L->x + br.w + dp(ui, 12), L->y, dp(ui, 260), dp(ui, 44) };
    if (ui_button(ui, ui_id("about-jump"), bs, "Jump to settings", ICON_GEAR, true)) g_jump_settings = true;
    L->y += br.h + dp(ui, 12);

    group(a, L, "Built with", NULL);
    static const char *const parts[][2] = {
        { "MPV (libmpv)", "Video and some audio formats. Loaded at run time; GPL or LGPL." },
        { "lib-Peep 0.5.0", "WAV and FLAC in assembly, and the codec interface for MP3 and OGG. Unlicense." },
        { "dr_mp3", "MP3 decoding. Public domain or MIT-0." },
        { "stb_vorbis, stb_truetype, stb_image", "OGG, text and cover images. Public domain or MIT." },
        { "SDL3", "Window, input and audio output. zlib." },
        { "SQLite", "Watch history. Public domain." },
        { "Atkinson Hyperlegible", "Interface font by the Braille Institute. SIL OFL 1.1." },
    };
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
        SDL_FRect r = next_row(a, L, dp(ui, 60));
        ui_fill(ui, r, t->surface, dp(ui, 12));
        ui_text_fit(ui, FONT_BOLD, TS_BODY, r.x + dp(ui, 16), r.y + dp(ui, 10), r.w - dp(ui, 32), t->text, parts[i][0]);
        ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + dp(ui, 16), r.y + dp(ui, 12) + ui_line_h(ui, TS_BODY), r.w - dp(ui, 32),
                    t->text_dim, parts[i][1]);
    }
    note(a, L, ICON_INFO, "THC's own code is released into the public domain under the Unlicense.");
#ifndef THC_HAVE_PEEP
    note(a, L, ICON_INFO, "This build has no lib-Peep for its CPU: FLAC, WAV, MP3 and OGG play through MPV.");
#endif
}

void screen_settings(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float pad = SDL_max(dp(ui, 32), (area.w - dp(ui, 900)) / 2);
    Uint32 page_id = ui_id("settings-page");

    float off = ui_scroll_begin(ui, page_id, area, false);
    Layout L;
    L.x = area.x + pad;
    L.w = SDL_min(area.w - 2 * pad, dp(ui, 820));
    L.y = area.y + dp(ui, 28) - off;
    L.row = dp(ui, 60);
    L.gap = dp(ui, 10);

    about_block(a, &L);

    /* settings begin here, below the About information */
    L.y += dp(ui, 28);
    float settings_top = L.y + off - area.y; /* in content coordinates */
    ui_fill(ui, (SDL_FRect){ L.x, L.y, L.w, 1 }, with_alpha(t->border, 160), 0);
    L.y += dp(ui, 28);
    ui_text(ui, FONT_BOLD, TS_H1, L.x, L.y, t->text, "Settings");
    L.y += ui_line_h(ui, TS_H1) + dp(ui, 16);
    float px = L.x;
    for (int i = 0; i < CAT_COUNT; i++) {
        float pw = ui_text_w(ui, FONT_BOLD, TS_BODY, k_cat_names[i]) + dp(ui, 32);
        if (px + pw > L.x + L.w) { px = L.x; L.y += dp(ui, 46); }
        if (ui_pill(ui, ui_idi("catp", i), (SDL_FRect){ px, L.y, pw, dp(ui, 38) }, k_cat_names[i], g_cat == i)) {
            g_cat = i;
            g_capture = -1;
        }
        px += pw + dp(ui, 8);
    }
    L.y += dp(ui, 60);
    if (g_jump_settings) {
        g_jump_settings = false;
        ui_scroll_to(ui, page_id, settings_top - dp(ui, 12));
    }

    switch (g_cat) {
    case CAT_LIBRARY: page_library(a, &L); break;
    case CAT_PLAYBACK: page_playback(a, &L); break;
    case CAT_APPEARANCE: page_appearance(a, &L); break;
    case CAT_ACCESS: page_access(a, &L); break;
    case CAT_HOTKEYS: page_hotkeys(a, &L); break;
    case CAT_MPV: page_mpv(a, &L); break;
    default: page_privacy(a, &L); break;
    }
    L.y += dp(ui, 60);
    ui_scroll_end(ui, L.y + off - area.y);
    if (g_capture >= 0) ui->needs_redraw = true;
}
