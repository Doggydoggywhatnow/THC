#include "settings.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const k_section_names[SECTION_COUNT] = { "Anime", "Television", "Cartoons", "Movies" };
const char *const k_section_keys[SECTION_COUNT] = { "anime", "television", "cartoons", "movies" };
const char *const k_sort_names[SORT_COUNT] = { "Folder order", "A to Z", "Recently added", "Recently watched" };

const ActionInfo k_actions[ACT_COUNT] = {
    { "play_pause", "Play / pause music or audiobook", "Space" },
    { "next_track", "Next track", "Ctrl+Right" },
    { "prev_track", "Previous track", "Ctrl+Left" },
    { "seek_back", "Skip back", "J" },
    { "seek_forward", "Skip forward", "L" },
    { "volume_up", "Volume up", "=" },
    { "volume_down", "Volume down", "-" },
    { "toggle_watched", "Mark watched / unwatched", "W" },
    { "portal_video", "Go to Video", "1" },
    { "portal_music", "Go to Music", "2" },
    { "portal_books", "Go to Audiobooks", "3" },
    { "settings", "Open settings", "Ctrl+," },
    { "toggle_theme", "Switch light / dark", "F2" },
    { "rescan", "Rescan libraries", "F5" },
    { "text_bigger", "Larger text", "Ctrl+=" },
    { "text_smaller", "Smaller text", "Ctrl+-" },
    { "fullscreen", "Fullscreen", "F11" },
    { "queue_panel", "Show or hide the queue", "Q" },
    { "queue_add", "Add to the queue", "A" },
    { "queue_play_next", "Play next (queue)", "Shift+A" },
    { "queue_up", "Move up in the queue", "Alt+Up" },
    { "queue_down", "Move down in the queue", "Alt+Down" },
    { "queue_remove", "Remove from the queue", "Delete" },
    { "search", "Search music", "/" },
    { "video_ontop", "Video window on top", "Ctrl+T" },
};

/* ---- table-driven settings (the 0.2.0 additions) ---- */
enum { ST_BOOL, ST_INT, ST_ENUM };
typedef struct SettingDef {
    const char *key;
    int type;
    size_t off;
    int def, lo, hi;
    const char *const *names;   /* ST_ENUM: the words in settings.ini */
    bool save;
} SettingDef;

static const char *const n_repeat[] = { "off", "one", "all", NULL };
static const char *const n_addpos[] = { "end", "next", NULL };
static const char *const n_target[] = { "home", "active", NULL };
static const char *const n_chain[] = { "none", "other", NULL };
static const char *const n_vim[] = { "window", "listen", "skip", NULL };
static const char *const n_scope[] = { "all", "artists", "albums", "songs", NULL };
static const char *const n_keys[] = { "off", "focused", "global", NULL };
static const char *const n_titles[] = { "full", "playing-only", NULL };
static const char *const n_ontop[] = { "off", "playing", "always", NULL };
static const char *const n_level[] = { "window", "system", NULL };

#define B(key, field, def) { key, ST_BOOL, offsetof(Settings, field), def, 0, 1, NULL, true }
#define I(key, field, def, lo, hi) { key, ST_INT, offsetof(Settings, field), def, lo, hi, NULL, true }
#define E(key, field, def, names) { key, ST_ENUM, offsetof(Settings, field), def, 0, 0, names, true }
static const SettingDef k_defs[] = {
    B("history_video", history_video, 1), B("history_music", history_music, 1), B("history_books", history_books, 1),
    B("mpv_block_network", mpv_block_network, 1),
    I("play_threshold_music", play_threshold_music, 65, 50, 100),
    I("music_resume_min_minutes", music_resume_min_minutes, 10, 0, 60),
    B("queue_auto_advance_video", queue_auto_advance_video, 1), B("queue_next_countdown", queue_next_countdown, 0),
    E("queue_repeat", queue_repeat, 0, n_repeat), B("queue_shuffle", queue_shuffle, 0),
    B("queue_prev_restarts", queue_prev_restarts, 1), E("queue_add_position", queue_add_position, 0, n_addpos),
    E("queue_add_target", queue_add_target, 0, n_target),
    B("queue_resume_video", queue_resume_video, 1), B("queue_resume_song", queue_resume_song, 0),
    B("queue_resume_book", queue_resume_book, 1), B("queue_season_unwatched_only", queue_season_unwatched_only, 0),
    B("queue_keep_backlog", queue_keep_backlog, 0), E("queue_chain_music", queue_chain_music, 0, n_chain),
    E("queue_chain_video", queue_chain_video, 0, n_chain), E("queue_video_in_music", queue_video_in_music, 0, n_vim),
    B("queue_pause_music_on_video", queue_pause_music_on_video, 1),
    B("queue_resume_music_after_video", queue_resume_music_after_video, 1),
    B("queue_export_relative", queue_export_relative, 1),
    B("search_remember_scope", search_remember_scope, 0), E("search_last_scope", search_last_scope, 0, n_scope),
    I("search_results_per_group", search_results_per_group, 5, 3, 20), B("search_clear_on_leave", search_clear_on_leave, 1),
    E("mediakeys_mode", mediakeys_mode, 2, n_keys), B("nowplaying_music", nowplaying_music, 1),
    B("nowplaying_books", nowplaying_books, 1), B("nowplaying_video", nowplaying_video, 0),
    E("nowplaying_titles", nowplaying_titles, 0, n_titles), B("nowplaying_art", nowplaying_art, 1),
    I("mediakeys_seek_seconds", mediakeys_seek_seconds, 10, 5, 60),
    E("video_ontop", video_ontop, 0, n_ontop), E("video_ontop_macos_level", video_ontop_macos_level, 0, n_level),
    B("video_ontop_ignore_fullscreen", video_ontop_ignore_fullscreen, 1),
};
#undef B
#undef I
#undef E
#define NDEFS ((int)(sizeof k_defs / sizeof k_defs[0]))

static void *def_ptr(Settings *s, const SettingDef *d) { return (char *)s + d->off; }

static void apply_default(Settings *s, const SettingDef *d)
{
    if (d->type == ST_BOOL) *(bool *)def_ptr(s, d) = d->def != 0;
    else *(int *)def_ptr(s, d) = d->def;
}

bool settings_history_on(const Settings *s, int kind)
{
    if (s->private_session) return false;
    switch (kind) {
    case KIND_MUSIC: return s->history_music;
    case KIND_BOOK: return s->history_books;
    default: return s->history_video;
    }
}

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->sort_mode = SORT_FOLDER;
    s->watched_threshold = 0.90;
    s->volume = 80;
    s->book_speed = 1.0;
    s->skip_seconds = 30;
    s->theme = THEME_SYSTEM;
    s->accent = 0;
    s->text_scale = 1.0f;
    s->ui_scale = 1.0f;
    for (int i = 0; i < NDEFS; i++) apply_default(s, &k_defs[i]);
    s->video_thumbs = true;
    for (int i = 0; i < ACT_COUNT; i++)
        snprintf(s->hotkey[i], sizeof s->hotkey[i], "%s", k_actions[i].def);
}

static char *trim(char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)e[-1])) *--e = 0;
    return p;
}

static bool parse_bool(const char *v)
{
    return !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on");
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

#define STR(field) snprintf(s->field, sizeof s->field, "%s", v)

static bool parse_def(Settings *s, const char *k, const char *v, bool *seen_history)
{
    for (int i = 0; i < NDEFS; i++) {
        const SettingDef *d = &k_defs[i];
        if (strcmp(k, d->key)) continue;
        if (d->type == ST_BOOL) *(bool *)def_ptr(s, d) = parse_bool(v);
        else if (d->type == ST_INT) *(int *)def_ptr(s, d) = (int)clampd(atoi(v), d->lo, d->hi);
        else {
            int val = d->def;
            for (int n = 0; d->names[n]; n++)
                if (!strcmp(v, d->names[n])) val = n;
            *(int *)def_ptr(s, d) = val;
        }
        if (!strncmp(k, "history_", 8)) *seen_history = true;
        return true;
    }
    return false;
}

/* Is this a key the current THC reads? Anything else is kept as it is (settings compatibility rule). */
static bool key_known(const char *k)
{
    static const char *const fixed[] = {
        "setup_done", "music_dir", "video_dir", "books_dir", "sort_mode", "watched_threshold", "volume", "book_speed",
        "skip_seconds", "libmpv_path", "theme", "accent", "text_scale", "ui_scale", "font_path", "high_contrast",
        "reduced_motion", "screen_reader", "remember_history", "video_thumbs", "last_portal", NULL
    };
    for (int i = 0; fixed[i]; i++)
        if (!strcmp(k, fixed[i])) return true;
    for (int i = 0; i < NDEFS; i++)
        if (!strcmp(k, k_defs[i].key)) return true;
    if (!strncmp(k, "override.", 9)) return true;
    if (!strncmp(k, "section.", 8)) {
        for (int i = 0; i < SECTION_COUNT; i++)
            if (!strcmp(k + 8, k_section_keys[i])) return true;
        return false;
    }
    if (!strncmp(k, "hotkey.", 7)) {
        for (int i = 0; i < ACT_COUNT; i++)
            if (!strcmp(k + 7, k_actions[i].key)) return true;
        return false;
    }
    return false;
}

static void keep_unknown(Settings *s, const char *k, const char *v)
{
    size_t used = strlen(s->unknown), need = strlen(k) + strlen(v) + 4;
    if (used + need >= sizeof s->unknown) return;      /* a full buffer drops the rest, never overflows */
    snprintf(s->unknown + used, sizeof s->unknown - used, "%s = %s\n", k, v);
}

bool settings_load(Settings *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool legacy_remember = false, legacy_value = true, seen_history_key = false;
    char line[THC_PATH_MAX * 2 + 64];
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (!*p || *p == '#' || *p == ';' || *p == '[') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(p), *v = trim(eq + 1);
        if (!key_known(k)) { keep_unknown(s, k, v); continue; }

        if (!strcmp(k, "setup_done")) s->setup_done = parse_bool(v);
        else if (!strcmp(k, "music_dir")) STR(music_dir);
        else if (!strcmp(k, "video_dir")) STR(video_dir);
        else if (!strcmp(k, "books_dir")) STR(books_dir);
        else if (!strcmp(k, "sort_mode")) s->sort_mode = (int)clampd(atoi(v), 0, SORT_COUNT - 1);
        else if (!strcmp(k, "watched_threshold")) s->watched_threshold = clampd(atof(v), 0.5, 1.0);
        else if (!strcmp(k, "volume")) s->volume = (int)clampd(atoi(v), 0, 100);
        else if (!strcmp(k, "book_speed")) s->book_speed = clampd(atof(v), 0.5, 3.0);
        else if (!strcmp(k, "skip_seconds")) s->skip_seconds = (int)clampd(atoi(v), 5, 120);
        else if (!strcmp(k, "libmpv_path")) STR(libmpv_path);
        else if (!strcmp(k, "theme")) s->theme = (int)clampd(atoi(v), 0, 2);
        else if (!strcmp(k, "accent")) s->accent = (int)clampd(atoi(v), 0, 6);
        else if (!strcmp(k, "text_scale")) s->text_scale = (float)clampd(atof(v), 0.75, 2.0);
        else if (!strcmp(k, "ui_scale")) s->ui_scale = (float)clampd(atof(v), 0.75, 2.0);
        else if (!strcmp(k, "font_path")) STR(font_path);
        else if (!strcmp(k, "high_contrast")) s->high_contrast = parse_bool(v);
        else if (!strcmp(k, "reduced_motion")) s->reduced_motion = parse_bool(v);
        else if (!strcmp(k, "screen_reader")) s->screen_reader = parse_bool(v);
        else if (!strcmp(k, "remember_history")) { legacy_remember = true; legacy_value = parse_bool(v); }
        else if (!strcmp(k, "video_thumbs")) s->video_thumbs = parse_bool(v);
        else if (!strcmp(k, "last_portal")) s->last_portal = (int)clampd(atoi(v), 0, 2);
        else if (parse_def(s, k, v, &seen_history_key)) { /* a table-driven setting */ }
        else if (!strncmp(k, "section.", 8)) {
            for (int i = 0; i < SECTION_COUNT; i++)
                if (!strcmp(k + 8, k_section_keys[i]))
                    snprintf(s->section_dir[i], sizeof s->section_dir[i], "%s", v);
        } else if (!strncmp(k, "hotkey.", 7)) {
            for (int i = 0; i < ACT_COUNT; i++)
                if (!strcmp(k + 7, k_actions[i].key))
                    snprintf(s->hotkey[i], sizeof s->hotkey[i], "%s", v);
        } else if (!strncmp(k, "override.", 9)) {
            settings_set_override(s, k + 9, atoi(v));
        }
    }
    fclose(f);
    /* 0.1.x had one switch for all history; it becomes the three per-portal switches */
    if (legacy_remember && !seen_history_key)
        s->history_video = s->history_music = s->history_books = legacy_value;
    return true;
}

bool settings_save(const Settings *s, const char *path)
{
    char tmp[THC_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    fprintf(f, "# THC settings. Edit while THC is closed, or use the Settings screen.\n");
    fprintf(f, "\n[library]\n");
    fprintf(f, "setup_done = %d\n", s->setup_done);
    fprintf(f, "music_dir = %s\n", s->music_dir);
    fprintf(f, "video_dir = %s\n", s->video_dir);
    fprintf(f, "books_dir = %s\n", s->books_dir);
    for (int i = 0; i < SECTION_COUNT; i++)
        fprintf(f, "section.%s = %s\n", k_section_keys[i], s->section_dir[i]);
    fprintf(f, "sort_mode = %d\n", s->sort_mode);
    for (int i = 0; i < s->noverrides; i++)
        fprintf(f, "override.%s = %d\n", s->overrides[i].path, s->overrides[i].section);
    fprintf(f, "\n[playback]\n");
    fprintf(f, "watched_threshold = %.2f\n", s->watched_threshold);
    fprintf(f, "volume = %d\n", s->volume);
    fprintf(f, "book_speed = %.2f\n", s->book_speed);
    fprintf(f, "skip_seconds = %d\n", s->skip_seconds);
    fprintf(f, "libmpv_path = %s\n", s->libmpv_path);
    fprintf(f, "\n[appearance]\n");
    fprintf(f, "theme = %d\n", s->theme);
    fprintf(f, "accent = %d\n", s->accent);
    fprintf(f, "text_scale = %.2f\n", s->text_scale);
    fprintf(f, "ui_scale = %.2f\n", s->ui_scale);
    fprintf(f, "font_path = %s\n", s->font_path);
    fprintf(f, "\n[accessibility]\n");
    fprintf(f, "high_contrast = %d\n", s->high_contrast);
    fprintf(f, "reduced_motion = %d\n", s->reduced_motion);
    fprintf(f, "screen_reader = %d\n", s->screen_reader);
    fprintf(f, "\n[privacy]\n");
    fprintf(f, "video_thumbs = %d\n", s->video_thumbs);
    fprintf(f, "\n[history, queue, search, media keys, window]\n");
    for (int i = 0; i < NDEFS; i++) {
        const SettingDef *d = &k_defs[i];
        void *p = (char *)s + d->off;
        if (d->type == ST_BOOL) fprintf(f, "%s = %d\n", d->key, *(bool *)p);
        else if (d->type == ST_INT) fprintf(f, "%s = %d\n", d->key, *(int *)p);
        else fprintf(f, "%s = %s\n", d->key, d->names[*(int *)p]);
    }
    fprintf(f, "\n[hotkeys]\n");
    for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "hotkey.%s = %s\n", k_actions[i].key, s->hotkey[i]);
    fprintf(f, "\n[state]\n");
    fprintf(f, "last_portal = %d\n", s->last_portal);
    if (s->unknown[0]) {
        fprintf(f, "\n[kept from a newer THC]\n");
        fputs(s->unknown, f);
    }
    bool ok = fclose(f) == 0;
    if (!ok) return false;
    remove(path);
    return rename(tmp, path) == 0;
}

int settings_override(const Settings *s, const char *key)
{
    for (int i = 0; i < s->noverrides; i++)
        if (!strcmp(s->overrides[i].path, key)) return s->overrides[i].section;
    return -2;
}

void settings_set_override(Settings *s, const char *key, int section)
{
    for (int i = 0; i < s->noverrides; i++) {
        if (!strcmp(s->overrides[i].path, key)) {
            if (section == -2) {
                s->overrides[i] = s->overrides[--s->noverrides];
            } else {
                s->overrides[i].section = section;
            }
            return;
        }
    }
    if (section == -2 || s->noverrides >= MAX_OVERRIDES) return;
    snprintf(s->overrides[s->noverrides].path, sizeof s->overrides[0].path, "%s", key);
    s->overrides[s->noverrides].section = section;
    s->noverrides++;
}
