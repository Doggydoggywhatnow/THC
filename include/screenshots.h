/* screenshots.h: MPV's screenshots in THC's own folder (#25). No SDL, tested on its own. */
#ifndef THC_SCREENSHOTS_H
#define THC_SCREENSHOTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ShotInfo {
    char name[256];       /* file name only */
    int64_t mtime;
    int64_t size;
    char title[160];      /* the video title taken from the name */
    char at[16];          /* "1:02:03" or "" if the name has no time */
} ShotInfo;

/* The folder: `setting` if given, else <data_dir>/screenshots. */
void screenshot_dir_resolve(const char *data_dir, const char *setting, char *out, size_t cap);
/* "png", "jpg" or "webp" for the setting value. */
const char *screenshot_format_name(int format);
/* The MPV template: the setting, or the default when empty. */
const char *screenshot_template(const char *setting);
/* Is it an image file THC lists (png, jpg, jpeg, webp)? */
bool screenshot_is_image(const char *name);
/* Split "Title-00_12_34.567.png" (the name MPV makes from the default template) into a title and "12:34". */
void screenshot_parse_name(const char *name, char *title, size_t title_cap, char *at, size_t at_cap);
/* The images in `dir`, newest first (not other files, not sub-folders). Returns how many were stored. */
int screenshots_list(const char *dir, ShotInfo *out, int max);
/* May THC delete `path`? Only a plain file directly inside `dir`, with no ".." tricks. */
bool screenshot_path_in_dir(const char *dir, const char *path);
/* Delete a screenshot if the path check passes. */
bool screenshot_delete(const char *dir, const char *path);

/* <screenshots folder>/nsfw: where pictures taken while an NSFW item plays go. */
void screenshot_nsfw_dir(const char *dir, char *out, size_t cap);
/* Delete the plain files directly inside `dir` (not sub-folders). Returns how many went. */
int files_remove_in_dir(const char *dir);

#endif
