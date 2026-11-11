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
    ACT_COUNT
} Action;

typedef struct ActionInfo {
    const char *key;      /* settings key */
    const char *label;    /* shown in the hotkey editor */
    const char *def;      /* default key name (SDL key names) */
} ActionInfo;
extern const ActionInfo k_actions[ACT_COUNT];

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

    /* Privacy */
    bool remember_history;      /* record watch history at all */

    /* Hotkeys: SDL key names, e.g. "Space", "Ctrl+Right" */
    char hotkey[ACT_COUNT][32];

    /* Last portal shown */
    int last_portal;
} Settings;

void settings_defaults(Settings *s);
/* Returns false if the file does not exist (defaults are kept). */
bool settings_load(Settings *s, const char *path);
bool settings_save(const Settings *s, const char *path);

/* Section override lookup; returns -2 if there is none. */
int settings_override(const Settings *s, const char *key);
void settings_set_override(Settings *s, const char *key, int section);

#endif
