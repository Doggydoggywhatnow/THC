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

    bool queue_open;               /* the queue panel is showing */

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
Uint64 app_history_version(void);   /* bumps whenever history changes (for caches) */

/* player.c: the player: both queues, the backends, the bottom bar */
#include "queue.h"
Queue *player_queue(int id);
int player_active_queue(void);
void player_play_index(App *a, int qid, int idx);
void player_play_items(App *a, int qid, const QueueItem *items, int n, int start_index);
void player_play_now(App *a, const QueueItem *it);
void player_next(App *a);
void player_prev(App *a);
void player_toggle_pause(App *a);
void player_stop(App *a);
void player_seek_relative(App *a, double seconds);
void player_set_volume(App *a, int percent);
void player_close_video(App *a);
void player_clear_queue(App *a, int qid);
bool player_has_item(const App *a);
bool player_is_paused(const App *a);
double player_position(const App *a);
double player_duration(const App *a);
const QueueItem *player_current(void);
void qitem_from_node(App *a, QueueItem *it, int root, const FsNode *f, const char *sub, const char *art);
int player_items_for(App *a, int root, const FsNode *dir, QueueItem *out, int max);
int music_tracks(const App *a, int root, const FsNode *dir, const FsNode **out, int max);
void music_init(App *a);
void music_shutdown(App *a);
void music_pump(App *a);
bool music_busy(App *a);
float music_bar_height(App *a);
void music_bar(App *a);
bool music_hotkey(App *a, Action act);
bool music_media_key(App *a, SDL_Keycode k);
void player_cycle_ontop(App *a);
void music_play_folder(App *a, int root, const FsNode *dir, int track_index);
bool music_is_current(App *a, const FsNode *track);
/* Playable files of a folder in play order: by disc and track number from their tags when
 * every file has one, else by file name. Returns the count. */
int music_tracks(const App *a, int root, const FsNode *dir, const FsNode **out, int max);

/* Where to open a context menu for an item: the mouse, or the item's middle when using the keyboard. */
static inline void ctx_pos(const Ui *ui, SDL_FRect r, float *x, float *y)
{
    if (ui->keyboard_mode) { *x = r.x + r.w / 2; *y = r.y + r.h / 2; }
    else { *x = ui->mx; *y = ui->my; }
}

/* portal_music.c: search */
void music_search_focus(App *a);
void music_search_clear(void);

/* screen_queue.c: the queue panel, the right-click menu, adding, export/import, backlog */
float queue_panel_width(App *a);
void queue_panel(App *a, SDL_FRect rect);
void queue_menu_draw(App *a);
bool queue_menu_open(void);
void queue_menu_close(void);
void queue_menu_item(App *a, float x, float y, int root, const FsNode *f);
void queue_menu_dir(App *a, float x, float y, int root, const FsNode *dir);
void queue_menu_show(App *a, float x, float y, const char *show_key, int season);
bool queue_hotkey(App *a, Action act);
void queue_add_dir(App *a, int root, const FsNode *dir, bool next);
void queue_add_show(App *a, const char *show_key, int season, bool unwatched_only);
void queue_add_file(App *a, int root, const FsNode *f, bool next);
void queue_poll(App *a);
void queue_backlog_save(App *a);
void queue_backlog_restore(App *a);
void portal_video_open_show(App *a, const char *show_key);

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
int tagcache_done(void);   /* files read so far (the search index rebuilds as this grows) */
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
