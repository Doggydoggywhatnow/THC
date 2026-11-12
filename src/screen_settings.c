/* screen_settings.c: everything the user can change, grouped by topic. */
#include "app.h"
#include "ontop.h"
#include "hotkeys.h"
#include "mpvconf.h"

#include <string.h>

enum { CAT_LIBRARY = 0, CAT_PLAYBACK, CAT_APPEARANCE, CAT_ACCESS, CAT_QUEUE, CAT_SYSTEM, CAT_HOTKEYS, CAT_MPV, CAT_PRIVACY, CAT_COUNT };
static const char *const k_cat_names[CAT_COUNT] = { "Library", "Playback", "Appearance", "Accessibility", "Queue", "System",
                                                    "Hotkeys", "MPV", "Privacy" };

static int g_cat;
static int g_capture = -1;        /* action waiting for a key press */
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
    group(a, L, "Music search", "Search matches artists, albums and songs, ignoring case and accents, and forgives one typo. Audiobooks are never included.");
    {
        Settings *st = &a->set;
        if (ui_toggle(ui, ui_id("set-s-remember"), next_row(a, L, dp(ui, 72)), "Remember my search scope", "Otherwise search always starts with All", &st->search_remember_scope))
            app_save_settings(a);
        float per = (float)st->search_results_per_group;
        if (ui_slider(ui, ui_id("set-s-per"), next_row(a, L, 0), "Results per group", &per, 3, 20, 1, "%.0f")) {
            st->search_results_per_group = (int)per;
            app_save_settings(a);
        }
        if (ui_toggle(ui, ui_id("set-s-clear"), next_row(a, L, dp(ui, 72)), "Clear the search when I leave Music", NULL, &st->search_clear_on_leave))
            app_save_settings(a);
    }
    L->y += dp(ui, 8);
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
    float pt = (float)a->set.play_threshold_music;
    if (ui_slider(ui, ui_id("set-playthr"), next_row(a, L, 0), "A song counts as played at", &pt, 50, 100, 5, "%.0f%%")) {
        a->set.play_threshold_music = (int)pt;
        app_save_settings(a);
    }
    float rm = (float)a->set.music_resume_min_minutes;
    if (ui_slider(ui, ui_id("set-resumemin"), next_row(a, L, 0), "Resume songs longer than", &rm, 0, 60, 1, "%.0f min")) {
        a->set.music_resume_min_minutes = (int)rm;
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


/* A row of one on/off setting; saves when it changes. */
static void tog(App *a, Layout *L, const char *id, const char *label, const char *hint, bool *v)
{
    if (ui_toggle(&a->ui, ui_id(id), next_row(a, L, dp(&a->ui, 72)), label, hint, v)) app_save_settings(a);
}

/* A choice setting stored as an int; saves when it changes. */
static void choice(App *a, Layout *L, const char *id, const char *label, const char *const *opts, int n, int *v)
{
    if (ui_segmented(&a->ui, ui_id(id), next_row(a, L, 0), label, opts, n, v)) app_save_settings(a);
}

static void page_queue(App *a, Layout *L)
{
    Settings *s = &a->set;
    group(a, L, "Adding", "Right-click anything, press A on it, or use the Add to queue buttons. Items go to the end by default.");
    static const char *const addpos[] = { "End", "Next" };
    static const char *const target[] = { "Home queue", "Active queue" };
    choice(a, L, "set-q-addpos", "Add at", addpos, 2, &s->queue_add_position);
    choice(a, L, "set-q-target", "Add to", target, 2, &s->queue_add_target);
    tog(a, L, "set-q-unwatched", "Add unwatched episodes only", "When adding a season or a show", &s->queue_season_unwatched_only);

    group(a, L, "Playing", NULL);
    tog(a, L, "set-q-auto", "Play the next video automatically", "When a video ends in the video queue", &s->queue_auto_advance_video);
    tog(a, L, "set-q-count", "Wait 5 seconds before the next video", "Press Next to skip the wait", &s->queue_next_countdown);
    tog(a, L, "set-q-prev", "Previous restarts the item first", "After 3 seconds in, Previous restarts; before that it goes back", &s->queue_prev_restarts);
    static const char *const rep[] = { "Off", "One", "All" };
    choice(a, L, "set-q-repeat", "Repeat", rep, 3, &s->queue_repeat);
    tog(a, L, "set-q-shuffle", "Shuffle", "Mixes only what is still to play", &s->queue_shuffle);
    player_queue(QUEUE_MUSIC)->repeat = player_queue(QUEUE_VIDEO)->repeat = s->queue_repeat;
    player_queue(QUEUE_MUSIC)->shuffle = player_queue(QUEUE_VIDEO)->shuffle = s->queue_shuffle;

    group(a, L, "Resume", "Where queued items start, from your history.");
    tog(a, L, "set-q-rv", "Resume videos", NULL, &s->queue_resume_video);
    tog(a, L, "set-q-rs", "Resume songs", "Only songs longer than the limit in Playback", &s->queue_resume_song);
    tog(a, L, "set-q-rb", "Resume audiobooks", NULL, &s->queue_resume_book);

    group(a, L, "Two queues", "Music and audiobooks use the music queue; videos use the video queue. Either can hold any type.");
    static const char *const chain[] = { "Stop", "Play the other queue" };
    choice(a, L, "set-q-cm", "When the music queue ends", chain, 2, &s->queue_chain_music);
    choice(a, L, "set-q-cv", "When the video queue ends", chain, 2, &s->queue_chain_video);
    static const char *const vim[] = { "Window", "Listen only", "Skip" };
    choice(a, L, "set-q-vim", "A video in the music queue", vim, 3, &s->queue_video_in_music);
    tog(a, L, "set-q-pause", "Pause music when a video starts", NULL, &s->queue_pause_music_on_video);
    tog(a, L, "set-q-resume", "Resume the music after the video", NULL, &s->queue_resume_music_after_video);

    group(a, L, "Saving", "The upcoming queue is never saved. The backlog is what you already played.");
    tog(a, L, "set-q-backlog", "Keep the queue backlog between sessions", "Only for portals that have history turned on", &s->queue_keep_backlog);
    tog(a, L, "set-q-rel", "Export playlists with relative paths", "Paths relative to your library folders", &s->queue_export_relative);
}

static void page_system(App *a, Layout *L)
{
    Settings *s = &a->set;
    group(a, L, "Media keys", "Play, pause, next and previous from your keyboard, headset and the desktop's media controls.");
    static const char *const modes[] = { "Off", "THC focused", "System-wide" };
    choice(a, L, "set-mk-mode", "Media keys", modes, 3, &s->mediakeys_mode);
    float sk = (float)s->mediakeys_seek_seconds;
    if (ui_slider(&a->ui, ui_id("set-mk-seek"), next_row(a, L, 0), "Fast-forward and rewind keys", &sk, 5, 60, 5, "%.0f s")) {
        s->mediakeys_seek_seconds = (int)sk;
        app_save_settings(a);
    }
    note(a, L, ICON_SPARK,
         "System-wide works through MPRIS on Linux (needs libdbus). On Windows and macOS the keys work while THC's window is focused.");

    group(a, L, "Video window", "How THC's MPV window behaves among your other windows.");
    static const char *const ontop[] = { "Off", "While playing", "Always" };
    choice(a, L, "set-ontop", "Keep video on top", ontop, 3, &s->video_ontop);
    tog(a, L, "set-ontop-fs", "Ignore in fullscreen", "Fullscreen already covers the screen", &s->video_ontop_ignore_fullscreen);
    static const char *const levels[] = { "Window level", "Above everything" };
    choice(a, L, "set-ontop-level", "macOS stacking level", levels, 2, &s->video_ontop_macos_level);
    if (ontop_is_wayland(SDL_getenv("XDG_SESSION_TYPE"), SDL_getenv("WAYLAND_DISPLAY")))
        note(a, L, ICON_SPARK,
             "On Wayland your compositor decides this. THC's video window has the app id \"thc\", so you can add a rule for it.");
    note(a, L, ICON_SPARK, "This setting wins over \"ontop\" in mpv.conf. The mpv window's own T key still toggles it for a moment.");

    group(a, L, "Now playing", "What your desktop, lock screen and other apps can see about what THC is playing.");
    tog(a, L, "set-np-music", "Show music", "Title, artist and album", &s->nowplaying_music);
    tog(a, L, "set-np-books", "Show audiobooks", "Title and author", &s->nowplaying_books);
    tog(a, L, "set-np-video", "Show video", "Off by default, so what you watch is not shown on lock screens",
        &s->nowplaying_video);
    tog(a, L, "set-np-art", "Show cover art", "Only for items shown above", &s->nowplaying_art);
    static const char *const titles[] = { "Title, artist and album", "Title only" };
    choice(a, L, "set-np-titles", "Details shown", titles, 2, &s->nowplaying_titles);
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

/* ---- helpers for the MPV page: copy paths, osc.conf, shaders, config check ---- */

static void copy_text(App *a, const char *text, const char *what)
{
    if (SDL_SetClipboardText(text)) app_toast(a, "Copied %s", what);
    else app_toast(a, "Could not copy to the clipboard");
}

/* Whole text file, NUL-terminated (caller frees with SDL_free), or NULL. */
static char *read_text(const char *path)
{
    size_t len = 0;
    void *d = SDL_LoadFile(path, &len);
    if (!d) return NULL;
    char *t = SDL_malloc(len + 1);
    if (t) { memcpy(t, d, len); t[len] = 0; }
    SDL_free(d);
    return t;
}

static void mpv_path(App *a, const char *rel, char *out, size_t cap)
{
    SDL_snprintf(out, cap, "%s/%s", a->mpv_dir, rel);
}

static void write_text(App *a, const char *rel, const char *text)
{
    char path[THC_PATH_MAX + 64];
    mpv_path(a, rel, path, sizeof path);
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) { app_toast(a, "Could not write %s", rel); return; }
    SDL_WriteIO(io, text, strlen(text));
    SDL_CloseIO(io);
}

/* A row for one file or folder in THC's MPV folder: its path, Copy path, and Create if missing. */
static void path_row(App *a, Layout *L, const char *rel, const char *what, const char *tmpl, bool is_dir)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    char full[THC_PATH_MAX + 64];
    mpv_path(a, rel, full, sizeof full);
    SDL_PathInfo info;
    bool exists = SDL_GetPathInfo(full, &info);
    SDL_FRect r = next_row(a, L, dp(ui, 72));
    ui_fill(ui, r, t->surface, dp(ui, 12));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    float bw = dp(ui, 130), bh = dp(ui, 40), pad = dp(ui, 14);
    bool can_make = !exists && (tmpl || is_dir);
    float reserve = bw * (can_make ? 2 : 1) + pad * 3;
    char title[160];
    SDL_snprintf(title, sizeof title, "%s%s", rel, exists ? "" : "  (not created yet)");
    (void)what;
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, r.x + dp(ui, 16), r.y + dp(ui, 12), r.w - reserve, t->text, title);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + dp(ui, 16), r.y + dp(ui, 14) + ui_line_h(ui, TS_LABEL), r.w - reserve,
                t->text_dim, full);
    float bx = r.x + r.w - pad - bw;
    char id[200];
    SDL_snprintf(id, sizeof id, "mpv-copy:%s", rel);
    if (ui_button(ui, ui_id(id), (SDL_FRect){ bx, r.y + (r.h - bh) / 2, bw, bh }, "Copy path", ICON_NONE, false))
        copy_text(a, full, rel);
    if (can_make) {
        bx -= bw + pad;
        SDL_snprintf(id, sizeof id, "mpv-make:%s", rel);
        if (ui_button(ui, ui_id(id), (SDL_FRect){ bx, r.y + (r.h - bh) / 2, bw, bh }, "Create", ICON_PLUS, false)) {
            if (is_dir) {
                SDL_CreateDirectory(full);
            } else {
                char dir[THC_PATH_MAX + 64];
                SDL_strlcpy(dir, full, sizeof dir);
                char *slash = SDL_strrchr(dir, '/');
                if (slash) { *slash = 0; SDL_CreateDirectory(dir); }
                write_text(a, rel, tmpl);
            }
            app_toast(a, "Created %s", rel);
        }
    }
}

/* ---- config check ---- */

typedef struct CheckCtx { const MpvApi *api; mpv_handle *h; } CheckCtx;

static int check_setter(void *ud, const char *name, const char *value)
{
    CheckCtx *c = ud;
    return c->api->set_option_string(c->h, name, value);
}

static MpvConfIssue g_issues[32];
static int g_nissues;
static bool g_checked, g_check_failed;
static Uint64 g_check_mtime;
static double g_check_poll;

static void run_check(App *a)
{
    g_nissues = 0;
    g_check_failed = false;
    char path[THC_PATH_MAX + 64];
    mpv_path(a, "mpv.conf", path, sizeof path);
    SDL_PathInfo info;
    g_check_mtime = SDL_GetPathInfo(path, &info) ? (Uint64)info.modify_time : 0;
    g_checked = true;
    char *text = read_text(path);
    if (!text) return;
    if (!a->have_mpv) { g_check_failed = true; SDL_free(text); return; }
    /* ask a throw-away libmpv handle that is never initialized (no window, no playback) */
    mpv_handle *h = a->mpv.create();
    if (h) {
        CheckCtx c = { &a->mpv, h };
        g_nissues = mpvconf_check(text, check_setter, &c, g_issues, 32);
        a->mpv.terminate_destroy(h);
    } else {
        g_check_failed = true;
    }
    SDL_free(text);
}

static void config_check(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    char path[THC_PATH_MAX + 64];
    mpv_path(a, "mpv.conf", path, sizeof path);
    SDL_PathInfo info;
    Uint64 mt = SDL_GetPathInfo(path, &info) ? (Uint64)info.modify_time : 0;
    if (!g_checked || (ui->time > g_check_poll && mt != g_check_mtime)) run_check(a);
    if (ui->time > g_check_poll) g_check_poll = ui->time + 1.0;

    group(a, L, "Config check",
          "THC asks MPV about every line of your mpv.conf and lists the ones MPV skips. MPV itself only "
          "prints these to a terminal.");
    SDL_FRect r = next_row(a, L, dp(ui, 52));
    ui_fill(ui, r, t->surface, dp(ui, 12));
    const char *summary;
    char buf[160];
    UiIcon icon = ICON_CHECK;
    if (!SDL_GetPathInfo(path, &info)) summary = "No mpv.conf yet, so there is nothing to check.";
    else if (g_check_failed) summary = "Could not check: libmpv is not available.";
    else if (g_nissues == 0) summary = "No problems found in mpv.conf.";
    else {
        SDL_snprintf(buf, sizeof buf, "%d line%s need attention", g_nissues, g_nissues == 1 ? "" : "s");
        summary = buf;
        icon = ICON_INFO;
    }
    ui_icon(ui, icon, (SDL_FRect){ r.x + dp(ui, 16), r.y + dp(ui, 14), dp(ui, 24), dp(ui, 24) },
            icon == ICON_CHECK ? t->watched : t->accent);
    ui_text_center(ui, FONT_BOLD, TS_LABEL, (SDL_FRect){ r.x + dp(ui, 52), r.y, r.w - dp(ui, 220), r.h }, t->text, summary, false);
    if (ui_button(ui, ui_id("mpv-recheck"), (SDL_FRect){ r.x + r.w - dp(ui, 150), r.y + dp(ui, 6), dp(ui, 136), dp(ui, 40) },
                  "Check again", ICON_NONE, false)) {
        run_check(a);
        app_toast(a, "Checked mpv.conf");
    }
    static const char *const k_kind[] = { "Skipped: no such option", "Skipped: value not accepted",
                                          "Wrong file: belongs in osc.conf", "Ignored: THC controls this" };
    for (int i = 0; i < g_nissues; i++) {
        const MpvConfIssue *is = &g_issues[i];
        SDL_FRect row = next_row(a, L, dp(ui, 92));
        ui_fill(ui, row, t->surface, dp(ui, 12));
        if (t->high_contrast) ui_stroke(ui, row, t->border, dp(ui, 12), dp(ui, 1));
        float x = row.x + dp(ui, 16), w = row.w - dp(ui, 32);
        char head[320];
        SDL_snprintf(head, sizeof head, "Line %d:  %s=%s", is->line, is->option, is->value);
        ui_text_fit(ui, FONT_BOLD, TS_BODY, x, row.y + dp(ui, 10), w, t->text, head);
        ui_text_fit(ui, FONT_BOLD, TS_SMALL, x, row.y + dp(ui, 10) + ui_line_h(ui, TS_BODY), w, t->accent, k_kind[is->kind]);
        ui_text_wrap(ui, FONT_REGULAR, TS_SMALL, x, row.y + dp(ui, 10) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL), w,
                     t->text_dim, is->hint, 2);
    }
}

/* ---- shaders ---- */

typedef struct ShaderList { char name[24][96]; int n; } ShaderList;

static SDL_EnumerationResult SDLCALL shader_cb(void *ud, const char *dirname, const char *fname)
{
    (void)dirname;
    ShaderList *l = ud;
    const char *dot = SDL_strrchr(fname, '.');
    if (fname[0] == '.' || !dot || (SDL_strcasecmp(dot, ".glsl") && SDL_strcasecmp(dot, ".hook") && SDL_strcasecmp(dot, ".frag")))
        return SDL_ENUM_CONTINUE;
    if (l->n < 24) SDL_strlcpy(l->name[l->n++], fname, sizeof l->name[0]);
    return SDL_ENUM_CONTINUE;
}

static void shaders_section(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    group(a, L, "GLSL shaders",
          "Put shader files (.glsl) in the shaders folder, then load one from mpv.conf with "
          "glsl-shaders-append=\"~~/shaders/name.glsl\". In THC, ~~/ means THC's own MPV folder, not your "
          "standalone MPV's, so copy any shaders you use into it.");
    char dir[THC_PATH_MAX + 64];
    mpv_path(a, "shaders", dir, sizeof dir);
    SDL_PathInfo info;
    bool have = SDL_GetPathInfo(dir, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
    SDL_FRect r = next_row(a, L, dp(ui, 72));
    ui_fill(ui, r, t->surface, dp(ui, 12));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    float bw = dp(ui, 130), bh = dp(ui, 40), pad = dp(ui, 14);
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, r.x + dp(ui, 16), r.y + dp(ui, 12), r.w - bw * 2 - pad * 4, t->text,
                have ? "shaders" : "shaders  (not created yet)");
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + dp(ui, 16), r.y + dp(ui, 14) + ui_line_h(ui, TS_LABEL),
                r.w - bw * 2 - pad * 4, t->text_dim, dir);
    float bx = r.x + r.w - pad - bw;
    if (ui_button(ui, ui_id("shader-copy-dir"), (SDL_FRect){ bx, r.y + (r.h - bh) / 2, bw, bh }, "Copy path", ICON_NONE, false))
        copy_text(a, dir, "shaders folder path");
    bx -= bw + pad;
    if (have) {
        if (ui_button(ui, ui_id("shader-open"), (SDL_FRect){ bx, r.y + (r.h - bh) / 2, bw, bh }, "Open", ICON_FOLDER, false))
            open_folder(a, dir);
    } else if (ui_button(ui, ui_id("shader-make"), (SDL_FRect){ bx, r.y + (r.h - bh) / 2, bw, bh }, "Create", ICON_PLUS, false)) {
        SDL_CreateDirectory(dir);
        app_toast(a, "Created the shaders folder");
    }
    ShaderList *sl = SDL_calloc(1, sizeof *sl);
    if (!sl) return;
    if (have) SDL_EnumerateDirectory(dir, shader_cb, sl);
    if (have && sl->n == 0) {
        ui_text(ui, FONT_REGULAR, TS_BODY, L->x, L->y, t->text_dim, "No shader files in the folder yet.");
        L->y += ui_line_h(ui, TS_BODY) + L->gap;
    }
    for (int i = 0; i < sl->n; i++) {
        SDL_FRect row = next_row(a, L, dp(ui, 64));
        ui_fill(ui, row, t->surface, dp(ui, 12));
        if (t->high_contrast) ui_stroke(ui, row, t->border, dp(ui, 12), dp(ui, 1));
        float bw2 = dp(ui, 190);
        ui_text_center(ui, FONT_BOLD, TS_BODY, (SDL_FRect){ row.x + dp(ui, 16), row.y, row.w - bw2 - bw - pad * 4, row.h },
                       t->text, sl->name[i], false);
        float x2 = row.x + row.w - pad - bw;
        char full[THC_PATH_MAX + 160], line[THC_PATH_MAX + 200], rel[160];
        SDL_snprintf(rel, sizeof rel, "shaders/%s", sl->name[i]);
        mpv_path(a, rel, full, sizeof full);
        if (ui_button(ui, ui_idi("shader-path", i), (SDL_FRect){ x2, row.y + (row.h - bh) / 2, bw, bh }, "Copy path", ICON_NONE, false))
            copy_text(a, full, sl->name[i]);
        x2 -= bw2 + pad;
        if (ui_button(ui, ui_idi("shader-line", i), (SDL_FRect){ x2, row.y + (row.h - bh) / 2, bw2, bh }, "Copy mpv.conf line", ICON_NONE, false)) {
            SDL_snprintf(line, sizeof line, "glsl-shaders-append=\"~~/shaders/%s\"", sl->name[i]);
            copy_text(a, line, "the mpv.conf line");
        }
    }
    SDL_free(sl);
}

/* ---- on-screen controller (osc.conf) ---- */

static void osc_section(App *a, Layout *L)
{
    Ui *ui = &a->ui;
    group(a, L, "On-screen controller (osc.conf)",
          "The controller's settings, such as where it sits, live in script-opts/osc.conf, not in mpv.conf. "
          "Lines like layout=topbar in mpv.conf are skipped by MPV. Restart a video after changing it.");
    path_row(a, L, "script-opts/osc.conf", "Settings for MPV's on-screen controller",
             "# THC's on-screen controller settings (MPV's osc.lua reads this file).\n"
             "# layout: bottombar (default), topbar, box or slimbox\n"
             "layout=bottombar\n"
             "# windowcontrols=auto\n"
             "# seekbarstyle=bar          # bar, diamond or knob\n"
             "# boxalpha=80\n"
             "# hidetimeout=500\n", false);
    static const char *const names[] = { "Bottom bar", "Top bar", "Box", "Slim box" };
    static const char *const vals[] = { "bottombar", "topbar", "box", "slimbox" };
    char path[THC_PATH_MAX + 64], cur[64] = "bottombar";
    mpv_path(a, "script-opts/osc.conf", path, sizeof path);
    char *text = read_text(path);
    if (text) mpvconf_get_key(text, "layout", cur, sizeof cur);
    int idx = 0;
    for (int i = 0; i < 4; i++) if (!strcmp(cur, vals[i])) idx = i;
    if (ui_segmented(ui, ui_id("osc-layout"), next_row(a, L, 0), "Controller layout", names, 4, &idx)) {
        char out[4096];
        char dir[THC_PATH_MAX + 64];
        mpv_path(a, "script-opts", dir, sizeof dir);
        SDL_CreateDirectory(dir);
        if (mpvconf_set_key(text ? text : "", "layout", vals[idx], out, sizeof out)) {
            write_text(a, "script-opts/osc.conf", out);
            app_toast(a, "Layout set to %s. It applies to the next video.", names[idx]);
        }
    }
    SDL_free(text);
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
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    ui_text_fit(ui, FONT_BOLD, TS_LABEL, r.x + dp(ui, 16), r.y + dp(ui, 12), r.w - dp(ui, 330), t->text, "Configuration folder");
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + dp(ui, 16), r.y + dp(ui, 14) + ui_line_h(ui, TS_LABEL), r.w - dp(ui, 330),
                t->text_dim, a->mpv_dir);
    if (ui_button(ui, ui_id("mpv-open"), (SDL_FRect){ r.x + r.w - dp(ui, 150), r.y + dp(ui, 16), dp(ui, 136), dp(ui, 40) },
                  "Open", ICON_FOLDER, false))
        open_folder(a, a->mpv_dir);
    if (ui_button(ui, ui_id("mpv-copy-dir"), (SDL_FRect){ r.x + r.w - dp(ui, 296), r.y + dp(ui, 16), dp(ui, 136), dp(ui, 40) },
                  "Copy path", ICON_NONE, false))
        copy_text(a, a->mpv_dir, "the folder path");
    note(a, L, ICON_INFO,
         "THC's watch history is the only record of where you stopped, so THC controls resume-playback, "
         "save-position-on-quit, watch-later-directory and start. Setting them in mpv.conf has no effect.");
    group(a, L, "Config files", "Each file has the same name and place as in MPV's own folder.");
    path_row(a, L, "mpv.conf", "MPV options",
             "# THC's own mpv.conf. Your standalone MPV does not read this file.\n"
             "# Any MPV option works here, for example:\n"
             "# profile=gpu-hq\n"
             "# hwdec=auto-safe\n"
             "# sub-font-size=46\n"
             "# sub-color=\"#FFFFFF\"\n"
             "# sub-back-color=\"#C0000000\"\n", false);
    path_row(a, L, "input.conf", "Key bindings inside the video window",
             "# THC's own input.conf: key bindings inside the video window.\n"
             "# Examples:\n"
             "# RIGHT seek 10\n"
             "# LEFT seek -10\n", false);
    path_row(a, L, "scripts", "Lua, JavaScript and C scripts load from here", NULL, true);
    osc_section(a, L);
    shaders_section(a, L);
    config_check(a, L);
    note(a, L, ICON_INFO, "To start from your existing MPV setup, copy its files into THC's folder. The two stay "
                          "independent afterward.");

    group(a, L, "libmpv", NULL);
    SDL_FRect s = next_row(a, L, dp(ui, 72));
    ui_fill(ui, s, t->surface, dp(ui, 12));
    char st[THC_PATH_MAX + 32];
    if (a->have_mpv) {
        unsigned long v = a->mpv.client_api_version();
        SDL_snprintf(st, sizeof st, "%s  (client API %lu.%lu)", a->mpv.path, v >> 16, v & 0xffff);
    }
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
    group(a, L, "History",
          "Where you stopped, what you watched and what you played. History is kept until you clear it. Turning a "
          "switch off stops recording for that portal (so resume does not work there); what is already saved stays "
          "until you clear it.");
    static const char *const names[KIND_COUNT] = { "Video history", "Music history", "Audiobook history" };
    static const char *const hints[KIND_COUNT] = { "Resume positions, playback bars and checkmarks",
                                                   "Plays, recently played and most played",
                                                   "Your place in every book" };
    bool *flags[KIND_COUNT] = { &a->set.history_video, &a->set.history_music, &a->set.history_books };
    for (int k = 0; k < KIND_COUNT; k++)
        if (ui_toggle(ui, ui_idi("set-hist", k), next_row(a, L, dp(ui, 72)), names[k], hints[k], flags[k]))
            app_save_settings(a);
    if (ui_toggle(ui, ui_id("set-private"), next_row(a, L, dp(ui, 72)), "Private session",
                  "Record nothing until THC restarts. Not saved.", &a->set.private_session))
        app_toast(a, a->set.private_session ? "Private session on: nothing is recorded" : "Private session off");
    float bw = dp(ui, 200);
    float bx = L->x;
    static const char *const clear_names[KIND_COUNT] = { "Clear video", "Clear music", "Clear audiobooks" };
    static const char *const clear_what[KIND_COUNT] = { "video", "music", "audiobook" };
    for (int k = 0; k < KIND_COUNT; k++) {
        if (ui_button(ui, ui_idi("set-clear", k), (SDL_FRect){ bx, L->y, bw, dp(ui, 46) }, clear_names[k], ICON_CLOSE, false)) {
            char body[200];
            SDL_snprintf(body, sizeof body, "This deletes your %s history: positions, plays and checkmarks. It cannot be undone.",
                         clear_what[k]);
            app_confirm(a, CONFIRM_CLEAR_HISTORY, k, "Clear history?", body, "Clear");
        }
        bx += bw + dp(ui, 10);
        if (bx + bw > L->x + L->w && k < KIND_COUNT - 1) { bx = L->x; L->y += dp(ui, 46) + dp(ui, 10); }
    }
    L->y += dp(ui, 46) + L->gap;
    if (ui_button(ui, ui_id("set-export-json"), (SDL_FRect){ L->x, L->y, dp(ui, 230), dp(ui, 46) }, "Export as JSON", ICON_SORT, false))
        app_export_history(a, true);
    if (ui_button(ui, ui_id("set-export-csv"), (SDL_FRect){ L->x + dp(ui, 242), L->y, dp(ui, 230), dp(ui, 46) }, "Export as CSV", ICON_SORT, false))
        app_export_history(a, false);
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
    case CAT_QUEUE: page_queue(a, &L); break;
    case CAT_SYSTEM: page_system(a, &L); break;
    case CAT_HOTKEYS: page_hotkeys(a, &L); break;
    case CAT_MPV: page_mpv(a, &L); break;
    default: page_privacy(a, &L); break;
    }
    L.y += dp(ui, 60);
    ui_scroll_end(ui, L.y + off - area.y);
    if (g_capture >= 0) ui->needs_redraw = true;
}
