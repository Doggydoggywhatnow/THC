#include "screenshots.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "settings.h"

void screenshot_dir_resolve(const char *data_dir, const char *setting, char *out, size_t cap)
{
    if (setting && *setting) {
        snprintf(out, cap, "%s", setting);
        return;
    }
    size_t n = strlen(data_dir);
    bool sep = n && (data_dir[n - 1] == '/' || data_dir[n - 1] == '\\');
    snprintf(out, cap, "%s%sscreenshots", data_dir, sep ? "" : "/");
}

const char *screenshot_format_name(int format)
{
    return format == SHOT_JPG ? "jpg" : format == SHOT_WEBP ? "webp" : "png";
}

const char *screenshot_template(const char *setting)
{
    return setting && *setting ? setting : SCREENSHOT_DEFAULT_TEMPLATE;
}

static bool ends_with_ci(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    if (a < b) return false;
    for (size_t i = 0; i < b; i++)
        if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suffix[i])) return false;
    return true;
}

bool screenshot_is_image(const char *name)
{
    return ends_with_ci(name, ".png") || ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") || ends_with_ci(name, ".webp");
}

void screenshot_parse_name(const char *name, char *title, size_t title_cap, char *at, size_t at_cap)
{
    char stem[256];
    snprintf(stem, sizeof stem, "%s", name);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    at[0] = 0;
    /* ...-HH:MM:SS.mmm  (":" becomes "_" or stays, depending on the system) */
    size_t n = strlen(stem);
    for (size_t i = 0; i + 8 <= n; i++) {
        const char *p = stem + i;
        bool ok = (p[-0] != 0) && isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && (p[2] == ':' || p[2] == '_') &&
                  isdigit((unsigned char)p[3]) && isdigit((unsigned char)p[4]) && (p[5] == ':' || p[5] == '_') &&
                  isdigit((unsigned char)p[6]) && isdigit((unsigned char)p[7]);
        if (!ok || i == 0 || (stem[i - 1] != '-' && stem[i - 1] != '_' && stem[i - 1] != ' ')) continue;
        int h = atoi(p), m = atoi(p + 3), sec = atoi(p + 6);
        if (h > 0) snprintf(at, at_cap, "%d:%02d:%02d", h, m, sec);
        else snprintf(at, at_cap, "%d:%02d", m, sec);
        stem[i - 1] = 0;
        break;
    }
    snprintf(title, title_cap, "%s", stem);
}

static int newer_first(const void *a, const void *b)
{
    const ShotInfo *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;
    return strcmp(x->name, y->name);
}

static void add_shot(const char *dir, const char *name, ShotInfo *out, int max, int *n)
{
    if (*n >= max || !screenshot_is_image(name)) return;
    char full[1400];
    snprintf(full, sizeof full, "%s/%s", dir, name);
    struct stat st;
    if (stat(full, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG) return;
    ShotInfo *s = &out[(*n)++];
    snprintf(s->name, sizeof s->name, "%s", name);
    s->mtime = (int64_t)st.st_mtime;
    s->size = (int64_t)st.st_size;
    screenshot_parse_name(name, s->title, sizeof s->title, s->at, sizeof s->at);
}

int screenshots_list(const char *dir, ShotInfo *out, int max)
{
    int n = 0;
#ifdef _WIN32
    char pat[1400];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do add_shot(dir, fd.cFileName, out, max, &n);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) add_shot(dir, e->d_name, out, max, &n);
        closedir(d);
    }
#endif
    if (n > 1) qsort(out, (size_t)n, sizeof *out, newer_first);
    return n;
}

bool screenshot_path_in_dir(const char *dir, const char *path)
{
    if (!dir || !*dir || !path) return false;
    size_t dl = strlen(dir);
    while (dl > 1 && (dir[dl - 1] == '/' || dir[dl - 1] == '\\')) dl--;
    if (strncmp(path, dir, dl) != 0) return false;
    const char *rest = path + dl;
    if (*rest != '/' && *rest != '\\') return false;
    rest++;
    if (!*rest) return false;
    /* a plain file name: no separators and no ".." */
    for (const char *p = rest; *p; p++)
        if (*p == '/' || *p == '\\') return false;
    if (!strcmp(rest, ".") || !strcmp(rest, "..") || strstr(rest, "..")) return false;
    return screenshot_is_image(rest);
}

bool screenshot_delete(const char *dir, const char *path)
{
    if (!screenshot_path_in_dir(dir, path)) return false;
    return remove(path) == 0;
}

void screenshot_nsfw_dir(const char *dir, char *out, size_t cap)
{
    snprintf(out, cap, "%s/nsfw", dir);
}

int files_remove_in_dir(const char *dir)
{
    int n = 0;
#ifdef _WIN32
    char pat[1400];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char full[1400];
            snprintf(full, sizeof full, "%s\\%s", dir, fd.cFileName);
            if (remove(full) == 0) n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char full[1400];
            snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
            struct stat st;
            if (lstat(full, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG) continue;
            if (remove(full) == 0) n++;
        }
        closedir(d);
    }
#endif
    return n;
}
