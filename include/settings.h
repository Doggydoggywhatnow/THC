/* settings.h: THC's settings, stored as a small INI-style text file
 * (settings.ini in THC's data directory). */
#ifndef THC_SETTINGS_H
#define THC_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#define THC_PATH_MAX 1024

/* Optional library sections (beyond the main music and video folders). */
enum { SECTION_ANIME = 0, SECTION_TV, SECTION_CARTOONS, SECTION_MOVIES, SECTION_COUNT };
extern const char *const k_section_names[SECTION_COUNT];
extern const char *const k_section_keys[SECTION_COUNT];

enum { THEME_SYSTEM = 0, THEME_DARK, THEME_LIGHT };
enum { SORT_FOLDER = 0, SORT_NAME, SORT_RECENT_ADDED, SORT_RECENT_WATCHED, SORT_COUNT };
extern const char *const k_sort_names[SORT_COUNT];

/* Remappable actions. */
typedef enum Action {
    ACT_PLAY_PAUSE = 0,
    ACT_NEXT_TRACK,
    ACT_PREV_TRACK,
    ACT_SEEK_BACK,
    ACT_SEEK_FORWARD,
    ACT_VOLUME_UP,
    ACT_VOLUME_DOWN,
    ACT_TOGGLE_WATCHED,
    ACT_PORTAL_VIDEO,
    ACT_PORTAL_MUSIC,
    ACT_PORTAL_BOOKS,
    ACT_SETTINGS,
    ACT_TOGGLE_THEME,
    ACT_RESCAN,
    ACT_TEXT_BIGGER,
    ACT_TEXT_SMALLER,
    ACT_FULLSCREEN,
    ACT_QUEUE_PANEL,
    ACT_QUEUE_ADD,
    ACT_QUEUE_PLAY_NEXT,
    ACT_QUEUE_UP,
    ACT_QUEUE_DOWN,
    ACT_QUEUE_REMOVE,
    ACT_SEARCH,
    ACT_ONTOP,
    ACT_PORTAL_SHOTS,
    ACT_NSFW_MARK,
    ACT_NSFW_PANIC,
    ACT_NSFW_TAB,
    ACT_COUNT
} Action;

typedef struct ActionInfo {
    const char *key;      /* settings key */
    const char *label;    /* shown in the hotkey editor */
    const char *def;      /* default key name (SDL key names) */
} ActionInfo;
extern const ActionInfo k_actions[ACT_COUNT];

enum { SHOT_PNG = 0, SHOT_JPG, SHOT_WEBP };
enum { NSFW_TAB_VISIBLE = 0, NSFW_TAB_HIDDEN };
#define SCREENSHOT_DEFAULT_TEMPLATE "%{media-title}-%P"

/* Kinds of media with their own history switch. */
enum { KIND_VIDEO = 0, KIND_MUSIC = 1, KIND_BOOK = 2, KIND_COUNT };

/* Values of the choice settings (stored in settings.ini as words, see k_*_names in settings.c). */
enum { REPEAT_OFF = 0, REPEAT_ONE, REPEAT_ALL };
enum { ADD_END = 0, ADD_NEXT };
enum { TARGET_HOME = 0, TARGET_ACTIVE };
enum { CHAIN_NONE = 0, CHAIN_OTHER };
enum { VIDEO_IN_MUSIC_WINDOW = 0, VIDEO_IN_MUSIC_LISTEN, VIDEO_IN_MUSIC_SKIP };
enum { SCOPE_ALL = 0, SCOPE_ARTISTS, SCOPE_ALBUMS, SCOPE_SONGS };
enum { KEYS_OFF = 0, KEYS_FOCUSED, KEYS_GLOBAL };
enum { TITLES_FULL = 0, TITLES_PLAYING_ONLY };
enum { ONTOP_OFF = 0, ONTOP_PLAYING, ONTOP_ALWAYS };
enum { LEVEL_WINDOW = 0, LEVEL_SYSTEM };

/* A presentation-only override: show a folder in a different section. */
#define MAX_OVERRIDES 256
typedef struct SectionOverride {
    char path[THC_PATH_MAX]; /* "<library>:<relative folder>" */
    int section;             /* SECTION_*, or -1 to hide */
} SectionOverride;

typedef struct Settings {
    /* Library */
    bool setup_done;
    char music_dir[THC_PATH_MAX];
    char video_dir[THC_PATH_MAX];
    char books_dir[THC_PATH_MAX];
    char section_dir[SECTION_COUNT][THC_PATH_MAX];
    int sort_mode;
    SectionOverride overrides[MAX_OVERRIDES];
    int noverrides;

    /* Playback */
    double watched_threshold;   /* 0.5 .. 1.0 */
    int volume;                 /* 0 .. 100 */
    double book_speed;          /* audiobook playback speed */
    int skip_seconds;           /* audiobook skip forward/back */
    char libmpv_path[THC_PATH_MAX];

    bool video_thumbs;          /* generate thumbnails from video frames */

    /* Appearance */
    int theme;                  /* THEME_* */
    int accent;                 /* 0 = portal default, else preset */
    float text_scale;           /* 0.75 .. 2.0 */
    float ui_scale;             /* 0.75 .. 2.0 */
    char font_path[THC_PATH_MAX]; /* empty = bundled Atkinson Hyperlegible */

    /* Accessibility */
    bool high_contrast;
    bool reduced_motion;
    bool screen_reader;         /* planned: stored, not used yet */

    /* Privacy: history is recorded per portal. private_session is never saved. */
    bool history_video, history_music, history_books;
    bool private_session;
    bool mpv_block_network;     /* THC's MPV: no ytdl, no URLs, no network protocols */

    /* Listening */
    int play_threshold_music;   /* percent of a track that counts as a play, 50..100 */
    int music_resume_min_minutes; /* songs shorter than this start at 0 */

    /* Queue */
    bool queue_auto_advance_video, queue_next_countdown, queue_shuffle, queue_prev_restarts;
    int queue_repeat, queue_add_position, queue_add_target;
    bool queue_resume_video, queue_resume_song, queue_resume_book;
    bool queue_season_unwatched_only, queue_keep_backlog;
    int queue_chain_music, queue_chain_video, queue_video_in_music;
    bool queue_pause_music_on_video, queue_resume_music_after_video, queue_export_relative;

    /* Search */
    bool search_remember_scope;
    int search_last_scope, search_results_per_group;
    bool search_clear_on_leave;

    /* Media keys and now playing */
    int mediakeys_mode;
    bool nowplaying_music, nowplaying_books, nowplaying_video, nowplaying_art;
    int nowplaying_titles, mediakeys_seek_seconds;

    /* Always on top (THC's MPV window) */
    int video_ontop, video_ontop_macos_level;
    bool video_ontop_ignore_fullscreen;
    /* NSFW portal (off by default). Tags live in history.db; nothing on disk is touched. */
    bool nsfw_enabled;
    int nsfw_tab;                            /* NSFW_TAB_VISIBLE / NSFW_TAB_HIDDEN */
    bool nsfw_hide_stops_playback;           /* hiding the tab stops NSFW playback */
    bool nsfw_record_history;                /* off: NSFW items leave no history, plays or backlog */
    bool nsfw_clear_thumbs_on_exit;
    bool nsfw_confirm_before_open;
    bool nsfw_mask_titles;                   /* window title, OSD, queue, now playing */
    bool nsfw_rules_enabled;                 /* removed in 0.3.1 (#34): still read and written back unchanged, never used */
    char nsfw_rules[256];                    /* same: kept for older and newer versions */
    bool nsfw_rules_notice_seen;             /* the one-time "name rules were removed" notice was dismissed */
    char nsfw_dir[THC_PATH_MAX];             /* the NSFW folder (#34): empty = none. Everything in it is NSFW */
    int nsfw_roots;                          /* bit i: library root i is treated as NSFW */
    char nsfw_pin_salt[64], nsfw_pin_hash[96];  /* never shown */
    int nsfw_relock_minutes;                 /* 0 = never */
    /* Screenshots (MPV's s key): folder, format, name template */
    char screenshot_dir[THC_PATH_MAX];       /* empty = <data>/screenshots */
    int screenshot_format;                   /* SHOT_PNG / SHOT_JPG / SHOT_WEBP */
    char screenshot_template[96];            /* empty = SCREENSHOT_DEFAULT_TEMPLATE */

    /* Hotkeys: SDL key names, e.g. "Space", "Ctrl+Right" */
    char hotkey[ACT_COUNT][32];

    /* Last portal shown */
    int last_portal;
    /* Lines THC does not recognise (written by a newer THC), kept verbatim and written back on save so a
     * downgrade does not lose them. "key = value\n" each; never shown or edited. */
    char unknown[8192];
} Settings;

void settings_defaults(Settings *s);
/* Returns false if the file does not exist (defaults are kept). */
/* NSFW name rules were removed in 0.3.1 (#34). Old rules that were switched on get a one-time notice. */
bool settings_nsfw_rules_notice_due(const Settings *s);
bool settings_load(Settings *s, const char *path);
bool settings_save(const Settings *s, const char *path);

/* Is history recorded for this kind of media right now? (off for a portal, or in a private session: no) */
bool settings_history_on(const Settings *s, int kind);

/* Section override lookup; returns -2 if there is none. */
int settings_override(const Settings *s, const char *key);
void settings_set_override(Settings *s, const char *key, int section);

#endif
