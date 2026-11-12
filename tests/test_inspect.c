/* thc-inspect against a fixture data folder: reports, JSON, exit codes, the imports check on a bad
 * program, and the media fingerprint. POSIX only (it runs the tool through the shell). */
#include "history.h"
#include "test_util.h"

#ifdef _WIN32
int main(void) { printf("SKIP: not built for Windows\n"); return 0; }
#else
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INSPECT_CLEAN
#error INSPECT_CLEAN
#endif
#ifndef INSPECT_BIN
#error INSPECT_BIN
#endif

static char out[65536];

/* Run a command, capture stdout, return the exit code. */
static int run(const char *fmt, ...)
{
    char cmd[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    FILE *p = popen(cmd, "r");
    size_t n = 0;
    if (p) {
        n = fread(out, 1, sizeof out - 1, p);
        int st = pclose(p);
        out[n] = 0;
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }
    out[0] = 0;
    return -1;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

int main(void)
{
    if (system("rm -rf insp_fx insp_media insp_snap.txt; mkdir -p insp_fx/mpv insp_fx/screenshots insp_media/sub")) {}

    /* a history database with a few rows */
    char err[256];
    History *h = history_open("insp_fx/history.db", err, sizeof err);
    CHECK(h != NULL);
    if (h) {
        history_save(h, "Shows/A/a.mkv", 1000, 120, 600, 0.9, false);
        history_save(h, "Shows/A/b.mkv", 2000, 600, 600, 0.9, true);
        history_close(h);
    }
    write_file("insp_fx/settings.ini", "mpv_block_network=true\n");
    write_file("insp_fx/screenshots/Show-00_01_02.png", "png");

    /* clean data, the tool's own program (no network imports): pass */
    int rc = run("%s --data insp_fx --exe %s", INSPECT_BIN, INSPECT_CLEAN);
    CHECK(rc == 0);
    CHECK(strstr(out, "history.db") && strstr(out, "schema version: 3") && strstr(out, "history: 2 rows"));
    CHECK(strstr(out, "Result: pass"));
    CHECK(strstr(out, "screenshots/Show-00_01_02.png"));     /* screenshots are listed with the other files THC writes (#25) */
    CHECK(!strstr(out, "Shows/A/a.mkv"));                  /* titles stay hidden by default */
    CHECK(run("%s --data insp_fx --exe %s --show-titles", INSPECT_BIN, INSPECT_CLEAN) == 0 && strstr(out, "Shows/A/a.mkv"));

    /* JSON */
    rc = run("%s --data insp_fx --exe %s --json", INSPECT_BIN, INSPECT_CLEAN);
    CHECK(rc == 0 && out[0] == '{' && strstr(out, "\"result\":\"pass\"") && strstr(out, "\"findings\":0"));
    CHECK(strstr(out, "\"name\":\"History database\""));

    /* "Block network" off and an online mpv.conf: findings, exit 1 */
    write_file("insp_fx/settings.ini", "mpv_block_network=false\n");
    write_file("insp_fx/mpv/mpv.conf", "volume=50\nytdl=yes\n");
    rc = run("%s --data insp_fx --exe %s", INSPECT_BIN, INSPECT_CLEAN);
    CHECK(rc == 1 && strstr(out, "Block network in MPV\" is off") && strstr(out, "line 2 (ytdl)"));
    /* with the block on, the same mpv.conf is only a note */
    write_file("insp_fx/settings.ini", "mpv_block_network=true\n");
    rc = run("%s --data insp_fx --exe %s", INSPECT_BIN, INSPECT_CLEAN);
    CHECK(rc == 0 && strstr(out, "THC's block overrides it"));

    /* a program that imports socket() fails the check */
    rc = run("%s --data insp_fx --exe %s", INSPECT_BIN, INSPECT_NETUSER);
    CHECK(rc == 1 && strstr(out, "imports a networking function: socket"));

    /* the tool only reads: the fixture is unchanged */
    CHECK(run("cat insp_fx/settings.ini") == 0 && strstr(out, "mpv_block_network=true"));

    /* usage errors exit 2 */
    CHECK(run("%s --bogus 2>/dev/null", INSPECT_BIN) == 2);
    CHECK(run("%s --media insp_media 2>/dev/null", INSPECT_BIN) == 2);
    CHECK(run("%s --snapshot x 2>/dev/null", INSPECT_BIN) == 2);

    /* media fingerprint: untouched -> pass; edited, added, removed -> findings */
    write_file("insp_media/a.mkv", "aaaaaaaaaa");
    write_file("insp_media/sub/b.mp3", "bbbbbbbbbbbb");
    CHECK(run("%s --media insp_media --snapshot insp_snap.txt", INSPECT_BIN) == 0);
    CHECK(run("%s --media insp_media --compare insp_snap.txt", INSPECT_BIN) == 0 && strstr(out, "exactly as it was"));
    write_file("insp_media/a.mkv", "aaaaaaaaaX");                       /* same size, one byte changed */
    rc = run("%s --media insp_media --compare insp_snap.txt", INSPECT_BIN);
    CHECK(rc == 1 && strstr(out, "Changed: a.mkv"));
    write_file("insp_media/a.mkv", "aaaaaaaaaa");
    write_file("insp_media/new.flac", "n");
    unlink("insp_media/sub/b.mp3");
    rc = run("%s --media insp_media --compare insp_snap.txt", INSPECT_BIN);
    CHECK(rc == 1 && strstr(out, "New: new.flac") && strstr(out, "Missing now: sub/b.mp3"));

    if (system("rm -rf insp_fx insp_media insp_snap.txt")) {}
    TEST_DONE();
}
#endif
