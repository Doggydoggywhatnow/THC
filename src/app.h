/* app.h: THC's application state, shared by the screens. */
#ifndef THC_APP_H
#define THC_APP_H

#include <SDL3/SDL.h>

#include "fstree.h"
#include "history.h"
#include "library.h"
#include "mpv_dyn.h"
#include "settings.h"
#include "tags.h"
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
    int kind[NAV_DEPTH];           /* portal-specific page kind */
    int arg[NAV_DEPTH];            /* e.g. section index, season tab */
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
    VideoLibrary vlib;             /* shows and movies parsed from the scan */

    /* the media item under focus or the mouse (for the "toggle watched" key) */
    const FsNode *focus_file;
    int focus_root;

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

    /* a modal confirmation window (clear history, clear queue) */
    struct { bool open, fresh; int action, arg; char title[96], body[320], yes[48]; } confirm;

    /* small notification at the bottom */
    char toast[256];
    double toast_until;
} App;

/* main.c */
void app_toast(App *a, const char *fmt, ...);
void app_export_history(App *a, bool json);
enum { CONFIRM_CLEAR_HISTORY = 1, CONFIRM_CLEAR_QUEUE };
void app_confirm(App *a, int action, int arg, const char *title, const char *body, const char *yes);
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
/* Playable files of a folder in play order: by disc and track number from their tags when
 * every file has one, else by file name. Returns the count. */
int music_tracks(const App *a, int root, const FsNode *dir, const FsNode **out, int max);

/* screen_settings.c */
bool settings_capture_key(App *a, const SDL_KeyboardEvent *k);

/* scan.c */
void scan_start(App *a);
bool scan_poll(App *a);  /* true when a new library was installed */
void library_free(Library *lib);

/* tagcache.c: audio tags (title, artist, album, track, embedded art) read in the
 * background after each scan. tag_get returns NULL until a file has been read,
 * or if it has no tags. */
void tagcache_start(App *a);
void tagcache_shutdown(void);
const TagInfo *tag_get(const App *a, int root, const FsNode *file);
/* "tag:<path>" key for art_get, if the file has an embedded cover. */
bool tag_art_key(const App *a, int root, const FsNode *file, char *buf, size_t cap);

/* thumbs.c: video thumbnails. thumb_get fills `out` with an image path (a picture next to the
 * video, or a frame generated in the background into THC's cache) and returns true; false means
 * none yet, so draw the placeholder. Generation needs libmpv and can be turned off in settings. */
bool thumb_get(App *a, int root, const FsNode *file, char *out, size_t cap);
bool thumb_generate(const MpvApi *api, const char *video, const char *out);
void thumbs_clear(App *a);
void thumbs_shutdown(void);

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
/* Art or a generated gradient, without text. */
void draw_art(App *a, SDL_FRect r, const char *seed, const char *art_path, float radius);
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
void nav_push_kind(App *a, PortalNav *n, int kind, int arg, int root, const char *rel);
void app_rebuild_library(App *a);
bool nav_pop(App *a, PortalNav *n);
FsNode *nav_node(App *a, const PortalNav *n);

#endif
