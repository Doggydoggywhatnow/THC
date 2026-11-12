#include "mpvconf.h"
#include "screenshots.h"
#include "settings.h"
#include "test_util.h"

#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#define MK(p) _mkdir(p)
#include <io.h>
#else
#include <unistd.h>
#include <utime.h>
#define MK(p) mkdir(p, 0777)
#endif

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static void age(const char *path, long secs_ago)
{
#ifndef _WIN32
    struct utimbuf t;
    t.actime = t.modtime = time(NULL) - secs_ago;
    utime(path, &t);
#else
    (void)path; (void)secs_ago;
#endif
}

int main(void)
{
    char d[300];
    screenshot_dir_resolve("/data/THC/", "", d, sizeof d);
    CHECK_STR(d, "/data/THC/screenshots");
    screenshot_dir_resolve("/data/THC", "", d, sizeof d);
    CHECK_STR(d, "/data/THC/screenshots");
    screenshot_dir_resolve("/data/THC/", "/home/me/Pictures/shots", d, sizeof d);   /* a custom folder works */
    CHECK_STR(d, "/home/me/Pictures/shots");
    CHECK_STR(screenshot_format_name(SHOT_PNG), "png");
    CHECK_STR(screenshot_format_name(SHOT_JPG), "jpg");
    CHECK_STR(screenshot_format_name(SHOT_WEBP), "webp");
    CHECK_STR(screenshot_template(""), "%{media-title}-%P");
    CHECK_STR(screenshot_template("%F"), "%F");

    /* names: title and time */
    char title[160], at[16];
    screenshot_parse_name("Show S01E02-00:12:34.567.png", title, sizeof title, at, sizeof at);
    CHECK_STR(title, "Show S01E02");
    CHECK_STR(at, "12:34");
    screenshot_parse_name("A Film_-01_02_03.100.jpg", title, sizeof title, at, sizeof at);
    CHECK_STR(at, "1:02:03");
    screenshot_parse_name("plain.png", title, sizeof title, at, sizeof at);
    CHECK_STR(title, "plain");
    CHECK_STR(at, "");

    /* the folder: newest first, images only */
    if (system("rm -rf sh_dir sh_media")) {}
    MK("sh_dir"); MK("sh_dir/nsfw"); MK("sh_dir/sub.png"); MK("sh_media");
    put("sh_dir/old.png", "x");
    put("sh_dir/new.jpg", "x");
    put("sh_dir/mid.WEBP", "x");
    put("sh_dir/notes.txt", "x");
    put("sh_dir/nsfw/hidden.png", "x");            /* the NSFW sub-folder is not part of this list */
    put("sh_media/movie.mkv", "MEDIA");
    age("sh_dir/old.png", 3000);
    age("sh_dir/mid.WEBP", 2000);
    age("sh_dir/new.jpg", 10);
    ShotInfo s[16];
    int n = screenshots_list("sh_dir", s, 16);
    CHECK(n == 3);                                  /* no .txt, no folder named .png, nothing from nsfw/ */
#ifndef _WIN32
    CHECK(n == 3 && !strcmp(s[0].name, "new.jpg") && !strcmp(s[1].name, "mid.WEBP") && !strcmp(s[2].name, "old.png"));
#endif
    CHECK(screenshots_list("no_such_dir", s, 16) == 0);
    CHECK(screenshots_list("sh_dir", s, 2) == 2);

    /* delete only inside the folder */
    CHECK(screenshot_path_in_dir("sh_dir", "sh_dir/old.png"));
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_dir/../sh_media/movie.mkv"));
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_dir/../sh_dir/old.png"));
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_media/movie.mkv"));
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_dir/nsfw/hidden.png"));        /* not a direct child */
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_dir/notes.txt"));              /* not an image */
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_dirx/old.png"));               /* a sibling with the same prefix */
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_dir"));
    CHECK(!screenshot_path_in_dir("sh_dir", "sh_dir/"));
    CHECK(!screenshot_path_in_dir("", "x.png") && !screenshot_path_in_dir("sh_dir", NULL));
    CHECK(!screenshot_delete("sh_dir", "sh_media/movie.mkv"));
    CHECK(!screenshot_delete("sh_dir", "sh_dir/../sh_media/movie.mkv"));
    CHECK(screenshot_delete("sh_dir", "sh_dir/old.png"));
    CHECK(screenshots_list("sh_dir", s, 16) == 2);
    /* the media file is untouched */
    FILE *f = fopen("sh_media/movie.mkv", "rb");
    CHECK(f != NULL);
    if (f) fclose(f);

    /* NSFW screenshots go to their own folder, which the Screenshots tab's flat listing never reads, and are cleared on exit */
    screenshot_nsfw_dir("/data/THC/screenshots", d, sizeof d);
    CHECK_STR(d, "/data/THC/screenshots/nsfw");
    if (system("mkdir -p sh_dir2/nsfw/keep && printf x > sh_dir2/nsfw/a.png && printf x > sh_dir2/nsfw/b.jpg && printf x > sh_dir2/nsfw/keep/c.png && printf x > sh_dir2/top.png")) {}
    CHECK(screenshots_list("sh_dir2", s, 16) == 1);                                    /* only top.png: nothing from nsfw/ */
    CHECK(files_remove_in_dir("sh_dir2/nsfw") == 2);
    CHECK(files_remove_in_dir("sh_dir2/nsfw") == 0);                                   /* again: nothing left, safe */
    CHECK(screenshots_list("sh_dir2/nsfw", s, 16) == 0);
    FILE *kf = fopen("sh_dir2/nsfw/keep/c.png", "rb");
    CHECK(kf != NULL);                                                                 /* sub-folders are left alone */
    if (kf) fclose(kf);
    FILE *tf = fopen("sh_dir2/top.png", "rb");
    CHECK(tf != NULL);                                                                 /* and nothing outside the folder */
    if (tf) fclose(tf);
    if (system("rm -rf sh_dir2")) {}
    CHECK(files_remove_in_dir("no_such_dir") == 0);

    /* MPV: these three options are THC's, not mpv.conf's */
    CHECK(mpvconf_is_thc_controlled("screenshot-directory"));
    CHECK(mpvconf_is_thc_controlled("screenshot-format"));
    CHECK(mpvconf_is_thc_controlled("screenshot-template"));
    CHECK(!mpvconf_is_thc_controlled("screenshot-jpeg-quality"));

    /* settings round trip */
    Settings a, b;
    settings_defaults(&a);
    CHECK(a.screenshot_format == SHOT_PNG && !a.screenshot_dir[0] && !a.screenshot_template[0]);
    CHECK_STR(k_actions[ACT_PORTAL_SHOTS].def, "4");
    snprintf(a.screenshot_dir, sizeof a.screenshot_dir, "/tmp/my shots");
    snprintf(a.screenshot_template, sizeof a.screenshot_template, "%%F-%%wH");
    a.screenshot_format = SHOT_WEBP;
    snprintf(a.hotkey[ACT_PORTAL_SHOTS], sizeof a.hotkey[0], "F6");
    CHECK(settings_save(&a, "sh_settings.ini"));
    settings_defaults(&b);
    CHECK(settings_load(&b, "sh_settings.ini"));
    CHECK_STR(b.screenshot_dir, "/tmp/my shots");
    CHECK_STR(b.screenshot_template, "%F-%wH");
    CHECK(b.screenshot_format == SHOT_WEBP);
    CHECK_STR(b.hotkey[ACT_PORTAL_SHOTS], "F6");
    CHECK(b.unknown[0] == 0);                       /* the new keys are known, not "unknown" */
    remove("sh_settings.ini");
    if (system("rm -rf sh_dir sh_media")) {}
    TEST_DONE();
}
