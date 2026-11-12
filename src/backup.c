#include "backup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

#define PATHLEN 1400
#define MAX_DEPTH 8

static void seterr(char *err, size_t cap, const char *fmt, const char *a)
{
    if (err && cap) snprintf(err, cap, fmt, a ? a : "");
}

static bool path_is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static bool path_is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

/* dir + "/" + name, with any trailing separator on dir removed */
static void join(char *out, size_t cap, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    while (n > 1 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) n--;
    snprintf(out, cap, "%.*s/%s", (int)n, dir, name);
}

static bool mkdir_p(const char *path)
{
    char buf[PATHLEN];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char c = *p;
        *p = 0;
        if (!path_is_dir(buf)) MKDIR(buf);
        *p = c;
    }
    if (!path_is_dir(buf)) MKDIR(buf);
    return path_is_dir(buf);
}

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    return fclose(out) == 0 && ok;
}

/* Visit the entries of a directory (not "." and ".."): fn(name, is_dir, ud). Symbolic links are skipped. */
typedef void (*EntryFn)(const char *name, bool is_dir, void *ud);

static void each_entry(const char *dir, EntryFn fn, void *ud)
{
#ifdef _WIN32
    char pat[PATHLEN];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        fn(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, ud);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char full[PATHLEN];
        join(full, sizeof full, dir, e->d_name);
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        int t = st.st_mode & S_IFMT;
        if (t != S_IFDIR && t != S_IFREG) continue;       /* links and special files are never followed */
        fn(e->d_name, t == S_IFDIR, ud);
    }
    closedir(d);
#endif
}

typedef struct CopyCtx { const char *src, *dst; int depth; const char *skip_top; bool ok; int files; } CopyCtx;

static void copy_cb(const char *name, bool is_dir, void *ud);

static bool copy_tree(const char *src, const char *dst, int depth, const char *skip_top, int *files)
{
    if (depth > MAX_DEPTH || !mkdir_p(dst)) return false;
    CopyCtx c = { src, dst, depth, skip_top, true, 0 };
    each_entry(src, copy_cb, &c);
    if (files) *files += c.files;
    return c.ok;
}

static void copy_cb(const char *name, bool is_dir, void *ud)
{
    CopyCtx *c = ud;
    if (c->depth == 0 && c->skip_top && !strcmp(name, c->skip_top)) return;
    char s[PATHLEN], d[PATHLEN];
    join(s, sizeof s, c->src, name);
    join(d, sizeof d, c->dst, name);
    if (is_dir) {
        int n = 0;
        if (!copy_tree(s, d, c->depth + 1, NULL, &n)) c->ok = false;
        c->files += n;
    } else if (copy_file(s, d)) {
        c->files++;
    } else {
        c->ok = false;
    }
}

static void remove_cb(const char *name, bool is_dir, void *ud);

static void remove_tree(const char *path)
{
    each_entry(path, remove_cb, (void *)path);
    remove(path);
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static void remove_cb(const char *name, bool is_dir, void *ud)
{
    char p[PATHLEN];
    join(p, sizeof p, (const char *)ud, name);
    if (is_dir) remove_tree(p);
    else remove(p);
}

static void count_cb(const char *name, bool is_dir, void *ud)
{
    (void)name;
    (void)is_dir;
    ++*(int *)ud;
}

static int count_files(const char *dir, int depth)
{
    if (depth > MAX_DEPTH) return 0;
    int n = 0;
    each_entry(dir, count_cb, &n);
    return n;     /* top-level entries: enough for a summary */
}

static void stamp(char *out, size_t cap)
{
    time_t t = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(out, cap, "%Y%m%d-%H%M%S", &tmv);
}

int backup_version_cmp(const char *a, const char *b)
{
    int x[3] = { 0, 0, 0 }, y[3] = { 0, 0, 0 };
    if (a) sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
    if (b) sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

bool backup_create(const char *data_dir, History *h, const char *dest, const BackupOptions *opt, const char *version,
                   char *out_dir, size_t out_cap, char *err, size_t err_cap)
{
    char src[PATHLEN], dst[PATHLEN], folder[PATHLEN];
    if (!dest || !path_is_dir(dest)) { seterr(err, err_cap, "The backup folder %s does not exist.", dest); return false; }
    join(src, sizeof src, data_dir, "settings.ini");
    if (!path_is_file(src)) { seterr(err, err_cap, "There are no saved settings to back up yet.", NULL); return false; }

    char st[32];
    stamp(st, sizeof st);
    char name[64];
    snprintf(name, sizeof name, "THC-backup-%s", st);
    join(folder, sizeof folder, dest, name);
    for (int n = 2; path_is_dir(folder) && n < 100; n++) {
        snprintf(name, sizeof name, "THC-backup-%s-%d", st, n);
        join(folder, sizeof folder, dest, name);
    }
    if (!mkdir_p(folder)) { seterr(err, err_cap, "Could not create %s", folder); return false; }

    join(dst, sizeof dst, folder, "settings.ini");
    if (!copy_file(src, dst)) { seterr(err, err_cap, "Could not copy the settings to %s", folder); return false; }
    join(src, sizeof src, data_dir, "mpv");
    if (path_is_dir(src)) {
        join(dst, sizeof dst, folder, "mpv");
        if (!copy_tree(src, dst, 0, "watch_later", NULL)) { seterr(err, err_cap, "Could not copy THC's MPV folder to %s", folder); return false; }
    }
    if (opt && opt->history) {
        join(dst, sizeof dst, folder, "history.db");
        join(src, sizeof src, data_dir, "history.db");
        bool ok = h ? history_backup_to(h, dst) : (path_is_file(src) && copy_file(src, dst));
        if (!ok && (h || path_is_file(src))) { seterr(err, err_cap, "Could not copy the history to %s", folder); return false; }
    }
    if (opt && opt->screenshots) {
        join(src, sizeof src, data_dir, "screenshots");
        if (path_is_dir(src)) {
            join(dst, sizeof dst, folder, "screenshots");
            if (!copy_tree(src, dst, 0, "nsfw", NULL)) { seterr(err, err_cap, "Could not copy the screenshots to %s", folder); return false; }
        }
    }
    join(dst, sizeof dst, folder, "thc-backup.txt");
    FILE *f = fopen(dst, "wb");
    if (f) {
        fprintf(f, "THC backup\nthc_version = %s\nhistory = %d\nscreenshots = %d\n", version ? version : "", opt && opt->history,
                opt && opt->screenshots);
        fclose(f);
    }
    if (out_dir && out_cap) snprintf(out_dir, out_cap, "%s", folder);
    return true;
}

typedef struct InspectCtx { BackupInfo *info; } InspectCtx;

static void inspect_cb(const char *name, bool is_dir, void *ud)
{
    (void)is_dir;
    BackupInfo *info = ((InspectCtx *)ud)->info;
    static const char *const known[] = { "thc-backup.txt", "settings.ini", "mpv", "history.db", "screenshots", NULL };
    for (int i = 0; known[i]; i++)
        if (!strcmp(name, known[i])) return;
    if (info->nskipped < 8) snprintf(info->skipped[info->nskipped++], sizeof info->skipped[0], "%.90s", name);
}

bool backup_inspect(const char *folder, const char *current_version, BackupInfo *info, char *err, size_t err_cap)
{
    memset(info, 0, sizeof *info);
    char p[PATHLEN];
    if (!folder || !path_is_dir(folder)) { seterr(err, err_cap, "That is not a folder.", NULL); return false; }
    join(p, sizeof p, folder, "settings.ini");
    if (!path_is_file(p)) {
        seterr(err, err_cap, "This folder has no settings.ini, so it does not look like a THC backup.", NULL);
        return false;
    }
    info->valid = true;
    join(p, sizeof p, folder, "thc-backup.txt");
    FILE *f = fopen(p, "rb");
    if (f) {
        char line[200];
        while (fgets(line, sizeof line, f)) {
            char v[32];
            if (sscanf(line, "thc_version = %31s", v) == 1) snprintf(info->version, sizeof info->version, "%s", v);
        }
        fclose(f);
    }
    info->newer = info->version[0] && current_version && backup_version_cmp(info->version, current_version) > 0;
    join(p, sizeof p, folder, "mpv");
    info->has_mpv = path_is_dir(p);
    if (info->has_mpv) info->mpv_files = count_files(p, 0);
    join(p, sizeof p, folder, "history.db");
    info->has_history = path_is_file(p);
    join(p, sizeof p, folder, "screenshots");
    info->has_screenshots = path_is_dir(p);
    if (info->has_screenshots) info->screenshot_files = count_files(p, 0);
    InspectCtx c = { info };
    each_entry(folder, inspect_cb, &c);
    return true;
}

#define STAGE "restore-staged"

bool backup_stage_restore(const char *data_dir, const char *folder, const BackupOptions *opt, char *err, size_t err_cap)
{
    BackupInfo info;
    if (!backup_inspect(folder, NULL, &info, err, err_cap)) return false;
    char stage[PATHLEN], s[PATHLEN], d[PATHLEN];
    join(stage, sizeof stage, data_dir, STAGE);
    if (path_is_dir(stage)) remove_tree(stage);
    if (!mkdir_p(stage)) { seterr(err, err_cap, "Could not prepare the restore in %s", stage); return false; }
    bool ok = true;
    join(s, sizeof s, folder, "settings.ini");
    join(d, sizeof d, stage, "settings.ini");
    ok = copy_file(s, d);
    if (ok && info.has_mpv) {
        join(s, sizeof s, folder, "mpv");
        join(d, sizeof d, stage, "mpv");
        ok = copy_tree(s, d, 0, "watch_later", NULL);
    }
    if (ok && opt && opt->history && info.has_history) {
        join(s, sizeof s, folder, "history.db");
        join(d, sizeof d, stage, "history.db");
        ok = copy_file(s, d);
    }
    if (ok && opt && opt->screenshots && info.has_screenshots) {
        join(s, sizeof s, folder, "screenshots");
        join(d, sizeof d, stage, "screenshots");
        ok = copy_tree(s, d, 0, "nsfw", NULL);
    }
    if (!ok) {
        remove_tree(stage);
        seterr(err, err_cap, "Could not read the backup. Nothing was changed.", NULL);
        return false;
    }
    return true;
}

bool backup_restore_pending(const char *data_dir)
{
    char p[PATHLEN], st[PATHLEN];
    join(st, sizeof st, data_dir, STAGE);
    join(p, sizeof p, st, "settings.ini");
    return path_is_file(p);
}

static void replace_tree(const char *staged, const char *target)
{
    if (path_is_dir(target)) remove_tree(target);
    if (rename(staged, target) != 0) {            /* another drive: copy instead */
        copy_tree(staged, target, 0, NULL, NULL);
    }
}

bool backup_apply_staged(const char *data_dir, char *err, size_t err_cap)
{
    if (!backup_restore_pending(data_dir)) return false;
    char stage[PATHLEN], pre[PATHLEN], s[PATHLEN], d[PATHLEN], t[32], name[64];
    join(stage, sizeof stage, data_dir, STAGE);
    stamp(t, sizeof t);
    snprintf(name, sizeof name, "pre-restore/%s", t);
    join(pre, sizeof pre, data_dir, name);
    if (!mkdir_p(pre)) { seterr(err, err_cap, "Could not save the current files in %s", pre); return false; }

    /* 1. keep what is there now */
    static const char *const files[] = { "settings.ini", "history.db", "history.db-wal", "history.db-shm", NULL };
    for (int i = 0; files[i]; i++) {
        join(s, sizeof s, data_dir, files[i]);
        join(d, sizeof d, pre, files[i]);
        if (path_is_file(s)) copy_file(s, d);
    }
    join(s, sizeof s, data_dir, "mpv");
    if (path_is_dir(s)) {
        join(d, sizeof d, pre, "mpv");
        copy_tree(s, d, 0, "watch_later", NULL);
    }

    /* 2. put the staged files in place */
    join(s, sizeof s, stage, "settings.ini");
    join(d, sizeof d, data_dir, "settings.ini");
    char tmp[PATHLEN + 16];
    snprintf(tmp, sizeof tmp, "%s.restore", d);
    if (copy_file(s, tmp)) {
        remove(d);
        rename(tmp, d);
    }
    join(s, sizeof s, stage, "mpv");
    if (path_is_dir(s)) {
        join(d, sizeof d, data_dir, "mpv");
        replace_tree(s, d);
    }
    join(s, sizeof s, stage, "history.db");
    if (path_is_file(s)) {
        join(d, sizeof d, data_dir, "history.db");
        remove(d);
        char x[PATHLEN];
        snprintf(x, sizeof x, "%s-wal", d); remove(x);
        snprintf(x, sizeof x, "%s-shm", d); remove(x);
        if (rename(s, d) != 0) { copy_file(s, d); }
    }
    join(s, sizeof s, stage, "screenshots");
    if (path_is_dir(s)) {
        join(d, sizeof d, data_dir, "screenshots");
        copy_tree(s, d, 0, "nsfw", NULL);
    }
    remove_tree(stage);
    return true;
}
