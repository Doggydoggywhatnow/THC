/* backup.h: back up and restore THC's settings and MPV setup (and optionally history and screenshots).
 *
 * A backup is a plain folder, so users can zip or sync it however they like:
 *   thc-backup.txt   what it is and which THC wrote it
 *   settings.ini
 *   mpv/             mpv.conf, input.conf, script-opts/, scripts/, shaders/
 *   history.db       (optional, copied with SQLite's backup function)
 *   screenshots/     (optional)
 * Never media files, thumbnails or other caches. No SDL here, so it is tested on its own.
 *
 * Restore is staged: it copies the chosen folder into <data>/restore-staged/ and the files are applied
 * at the next start (before the history database is opened), after the current files were saved into
 * <data>/pre-restore/<time>/. Only the known names above are ever read from a backup. */
#ifndef THC_BACKUP_H
#define THC_BACKUP_H

#include "history.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct BackupOptions {
    bool history;
    bool screenshots;
} BackupOptions;

typedef struct BackupInfo {
    bool valid;              /* has a settings.ini */
    bool has_mpv, has_history, has_screenshots;
    char version[32];        /* THC version that wrote it, "" if unknown */
    bool newer;              /* written by a newer THC than this one */
    int mpv_files, screenshot_files;
    char skipped[8][96];     /* unexpected entries that restore will ignore */
    int nskipped;
} BackupInfo;

/* Create <dest>/THC-backup-<date>-<time>/ . `out_dir` receives its path. `h` may be NULL (then history is
 * copied as a file if it is asked for). Returns false and fills `err` on failure. */
bool backup_create(const char *data_dir, History *h, const char *dest, const BackupOptions *opt, const char *version,
                   char *out_dir, size_t out_cap, char *err, size_t err_cap);

/* Look into a backup folder. Returns false (with `err`) if it is not a usable backup. */
bool backup_inspect(const char *folder, const char *current_version, BackupInfo *info, char *err, size_t err_cap);

/* Stage a restore of `folder` (the parts chosen in `opt`; settings and mpv are always included). */
bool backup_stage_restore(const char *data_dir, const char *folder, const BackupOptions *opt, char *err, size_t err_cap);

/* Is a staged restore waiting? */
bool backup_restore_pending(const char *data_dir);

/* At start, before settings and history are opened: save the current files into pre-restore/ and put the
 * staged ones in place. Returns true if something was applied. */
bool backup_apply_staged(const char *data_dir, char *err, size_t err_cap);

/* Compare "x.y.z" versions: <0, 0, >0. */
int backup_version_cmp(const char *a, const char *b);

#endif
