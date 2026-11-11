#include "settings.h"

#include <ctype.h>
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
};

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
    s->remember_history = true;
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

bool settings_load(Settings *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[THC_PATH_MAX * 2 + 64];
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (!*p || *p == '#' || *p == ';' || *p == '[') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(p), *v = trim(eq + 1);

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
        else if (!strcmp(k, "remember_history")) s->remember_history = parse_bool(v);
        else if (!strcmp(k, "video_thumbs")) s->video_thumbs = parse_bool(v);
        else if (!strcmp(k, "last_portal")) s->last_portal = (int)clampd(atoi(v), 0, 2);
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
    fprintf(f, "remember_history = %d\n", s->remember_history);
    fprintf(f, "video_thumbs = %d\n", s->video_thumbs);
    fprintf(f, "\n[hotkeys]\n");
    for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "hotkey.%s = %s\n", k_actions[i].key, s->hotkey[i]);
    fprintf(f, "\n[state]\n");
    fprintf(f, "last_portal = %d\n", s->last_portal);
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
