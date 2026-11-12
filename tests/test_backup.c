#include "backup.h"
#include "test_util.h"

#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#define MK(p) _mkdir(p)
#include <direct.h>
#else
#define MK(p) mkdir(p, 0777)
#endif

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static bool exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void slurp(const char *p, char *buf, size_t cap)
{
    FILE *f = fopen(p, "rb");
    size_t n = f ? fread(buf, 1, cap - 1, f) : 0;
    buf[n] = 0;
    if (f) fclose(f);
}

static void rmrf(const char *p)
{
    char cmd[600];
#ifdef _WIN32
    snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\" 2>nul", p);
#else
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", p);
#endif
    if (system(cmd)) {}
}

int main(void)
{
    rmrf("bk_data"); rmrf("bk_dest"); rmrf("bk_media");
    MK("bk_data"); MK("bk_data/mpv"); MK("bk_data/mpv/scripts"); MK("bk_data/mpv/script-opts"); MK("bk_data/mpv/watch_later");
    MK("bk_data/thumbs"); MK("bk_data/screenshots"); MK("bk_data/screenshots/nsfw"); MK("bk_dest"); MK("bk_media");
    put("bk_data/settings.ini", "volume = 40\nfuture_key = 1\nnsfw_dir = /mnt/Private Stash\nnsfw_rules = NSFW|*xxx*\n");
    put("bk_data/mpv/mpv.conf", "volume=50\n");
    put("bk_data/mpv/input.conf", "F9 show-text hi\n");
    put("bk_data/mpv/scripts/a.lua", "-- a\n");
    put("bk_data/mpv/script-opts/osc.conf", "layout=topbar\n");
    put("bk_data/mpv/watch_later/zzz", "x");
    put("bk_data/thumbs/t.jpg", "thumb");
    put("bk_data/screenshots/s1.png", "png");
    put("bk_data/screenshots/nsfw/hidden.png", "png");
    put("bk_media/movie.mkv", "MEDIA");

    char err[256] = "", out[1024];
    History *h = history_open("bk_data/history.db", err, sizeof err);
    CHECK(h != NULL);
    if (h) history_save(h, "Shows/A/1.mkv", 1000, 100, 600, 0.9, false);

    /* back up settings and MPV only */
    BackupOptions none = { false, false };
    CHECK(backup_create("bk_data", h, "bk_dest", &none, "0.3.0", out, sizeof out, err, sizeof err));
    char p[1200];
    snprintf(p, sizeof p, "%s/settings.ini", out);
    CHECK(exists(p));
    {   /* the NSFW folder setting (#34) travels with the settings, and so do the old rule keys */
        char sp[1200], buf[512] = { 0 };
        snprintf(sp, sizeof sp, "%s/settings.ini", out);
        FILE *sf = fopen(sp, "rb");
        if (sf) { size_t n = fread(buf, 1, sizeof buf - 1, sf); buf[n] = 0; fclose(sf); }
        CHECK(strstr(buf, "nsfw_dir = /mnt/Private Stash") && strstr(buf, "nsfw_rules = NSFW|*xxx*"));
    }
    snprintf(p, sizeof p, "%s/mpv/mpv.conf", out);
    CHECK(exists(p));
    snprintf(p, sizeof p, "%s/mpv/scripts/a.lua", out);
    CHECK(exists(p));
    snprintf(p, sizeof p, "%s/mpv/script-opts/osc.conf", out);
    CHECK(exists(p));
    snprintf(p, sizeof p, "%s/mpv/watch_later/zzz", out);
    CHECK(!exists(p));                                       /* mpv's own resume files are not part of a backup */
    snprintf(p, sizeof p, "%s/history.db", out);
    CHECK(!exists(p));                                       /* history only when chosen */
    snprintf(p, sizeof p, "%s/thumbs", out);
    CHECK(!exists(p));                                       /* never caches */
    snprintf(p, sizeof p, "%s/screenshots", out);
    CHECK(!exists(p) && !exists("bk_dest/thumbs"));          /* screenshots off by default, nothing outside the backup */

    /* with history and screenshots: the copy is a valid database, the NSFW screenshots stay out */
    BackupOptions all = { true, true };
    char out2[1024];
    CHECK(backup_create("bk_data", h, "bk_dest", &all, "0.3.0", out2, sizeof out2, err, sizeof err));
    CHECK(strcmp(out, out2) != 0);                           /* a second backup in the same second gets its own folder */
    /* inspect first: opening the copy below may leave SQLite's -wal and -shm files next to it */
    BackupInfo info;
    CHECK(backup_inspect(out2, "0.3.0", &info, err, sizeof err));
    CHECK(info.valid && info.has_mpv && info.has_history && info.has_screenshots && !info.newer);
    CHECK_STR(info.version, "0.3.0");
    CHECK(info.nskipped == 0);
    snprintf(p, sizeof p, "%s/history.db", out2);
    History *copy = history_open(p, err, sizeof err);
    CHECK(copy != NULL);
    HistoryEntry e;
    CHECK(copy && history_get(copy, "Shows/A/1.mkv", 1000, &e) && e.position == 100);
    if (copy) history_close(copy);
    snprintf(p, sizeof p, "%s/screenshots/s1.png", out2);
    CHECK(exists(p));
    snprintf(p, sizeof p, "%s/screenshots/nsfw", out2);
    CHECK(!exists(p));


    /* a folder with no settings.ini is refused with a clear message */
    CHECK(!backup_inspect("bk_media", "0.3.0", &info, err, sizeof err));
    CHECK(strstr(err, "settings.ini"));
    CHECK(!backup_stage_restore("bk_data", "bk_media", &all, err, sizeof err));
    CHECK(!backup_restore_pending("bk_data"));
    CHECK(!backup_inspect("no_such_folder", "0.3.0", &info, err, sizeof err));
    CHECK(!backup_create("bk_data", h, "no_such_dest", &none, "0.3.0", out, sizeof out, err, sizeof err));

    /* restore: staged, nothing changes until the next start */
    if (h) { history_close(h); h = NULL; }
    put("bk_data/settings.ini", "volume = 99\n");             /* the user kept changing things */
    put("bk_data/mpv/mpv.conf", "volume=1\n");
    CHECK(backup_stage_restore("bk_data", out2, &all, err, sizeof err));
    CHECK(backup_restore_pending("bk_data"));
    char buf[256];
    slurp("bk_data/settings.ini", buf, sizeof buf);
    CHECK(strstr(buf, "volume = 99"));                       /* untouched until applied */
    CHECK(backup_apply_staged("bk_data", err, sizeof err));
    CHECK(!backup_restore_pending("bk_data"));
    slurp("bk_data/settings.ini", buf, sizeof buf);
    CHECK(strstr(buf, "volume = 40") && strstr(buf, "future_key = 1"));
    slurp("bk_data/mpv/mpv.conf", buf, sizeof buf);
    CHECK(strstr(buf, "volume=50"));
    History *r = history_open("bk_data/history.db", err, sizeof err);
    CHECK(r && history_get(r, "Shows/A/1.mkv", 1000, &e) && e.position == 100);
    if (r) history_close(r);
    CHECK(exists("bk_data/screenshots/s1.png"));
    /* the current files were saved first */
    {
        bool found = false;
        char pre[300];
#ifndef _WIN32
        FILE *ls = popen("ls bk_data/pre-restore", "r");
        if (ls) {
            char d[64];
            if (fgets(d, sizeof d, ls)) {
                d[strcspn(d, "\r\n")] = 0;
                snprintf(pre, sizeof pre, "bk_data/pre-restore/%s/settings.ini", d);
                slurp(pre, buf, sizeof buf);
                found = strstr(buf, "volume = 99") != NULL;
                snprintf(pre, sizeof pre, "bk_data/pre-restore/%s/mpv/mpv.conf", d);
                slurp(pre, buf, sizeof buf);
                found = found && strstr(buf, "volume=1") != NULL;
            }
            pclose(ls);
        }
        CHECK(found);
#else
        (void)pre;
        (void)found;
#endif
    }

    /* a crafted backup: unexpected names are ignored and reported; nothing leaves the data folder */
    rmrf("bk_evil");
    MK("bk_evil"); MK("bk_evil/mpv"); MK("bk_evil/..x"); MK("bk_outside");
    put("bk_evil/settings.ini", "volume = 11\n");
    put("bk_evil/mpv/mpv.conf", "ok=1\n");
    put("bk_evil/evil.sh", "rm -rf /\n");
    put("bk_evil/thumbs", "not a folder");
    put("bk_evil/thc-backup.txt", "THC backup\nthc_version = 9.9.9\n");
    CHECK(backup_inspect("bk_evil", "0.3.0", &info, err, sizeof err));
    CHECK(info.newer && info.nskipped == 3);                 /* evil.sh, thumbs, ..x */
    BackupOptions hist_only = { true, false };
    CHECK(backup_stage_restore("bk_data", "bk_evil", &hist_only, err, sizeof err));
    CHECK(backup_apply_staged("bk_data", err, sizeof err));
    slurp("bk_data/settings.ini", buf, sizeof buf);
    CHECK(strstr(buf, "volume = 11"));                       /* a newer backup restores what it can */
    CHECK(!exists("bk_data/evil.sh") && !exists("bk_evil_out"));
    slurp("bk_data/thumbs/t.jpg", buf, sizeof buf);
    CHECK_STR(buf, "thumb");                                  /* the "thumbs" file in the backup did not replace the folder */
    CHECK(exists("bk_data/history.db"));                     /* a backup without history keeps the current one */
    CHECK(!exists("bk_data/screenshots/evil"));

    /* media is never touched */
    slurp("bk_media/movie.mkv", buf, sizeof buf);
    CHECK_STR(buf, "MEDIA");

    CHECK(backup_version_cmp("0.3.0", "0.2.9") > 0 && backup_version_cmp("0.3.0", "0.3.0") == 0 &&
          backup_version_cmp("0.2.0", "1.0.0") < 0 && backup_version_cmp("", "0.1.0") < 0);

    rmrf("bk_data"); rmrf("bk_dest"); rmrf("bk_media"); rmrf("bk_evil"); rmrf("bk_outside");
    TEST_DONE();
}
