/* THC: a lightweight multimedia player for the music and videos you have. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdarg.h>
#include <string.h>

#include "app.h"
#include "hotkeys.h"

/* ------------------------------------------------------------------------ */
/* History cache: library pages ask for many files every frame.              */
/* ------------------------------------------------------------------------ */

#define HCACHE 8192
typedef struct HSlot {
    Uint64 key;
    Uint64 ver;
    bool found;
    HistoryEntry e;
} HSlot;
static HSlot g_hcache[HCACHE];
static Uint64 g_hver = 1;

static Uint64 hash64(const char *s, Sint64 size)
{
    Uint64 h = 1469598103934665603ull;
    while (*s) { h ^= (Uint8)*s++; h *= 1099511628211ull; }
    h ^= (Uint64)size;
    h *= 1099511628211ull;
    return h ? h : 1;
}

bool app_history(App *a, const FsNode *file, HistoryEntry *e)
{
    char rel[THC_PATH_MAX];
    fs_relpath(file, rel, sizeof rel);
    Uint64 k = hash64(rel, file->size);
    HSlot *s = &g_hcache[k & (HCACHE - 1)];
    if (s->key != k || s->ver != g_hver) {
        s->key = k;
        s->ver = g_hver;
        s->found = history_get(a->hist, rel, file->size, &s->e);
    }
    *e = s->e;
    return s->found;
}

void app_history_changed(void) { g_hver++; }

/* ------------------------------------------------------------------------ */

void app_toast(App *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(a->toast, sizeof a->toast, fmt, ap);
    va_end(ap);
    a->toast_until = a->ui.time + 3.5;
}

void app_confirm(App *a, int action, int arg, const char *title, const char *body, const char *yes)
{
    a->confirm.open = true;
    a->confirm.fresh = true;
    a->confirm.action = action;
    a->confirm.arg = arg;
    SDL_strlcpy(a->confirm.title, title, sizeof a->confirm.title);
    SDL_strlcpy(a->confirm.body, body, sizeof a->confirm.body);
    SDL_strlcpy(a->confirm.yes, yes, sizeof a->confirm.yes);
}

static void SDLCALL export_chosen(void *ud, const char *const *list, int filter)
{
    (void)filter;
    App *a = ud;
    if (!list || !list[0]) return;
    FILE *f = fopen(list[0], "wb");
    bool json = SDL_strstr(list[0], ".csv") == NULL;
    bool ok = f && history_export(a->hist, f, json, -1);
    if (f) fclose(f);
    SDL_Log("history export to %s: %s", list[0], ok ? "ok" : "failed");
}

void app_export_history(App *a, bool json)
{
    static const SDL_DialogFileFilter jf[] = { { "JSON", "json" } }, cf[] = { { "CSV", "csv" } };
    SDL_ShowSaveFileDialog(export_chosen, a, a->win, json ? jf : cf, 1, json ? "thc-history.json" : "thc-history.csv");
    app_toast(a, "Choose where to save your history. THC writes only that file.");
}

void queue_confirmed_clear(App *a, int queue);   /* player.c */

static void run_confirmed(App *a)
{
    switch (a->confirm.action) {
    case CONFIRM_CLEAR_HISTORY:
        if (a->confirm.arg < 0) {
            history_clear(a->hist);
        } else {
            history_clear_kind(a->hist, a->confirm.arg);
        }
        if (a->confirm.arg < 0 || a->confirm.arg == KIND_VIDEO) video_clear_watch_later(a->mpv_dir);
        app_history_changed();
        app_toast(a, "History cleared");
        break;
    case CONFIRM_CLEAR_QUEUE:
        queue_confirmed_clear(a, a->confirm.arg);
        break;
    }
}

void app_save_settings(App *a)
{
    if (!settings_save(&a->set, a->settings_path)) SDL_Log("could not save %s", a->settings_path);
}

void app_apply_appearance(App *a)
{
    if (a->set.theme == THEME_SYSTEM) a->dark = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
    else a->dark = a->set.theme == THEME_DARK;
    a->ui.text_scale = a->set.text_scale;
    a->ui.ui_scale = a->set.ui_scale;
    a->ui.reduced_motion = a->set.reduced_motion;
}

static int portal_of(Screen s)
{
    switch (s) {
    case SCREEN_MUSIC: return PORTAL_MUSIC;
    case SCREEN_BOOKS: return PORTAL_BOOKS;
    case SCREEN_SETTINGS: case SCREEN_SETUP: return PORTAL_NEUTRAL;
    default: return PORTAL_VIDEO;
    }
}

void app_go(App *a, Screen s)
{
    if (a->screen == s) {
        /* pressing the current tab again goes back to the portal's start */
        if (s == SCREEN_VIDEO) nav_reset(&a->vnav);
        if (s == SCREEN_MUSIC) nav_reset(&a->mnav);
        if (s == SCREEN_BOOKS) nav_reset(&a->bnav);
        return;
    }
    if (s >= SCREEN_VIDEO && s <= SCREEN_BOOKS) {
        a->last_portal = s;
        a->set.last_portal = s - SCREEN_VIDEO;
    }
    a->screen = s;
    a->ui.focus = 0;
}

char *app_full_path(const App *a, int root, const FsNode *n, char *buf, size_t cap)
{
    char rel[THC_PATH_MAX];
    fs_relpath(n, rel, sizeof rel);
    const char *base = a->lib.path[root];
    size_t bl = strlen(base);
    bool sep = bl > 0 && (base[bl - 1] == '/' || base[bl - 1] == '\\');
#ifdef _WIN32
    for (char *p = rel; *p; p++) if (*p == '/') *p = '\\';
    SDL_snprintf(buf, cap, "%s%s%s", base, sep ? "" : "\\", rel);
#else
    SDL_snprintf(buf, cap, "%s%s%s", base, sep ? "" : "/", rel);
#endif
    return buf;
}

void app_rescan(App *a)
{
    scan_start(a);
}

void app_rebuild_library(App *a)
{
    LibraryRoot roots[1 + SECTION_COUNT];
    int n = 0;
    roots[n++] = (LibraryRoot){ a->lib.root[ROOT_VIDEO], ROOT_VIDEO, -1 };
    for (int i = 0; i < SECTION_COUNT; i++)
        roots[n++] = (LibraryRoot){ a->lib.root[ROOT_SECTION0 + i], ROOT_SECTION0 + i, i };
    library_clear(&a->vlib);
    library_build(&a->vlib, roots, n, &a->set);
}

/* ------------------------------------------------------------------------ */
/* Folder picker                                                              */
/* ------------------------------------------------------------------------ */

static void SDLCALL folder_chosen(void *ud, const char *const *list, int filter)
{
    (void)filter;
    App *a = ud;
    if (!list || !list[0]) return;
    SDL_LockMutex(a->dlg_mutex);
    SDL_strlcpy(a->dlg_path, list[0], sizeof a->dlg_path);
    a->dlg_ready = true;
    SDL_UnlockMutex(a->dlg_mutex);
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = a->mpv_event;
    SDL_PushEvent(&ev);
}

void app_pick_folder(App *a, int target, const char *current)
{
    a->dlg_target = target;
    SDL_ShowOpenFolderDialog(folder_chosen, a, a->win, current && *current ? current : NULL, false);
}

static char *folder_setting(App *a, int target)
{
    switch (target) {
    case 0: return a->set.music_dir;
    case 1: return a->set.video_dir;
    case 2: return a->set.books_dir;
    default:
        if (target >= 3 && target < 3 + SECTION_COUNT) return a->set.section_dir[target - 3];
        return NULL;
    }
}

static void poll_dialog(App *a)
{
    SDL_LockMutex(a->dlg_mutex);
    if (a->dlg_ready) {
        char *dst = folder_setting(a, a->dlg_target);
        if (dst) SDL_strlcpy(dst, a->dlg_path, THC_PATH_MAX);
        a->dlg_ready = false;
        if (a->set.setup_done) {
            app_save_settings(a);
            app_rescan(a);
        }
    }
    SDL_UnlockMutex(a->dlg_mutex);
}

/* ------------------------------------------------------------------------ */
/* Video playback                                                             */
/* ------------------------------------------------------------------------ */

/* The "now playing in mpv" pill shown while a video plays. */
static void video_pill(App *a)
{
    if (!a->video.active) return;
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float h = dp(ui, 56);
    float w = SDL_min(ui->w - dp(ui, 48), dp(ui, 560));
    SDL_FRect r = { (ui->w - w) / 2, ui->h - music_bar_height(a) - h - dp(ui, 24), w, h };
    ui_shadow(ui, r, h / 2, dp(ui, 28), t->shadow);
    ui_fill(ui, r, t->surface_hi, h / 2);
    if (t->high_contrast) ui_stroke(ui, r, t->border, h / 2, dp(ui, 2));
    float is = dp(ui, 28);
    ui_circle(ui, r.x + h / 2, r.y + h / 2, is / 2 + dp(ui, 4), t->accent);
    ui_icon(ui, a->video.paused ? ICON_PAUSE : ICON_PLAY,
            (SDL_FRect){ r.x + h / 2 - is * 0.35f, r.y + h / 2 - is * 0.35f, is * 0.7f, is * 0.7f }, t->on_accent);
    float x = r.x + h + dp(ui, 4);
    float bw = dp(ui, 84);
    char tm[64], p[32], d[32];
    fmt_time(a->video.pos, p, sizeof p);
    fmt_time(a->video.duration, d, sizeof d);
    SDL_snprintf(tm, sizeof tm, "Playing in mpv  \xC2\xB7  %s / %s", p, d);
    ui_text_fit(ui, FONT_BOLD, TS_BODY, x, r.y + dp(ui, 8), r.w - (x - r.x) - bw - dp(ui, 16), t->text, a->play_title);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, x, r.y + dp(ui, 8) + ui_line_h(ui, TS_BODY), r.w - (x - r.x) - bw - dp(ui, 16),
                t->text_dim, tm);
    float frac = a->video.duration > 0 ? (float)(a->video.pos / a->video.duration) : 0;
    ui_progress(ui, (SDL_FRect){ r.x + h / 2, r.y + h - dp(ui, 4), r.w - h, dp(ui, 3) }, frac, t->accent);
    ui->layer = 1;
    if (ui_button(ui, ui_id("video-stop"), (SDL_FRect){ r.x + r.w - bw - dp(ui, 8), r.y + dp(ui, 8), bw, h - dp(ui, 16) },
                  "Stop", ICON_NONE, false)) {
        player_close_video(a);
    }
    ui->layer = 0;
}

/* ------------------------------------------------------------------------ */
/* Hotkeys                                                                    */
/* ------------------------------------------------------------------------ */

static void toggle_theme(App *a)
{
    a->set.theme = a->dark ? THEME_LIGHT : THEME_DARK;
    app_apply_appearance(a);
    app_save_settings(a);
}

static void change_text(App *a, float d)
{
    a->set.text_scale = SDL_clamp(a->set.text_scale + d, 0.75f, 2.0f);
    app_apply_appearance(a);
    app_save_settings(a);
    app_toast(a, "Text size %d%%", (int)(a->set.text_scale * 100 + 0.5f));
}

static bool handle_hotkey(App *a, const SDL_KeyboardEvent *k)
{
    for (int i = 0; i < ACT_COUNT; i++) {
        if (!hotkey_matches(a->set.hotkey[i], k->key, k->mod)) continue;
        switch ((Action)i) {
        case ACT_PORTAL_VIDEO: app_go(a, SCREEN_VIDEO); return true;
        case ACT_PORTAL_MUSIC: app_go(a, SCREEN_MUSIC); return true;
        case ACT_PORTAL_BOOKS: app_go(a, SCREEN_BOOKS); return true;
        case ACT_SETTINGS: app_go(a, SCREEN_SETTINGS); return true;
        case ACT_TOGGLE_THEME: toggle_theme(a); return true;
        case ACT_RESCAN: app_rescan(a); app_toast(a, "Rescanning your library"); return true;
        case ACT_TEXT_BIGGER: change_text(a, 0.1f); return true;
        case ACT_TEXT_SMALLER: change_text(a, -0.1f); return true;
        case ACT_TOGGLE_WATCHED: {
            const FsNode *f = a->focus_file;
            if (!f || f->dir || f->kind != MK_VIDEO) return false;
            HistoryEntry e;
            bool w = app_history(a, f, &e) && e.watched;
            char rel[THC_PATH_MAX];
            history_set_watched(a->hist, fs_relpath(f, rel, sizeof rel), f->size, !w);
            app_history_changed();
            app_toast(a, w ? "Marked as not watched" : "Marked as watched");
            return true;
        }
        case ACT_FULLSCREEN: {
            bool fs = (SDL_GetWindowFlags(a->win) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(a->win, !fs);
            return true;
        }
        default:
            if (music_hotkey(a, (Action)i)) return true;
            break;
        }
    }
    return false;
}

static void go_back(App *a)
{
    switch (a->screen) {
    case SCREEN_VIDEO: nav_pop(a, &a->vnav); break;
    case SCREEN_MUSIC: nav_pop(a, &a->mnav); break;
    case SCREEN_BOOKS: nav_pop(a, &a->bnav); break;
    case SCREEN_SETTINGS: app_go(a, a->last_portal ? a->last_portal : SCREEN_VIDEO); break;
    default: break;
    }
}

/* ------------------------------------------------------------------------ */

static void debug_script(App *a, Uint64 frame_no);

static void frame(App *a)
{
    Ui *ui = &a->ui;
    ui->th = theme_make(portal_of(a->screen), a->dark, a->set.high_contrast, a->set.accent);
    ui->modal_layer = a->confirm.open ? UI_MODAL_LAYER : 0;
    ui_begin(ui);
    a->focus_file = NULL;
    art_frame(a);
    SDL_SetRenderDrawColor(a->ren, ui->th.bg.r, ui->th.bg.g, ui->th.bg.b, 255);
    SDL_RenderClear(a->ren);
    chrome_background(a);

    if (a->screen == SCREEN_SETUP) {
        screen_setup(a);
    } else {
        float top = dp(ui, TOPBAR_DP);
        float bottom = music_bar_height(a);
        SDL_FRect area = { 0, top, (float)ui->w, ui->h - top - bottom };
        switch (a->screen) {
        case SCREEN_VIDEO: portal_video(a, area); break;
        case SCREEN_MUSIC: portal_music(a, area); break;
        case SCREEN_BOOKS: portal_books(a, area); break;
        case SCREEN_SETTINGS: screen_settings(a, area); break;
        default: break;
        }
        chrome_topbar(a);
        music_bar(a);
        video_pill(a);
    }
    chrome_toast(a);
    if (a->confirm.open) {
        int r = ui_confirm_dialog(ui, a->confirm.title, a->confirm.body, a->confirm.yes, "Cancel", a->confirm.fresh);
        a->confirm.fresh = false;
        if (r) {
            a->confirm.open = false;
            if (r == 1) run_confirmed(a);
        }
    }
    ui_end(ui);
    debug_script(a, ui->frame);
    SDL_RenderPresent(a->ren);

    if (ui->text_active && !SDL_TextInputActive(a->win)) SDL_StartTextInput(a->win);
    else if (!ui->text_active && SDL_TextInputActive(a->win)) SDL_StopTextInput(a->win);
}

/* Developer aid: THC_KEYS="Down,Down,Return" types keys after start-up and
 * THC_SCREENSHOT=out.bmp saves the window once they are done, then quits.
 * Used to check the UI in CI-like headless runs. */
static void debug_script(App *a, Uint64 frame_no)
{
    static const char *keys;
    static const char *shot;
    static bool init;
    static Uint64 next = 20;
    static bool pending_click;
    static float click_x, click_y;
    if (!init) {
        init = true;
        keys = SDL_getenv("THC_KEYS");
        shot = SDL_getenv("THC_SCREENSHOT");
    }
    if (!keys && !shot) return;
    a->ui.needs_redraw = true;
    if (frame_no < next) return;
    if (pending_click) {
        SDL_Event ev;
        SDL_zero(ev);
        pending_click = false;
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.x = click_x;
        ev.button.y = click_y;
        ev.button.down = true;
        SDL_PushEvent(&ev);
        ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
        ev.button.down = false;
        SDL_PushEvent(&ev);
        next = frame_no + 8;
        return;
    }
    if (keys && *keys) {
        char name[32];
        size_t n = strcspn(keys, ",");
        SDL_strlcpy(name, keys, SDL_min(n + 1, sizeof name));
        keys += n + (keys[n] == ',');
        SDL_Event ev;
        SDL_zero(ev);
        if (!SDL_strncmp(name, "wait", 4)) {
            next = frame_no + 60;
            return;
        }
        if (!SDL_strncmp(name, "click:", 6)) {
            float x = 0, y = 0;
            SDL_sscanf(name + 6, "%f:%f", &x, &y);
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.x = x;
            ev.motion.y = y;
            SDL_PushEvent(&ev);
            next = frame_no + 4;
            pending_click = true;
            click_x = x;
            click_y = y;
            return;
        }
        if (0) {
            float x = 0, y = 0;
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.x = x;
            ev.button.y = y;
            ev.button.down = true;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.down = false;
            SDL_PushEvent(&ev);
        } else {
            SDL_Keymod mod = 0;
            const char *k = name;
            if (!SDL_strncmp(k, "Shift+", 6)) { mod = SDL_KMOD_LSHIFT; k += 6; }
            if (!SDL_strncmp(k, "Ctrl+", 5)) { mod = SDL_KMOD_LCTRL; k += 5; }
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.key = SDL_GetKeyFromName(k);
            ev.key.mod = mod;
            ev.key.down = true;
            SDL_PushEvent(&ev);
        }
        next = frame_no + 8;
        return;
    }
    if (shot) {
        SDL_Surface *s = SDL_RenderReadPixels(a->ren, NULL);
        if (s) {
            SDL_SaveBMP(s, shot);
            SDL_DestroySurface(s);
        }
    }
    a->running = false;
}

static void handle_event(App *a, SDL_Event *ev)
{
    Ui *ui = &a->ui;
    if (ev->type == SDL_EVENT_QUIT) {
        a->running = false;
        return;
    }
    if (ev->type == SDL_EVENT_SYSTEM_THEME_CHANGED) {
        app_apply_appearance(a);
        return;
    }
    bool used = ui_event(ui, ev);
    if (ev->type == SDL_EVENT_KEY_DOWN && !used) {
        SDL_Keycode k = ev->key.key;
        if (a->confirm.open) {
            if (k == SDLK_ESCAPE) a->confirm.open = false;
            return;
        }
        if (ui->text_active) {
            if (k == SDLK_ESCAPE) ui->focus = 0;
            return;
        }
        if (a->screen == SCREEN_SETTINGS && settings_capture_key(a, &ev->key)) return;
        if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE || k == SDLK_AC_BACK) {
            go_back(a);
            return;
        }
        if (a->screen != SCREEN_SETUP) handle_hotkey(a, &ev->key);
    }
    if (ui->key_back) {
        ui->key_back = false;
        go_back(a);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static App app;
    App *a = &app;
    SDL_SetAppMetadata("THC", THC_VERSION, "org.thc.player");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    /* Data lives in the OS standard data directory. */
    char *pref = SDL_GetPrefPath("THC", "THC");
    SDL_strlcpy(a->data_dir, pref ? pref : "./", sizeof a->data_dir);
    SDL_free(pref);
    SDL_snprintf(a->mpv_dir, sizeof a->mpv_dir, "%smpv", a->data_dir);
    SDL_CreateDirectory(a->mpv_dir);
    SDL_snprintf(a->settings_path, sizeof a->settings_path, "%ssettings.ini", a->data_dir);
    settings_defaults(&a->set);
    settings_load(&a->set, a->settings_path);

    char db[THC_PATH_MAX + 16], err[256];
    SDL_snprintf(db, sizeof db, "%shistory.db", a->data_dir);
    a->hist = history_open(db, err, sizeof err);
    if (!a->hist) SDL_Log("history database: %s", err);

    a->have_mpv = mpv_dyn_load(&a->mpv, a->set.libmpv_path);
    if (!a->have_mpv) SDL_Log("libmpv not found: video playback is disabled");
    a->mpv_event = SDL_RegisterEvents(1);
    a->dlg_mutex = SDL_CreateMutex();

    a->win = SDL_CreateWindow("THC", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!a->win) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(a->win, 640, 480);
    a->ren = SDL_CreateRenderer(a->win, NULL);
    if (!a->ren) {
        SDL_Log("renderer: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(a->ren, 1);
    if (!ui_init(&a->ui, a->win, a->ren, a->set.font_path)) {
        SDL_Log("ui init failed");
        return 1;
    }
    app_apply_appearance(a);
    music_init(a);

    if (!a->set.setup_done) {
        a->screen = SCREEN_SETUP;
    } else {
        a->screen = (Screen)(SCREEN_VIDEO + a->set.last_portal);
        a->last_portal = a->screen;
        app_rescan(a);
    }

    a->running = true;
    while (a->running) {
        SDL_Event ev;
        bool busy = a->ui.needs_redraw || a->video.active || a->scanning || a->toast[0] || music_busy(a);
        if (!busy) {
            if (SDL_WaitEventTimeout(&ev, 1000)) handle_event(a, &ev);
        }
        while (SDL_PollEvent(&ev)) handle_event(a, &ev);
        poll_dialog(a);
        if (scan_poll(a)) {
            a->focus_file = NULL;
            app_rebuild_library(a);
            tagcache_start(a);
            app_history_changed();
        }
        music_pump(a);
        frame(a);
    }

    music_shutdown(a);
    tagcache_shutdown();
    thumbs_shutdown();
    app_save_settings(a);
    if (a->scan_thread) SDL_WaitThread(a->scan_thread, NULL);
    library_clear(&a->vlib);
    library_free(&a->lib);
    history_close(a->hist);
    art_clear();
    ui_shutdown(&a->ui);
    mpv_dyn_unload(&a->mpv);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_DestroyMutex(a->dlg_mutex);
    SDL_Quit();
    return 0;
}
