/* app.h: THC's application state, shared by the screens. */
#ifndef THC_APP_H
#define THC_APP_H

#include <SDL3/SDL.h>

#include "fstree.h"
#include "history.h"
#include "mpv_dyn.h"
#include "settings.h"
#include "ui.h"
#include "video.h"

typedef enum Screen {
    SCREEN_SETUP = 0,
    SCREEN_VIDEO,
    SCREEN_MUSIC,
    SCREEN_BOOKS,
    SCREEN_SETTINGS
} Screen;

/* Library roots, as scanned from disk. */
enum { ROOT_VIDEO = 0, ROOT_MUSIC, ROOT_BOOKS, ROOT_SECTION0, ROOT_COUNT = ROOT_SECTION0 + SECTION_COUNT };

typedef struct Library {
    FsNode *root[ROOT_COUNT];      /* NULL if the folder is not set */
    char path[ROOT_COUNT][THC_PATH_MAX];
} Library;

/* Navigation inside a portal: a stack of folder paths (relative). */
#define NAV_DEPTH 16
typedef struct PortalNav {
    int depth;
    int root[NAV_DEPTH];
    char rel[NAV_DEPTH][THC_PATH_MAX];
    Uint32 focus[NAV_DEPTH];       /* focus to restore when going back */
} PortalNav;

typedef struct App {
    SDL_Window *win;
    SDL_Renderer *ren;
    Ui ui;
    bool running;

    Settings set;
    char data_dir[THC_PATH_MAX];
    char mpv_dir[THC_PATH_MAX];
    char settings_path[THC_PATH_MAX];
    bool dark;                     /* resolved light/dark */

    History *hist;
    MpvApi mpv;
    bool have_mpv;
    Uint32 mpv_event;

    /* video playback */
    VideoSession video;
    char play_key[THC_PATH_MAX];   /* history key of the playing file */
    Sint64 play_size;
    char play_title[256];

    /* library */
    Library lib;
    SDL_Thread *scan_thread;
    SDL_AtomicInt scan_done;
    Library *scan_result;
    bool scanning;
    Uint64 lib_version;            /* bumps on every new scan */

    /* screens */
    Screen screen;
    Screen last_portal;
    PortalNav vnav, mnav, bnav;
    int setup_step;

    /* folder picker results (dialogs call back on another thread) */
    SDL_Mutex *dlg_mutex;
    char dlg_path[THC_PATH_MAX];
    int dlg_target;                /* which setting to fill */
    bool dlg_ready;

    /* small notification at the bottom */
    char toast[256];
    double toast_until;
} App;

/* main.c */
void app_toast(App *a, const char *fmt, ...);
void app_save_settings(App *a);
void app_apply_appearance(App *a);
void app_play_video(App *a, int root, const FsNode *file);
void app_rescan(App *a);
void app_go(App *a, Screen s);
void app_pick_folder(App *a, int target, const char *current);
char *app_full_path(const App *a, int root, const FsNode *n, char *buf, size_t cap);
/* Look up history for a file node. */
bool app_history(App *a, const FsNode *file, HistoryEntry *e);

void app_history_changed(void);

/* music.c: the music and audiobook player (bottom bar) */
void music_init(App *a);
void music_shutdown(App *a);
void music_pump(App *a);
bool music_busy(App *a);
float music_bar_height(App *a);
void music_bar(App *a);
bool music_hotkey(App *a, Action act);
void music_play_folder(App *a, int root, const FsNode *dir, int track_index);
bool music_is_current(App *a, const FsNode *track);

/* screen_settings.c */
bool settings_capture_key(App *a, const SDL_KeyboardEvent *k);

/* scan.c */
void scan_start(App *a);
bool scan_poll(App *a);  /* true when a new library was installed */
void library_free(Library *lib);

/* art.c */
SDL_Texture *art_get(App *a, const char *path);
void art_frame(App *a);
void art_clear(void);

/* chrome.c */
#define TOPBAR_DP 72
void chrome_background(App *a);
void chrome_topbar(App *a);
void chrome_toast(App *a);
/* A poster/cover card. Draws art or a generated gradient. */
void draw_cover(App *a, SDL_FRect r, const char *title, const char *art_path, float radius, UiIcon icon);
/* Section title with an optional "N items" note. */
void draw_heading(App *a, float x, float y, const char *title, const char *note);
/* Empty-state panel. */
void draw_empty(App *a, SDL_FRect r, UiIcon icon, const char *title, const char *body,
                const char *button, bool *pressed);
void fmt_time(double secs, char *buf, size_t cap);

/* screens */
void screen_setup(App *a);
void portal_video(App *a, SDL_FRect area);
void portal_music(App *a, SDL_FRect area);
void portal_books(App *a, SDL_FRect area);
void screen_settings(App *a, SDL_FRect area);

/* portal navigation helpers */
void nav_reset(PortalNav *n);
void nav_push(App *a, PortalNav *n, int root, const char *rel);
bool nav_pop(App *a, PortalNav *n);
FsNode *nav_node(App *a, const PortalNav *n);

#endif
