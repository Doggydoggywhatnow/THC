/* nsfw.h: tags that move files, folders and whole library folders into the NSFW portal (#13).
 *
 * Presentation only: tags live in THC's history.db, nothing on disk is touched. This is the ONE place that
 * decides whether something is NSFW (`nsfw_hidden`); every view reads the library through trees that were
 * filtered with it (see fs_filter), so no view can forget a rule. No SDL, so it is tested on its own.
 *
 * Resolution, nearest first:
 *   1. a tag on the item itself (a file by path and size, or a folder by path),
 *   2. a tag on a parent folder, nearest parent first (an explicit "not NSFW" beats everything above it),
 *   3. if nothing was tagged: the library folder is marked NSFW, or a rule matches a folder or file name.
 * Rules never override an explicit "not NSFW". With the portal switched off nothing is NSFW. */
#ifndef THC_NSFW_H
#define THC_NSFW_H

#include "fstree.h"
#include "history.h"

#include <stdbool.h>
#include <stdint.h>

enum { NSFW_SCOPE_FILE = 0, NSFW_SCOPE_FOLDER = 1 };
enum { NSFW_WHY_NONE = 0, NSFW_WHY_ITEM, NSFW_WHY_FOLDER, NSFW_WHY_LIBRARY, NSFW_WHY_RULE, NSFW_WHY_EXPLICIT_NOT };

typedef struct NsfwConfig {
    bool enabled;            /* the portal is on; off means nothing is hidden */
    bool rules_enabled;
    char rules[256];         /* names or patterns separated by "|" (case-insensitive, * and ? work) */
    unsigned root_mask;      /* bit i: library root i is treated as NSFW */
} NsfwConfig;

typedef struct NsfwIndex NsfwIndex;

NsfwIndex *nsfw_index_new(void);
void nsfw_index_free(NsfwIndex *x);
void nsfw_index_config(NsfwIndex *x, const NsfwConfig *cfg);
/* Replace the tags with the ones in the database. */
bool nsfw_index_load(NsfwIndex *x, History *h);
/* A path that no longer exists in the library lets a file tag follow a moved file (same rule as history, #24).
 * With no check, file tags never follow a move. */
void nsfw_index_set_exists_check(NsfwIndex *x, bool (*exists)(void *ud, int root, const char *rel), void *ud);

/* Tag or untag. `state`: 1 NSFW, 0 explicitly not NSFW. They write through to `h` (may be NULL: memory only). */
bool nsfw_tag(NsfwIndex *x, History *h, int root, int scope, const char *rel, int64_t size, int state);
/* Remove a tag, so the item goes back to inheriting. */
bool nsfw_untag(NsfwIndex *x, History *h, int root, int scope, const char *rel, int64_t size);
int nsfw_tag_count(const NsfwIndex *x);
int nsfw_tag_count_state(const NsfwIndex *x, int state);

/* THE decision. `size` is only used for files. `why` (may be NULL) says which rule decided. */
bool nsfw_hidden(const NsfwIndex *x, int root, const char *rel, bool is_dir, int64_t size, int *why);

/* May this item leave history, plays or a saved backlog behind? Nothing is recorded for an NSFW item unless the user
 * turned that on (`record_nsfw`), and never when the portal's history switch for that kind of media is off. */
bool nsfw_record_allowed(const NsfwIndex *x, bool history_on_for_kind, bool record_nsfw, int root, const char *rel, int64_t size);
/* The name a thumbnail is cached under: "<hex>" normally, "nsfw/<hex>" for NSFW items (a folder of its own). */
void nsfw_thumb_name(const char *hexkey, bool nsfw, char *out, size_t cap);

/* Does a name match a rule pattern ("*xxx*", "NSFW")? Case-insensitive. */
bool nsfw_pattern_match(const char *pattern, const char *name);
/* Does any rule in a "|"-separated list match any folder or file name along `rel`? */
bool nsfw_rules_match(const char *rules, const char *rel);

/* Import and export the tag list as plain text: "<root-name>\t<f|d>\t<state>\t<size>\t<relative path>" per line.
 * Callers must have the lock open before they offer these. Returns the number of lines. */
int nsfw_export(const NsfwIndex *x, FILE *f);
int nsfw_import(NsfwIndex *x, History *h, FILE *f);

/* Filtered copies of a folder tree: want_hidden = false keeps the files that are NOT NSFW (the normal
 * portals), true keeps only the NSFW ones (the NSFW portal). Folders are kept while they hold a kept file.
 * The returned root is always a folder (empty if nothing was kept). Callbacks decide per node. */
FsNode *fs_filter(const FsNode *src, bool (*hidden)(void *ud, const FsNode *n), void *ud, bool want_hidden);

#endif
