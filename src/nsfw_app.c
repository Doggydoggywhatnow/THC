/* nsfw_app.c: connects the NSFW tags (core) to the app's library. The normal portals read a->lib, which is a
 * filtered copy without NSFW items; the NSFW portal reads a->nlib, which holds only them. Both are rebuilt from
 * a->full after every scan and every tag change, so no view has its own filter to forget (#13). */
#include "app.h"
#include "pin.h"
#include "screenshots.h"

typedef struct FilterCtx { App *a; int root; } FilterCtx;

static bool node_hidden(void *ud, const FsNode *n)
{
    FilterCtx *c = ud;
    char rel[THC_PATH_MAX];
    fs_relpath(n, rel, sizeof rel);
    return nsfw_hidden(c->a->nsfw, c->root, rel, n->dir, n->size, NULL);
}

bool app_nsfw_hidden(App *a, int root, const FsNode *n)
{
    FilterCtx c = { a, root };
    return node_hidden(&c, n);
}

/* Is this path still somewhere in the library? (a scan in progress, or no library yet, answers yes) */
static bool tag_exists(void *ud, int root, const char *rel)
{
    App *a = ud;
    if (a->scanning) return true;
    if (root < 0 || root >= ROOT_COUNT || !a->full.root[root]) return true;
    return fs_find(a->full.root[root], rel) != NULL;
}

void app_nsfw_init(App *a)
{
    a->nsfw = nsfw_index_new();
    nsfw_index_set_exists_check(a->nsfw, tag_exists, a);
    nsfw_index_load(a->nsfw, a->hist);
    app_nsfw_sync(a);
    a->nlib_version = (Uint64)1 << 40;      /* never equal to lib_version, so screen caches rebuild when the view swaps */
    app_nsfw_lock(a);
}

void app_nsfw_sync(App *a)
{
    if (!a->nsfw) return;
    NsfwConfig c;
    memset(&c, 0, sizeof c);
    c.enabled = a->set.nsfw_enabled;
    c.dir_set = a->set.nsfw_dir[0] != 0;
    c.dir_root = ROOT_NSFW;
    c.root_mask = (unsigned)a->set.nsfw_roots;
    nsfw_index_config(a->nsfw, &c);
}

static void drop_filtered(App *a)
{
    if (a->lib_filtered) {
        library_free(&a->lib);
        library_free(&a->nlib);
    }
    memset(&a->lib, 0, sizeof a->lib);
    memset(&a->nlib, 0, sizeof a->nlib);
    a->lib_filtered = false;
}

void app_nsfw_apply(App *a)
{
    tagcache_shutdown();                       /* it holds pointers into the trees that are about to go */
    a->focus_file = NULL;
    drop_filtered(a);
    app_nsfw_sync(a);
    if (!a->set.nsfw_enabled || !a->nsfw) {
        a->lib = a->full;                      /* the same trees: nothing is hidden, nothing is copied */
        a->lib.root[ROOT_NSFW] = NULL;         /* except the NSFW folder: with the portal off it is not shown anywhere */
        return;
    }
    for (int r = 0; r < ROOT_COUNT; r++) {
        SDL_strlcpy(a->lib.path[r], a->full.path[r], THC_PATH_MAX);
        SDL_strlcpy(a->nlib.path[r], a->full.path[r], THC_PATH_MAX);
        if (!a->full.root[r]) continue;
        FilterCtx c = { a, r };
        a->lib.root[r] = fs_filter(a->full.root[r], node_hidden, &c, false);
        a->nlib.root[r] = fs_filter(a->full.root[r], node_hidden, &c, true);
    }
    a->lib_filtered = true;
}

void app_library_changed(App *a)
{
    app_nsfw_apply(a);
    a->lib_version++;
    app_rebuild_library(a);
    tagcache_start(a);
    app_history_changed();
}

void app_free_library(App *a)
{
    drop_filtered(a);
    library_free(&a->full);
}

void app_nsfw_shutdown(App *a)
{
    nsfw_index_free(a->nsfw);
    a->nsfw = NULL;
}

/* ------------------------------------------------------------------------ */
/* The NSFW portal's view                                                     */
/* ------------------------------------------------------------------------ */

/* swap two objects of the same type (portable: no typeof) */
#define SWAP(x, y)                                  \
    do {                                            \
        unsigned char tmp_[sizeof(x)];              \
        memcpy(tmp_, &(x), sizeof(x));              \
        memcpy(&(x), &(y), sizeof(x));              \
        memcpy(&(y), tmp_, sizeof(x));              \
    } while (0)

bool app_nsfw_tab_visible(const App *a)
{
    return a->set.nsfw_enabled && a->set.nsfw_tab == NSFW_TAB_VISIBLE;
}

static void swap_view(App *a)
{
    SWAP(a->lib, a->nlib);
    SWAP(a->vlib, a->nvlib);
    SWAP(a->vnav, a->nvnav);
    SWAP(a->mnav, a->nmnav);
    SWAP(a->bnav, a->nbnav);
    SWAP(a->lib_version, a->nlib_version);
    a->nsfw_view = !a->nsfw_view;
}

void app_nsfw_view_enter(App *a)
{
    if (!a->nsfw_view) swap_view(a);
}

void app_nsfw_view_leave(App *a)
{
    if (a->nsfw_view) swap_view(a);
}

/* ------------------------------------------------------------------------ */
/* Marking                                                                    */
/* ------------------------------------------------------------------------ */

void app_nsfw_mark(App *a, int root, int scope, const char *rel, int64_t size, bool nsfw)
{
    if (!a->set.nsfw_enabled || !a->nsfw || !rel || !*rel) return;
    /* what was there before, so Undo can put it back: a tag of its own, or nothing (inherits) */
    bool was_hidden = nsfw_hidden(a->nsfw, root, rel, scope == NSFW_SCOPE_FOLDER, size, NULL);
    /* an explicit tag on the item itself: it beats anything inherited, in both directions */
    nsfw_tag(a->nsfw, a->hist, root, scope, rel, size, nsfw ? 1 : 0);
    int prev = was_hidden ? 1 : 0;
    a->undo.active = true;
    a->undo.root = root;
    a->undo.scope = scope;
    a->undo.prev = prev;
    a->undo.size = size;
    SDL_strlcpy(a->undo.rel, rel, sizeof a->undo.rel);
    a->undo.until = a->ui.time + 6.0;
    player_purge_nsfw(a, false);
    a->nsfw_refresh = true;
    app_toast(a, nsfw ? "Marked as NSFW" : "Removed from NSFW");
    a->toast_until = a->ui.time + 6.0;
}

void app_nsfw_undo(App *a)
{
    if (!a->undo.active || !a->nsfw) return;
    a->undo.active = false;
    /* the previous state is "NSFW or not"; restoring it explicitly is always correct, and removing the tag
     * where it simply inherited keeps the list short */
    nsfw_untag(a->nsfw, a->hist, a->undo.root, a->undo.scope, a->undo.rel, a->undo.size);
    bool now = nsfw_hidden(a->nsfw, a->undo.root, a->undo.rel, a->undo.scope == NSFW_SCOPE_FOLDER, a->undo.size, NULL);
    if (now != (a->undo.prev == 1)) nsfw_tag(a->nsfw, a->hist, a->undo.root, a->undo.scope, a->undo.rel, a->undo.size, a->undo.prev == 1);
    a->nsfw_refresh = true;
    a->toast[0] = 0;
    app_toast(a, "Undone");
}

void app_nsfw_frame(App *a)
{
    /* idle long enough: lock again (only with a PIN set) */
    if (a->nsfw_unlocked && a->set.nsfw_pin_hash[0] && nsfw_should_relock(a->ui.time, a->last_input, a->set.nsfw_relock_minutes))
        app_nsfw_lock(a);
    if (a->undo.active && a->ui.time > a->undo.until) a->undo.active = false;
    if (a->nsfw_refresh) {
        a->nsfw_refresh = false;
        bool was_view = a->nsfw_view;
        app_nsfw_view_leave(a);          /* refilter the real trees, never the swapped ones */
        app_library_changed(a);
        if (was_view) app_nsfw_view_enter(a);
    }
}

/* ------------------------------------------------------------------------ */
/* Hiding the tab, locking, panic                                             */
/* ------------------------------------------------------------------------ */

void app_nsfw_lock(App *a)
{
    a->nsfw_unlocked = a->set.nsfw_pin_hash[0] == 0;     /* no PIN: nothing to lock */
}

void app_nsfw_set_tab_hidden(App *a, bool hidden)
{
    if (!a->set.nsfw_enabled) return;
    a->set.nsfw_tab = hidden ? NSFW_TAB_HIDDEN : NSFW_TAB_VISIBLE;
    app_save_settings(a);
    if (hidden) {
        app_nsfw_lock(a);
        if (a->screen == SCREEN_NSFW) app_go(a, a->last_portal ? a->last_portal : SCREEN_VIDEO);
        if (a->set.nsfw_hide_stops_playback) player_purge_nsfw(a, true);
        a->toast[0] = 0;
        app_toast(a, "NSFW tab hidden");         /* names nothing */
    } else {
        app_toast(a, "NSFW tab shown");
    }
}

bool app_nsfw_hotkey(App *a, Action act)
{
    switch (act) {
    case ACT_NSFW_TAB:
        if (!a->set.nsfw_enabled) return false;
        app_nsfw_set_tab_hidden(a, a->set.nsfw_tab == NSFW_TAB_VISIBLE);   /* the last press decides */
        return true;
    case ACT_NSFW_PANIC:
        if (!a->set.nsfw_enabled) return false;
        app_nsfw_panic(a);
        return true;
    case ACT_NSFW_MARK: {
        if (!a->set.nsfw_enabled || !a->focus_file || a->focus_file->dir) return false;
        char rel[THC_PATH_MAX];
        fs_relpath(a->focus_file, rel, sizeof rel);
        /* in the NSFW portal the key removes the mark, elsewhere it adds it */
        app_nsfw_mark(a, a->focus_root, NSFW_SCOPE_FILE, rel, a->focus_file->size, !a->nsfw_view);
        return true;
    }
    default: return false;
    }
}

/* Stop, close, silence, leave the portal, lock it, and empty it from every queue. Pressing it again is harmless. */
void app_nsfw_panic(App *a)
{
    player_purge_nsfw(a, true);
    app_nsfw_view_leave(a);
    if (a->screen == SCREEN_NSFW) app_go(a, a->last_portal ? a->last_portal : SCREEN_VIDEO);
    app_nsfw_lock(a);
    a->toast[0] = 0;
}

/* ------------------------------------------------------------------------ */
/* History: nothing is recorded for NSFW items unless the user turned it on    */
/* ------------------------------------------------------------------------ */

bool app_history_ok(App *a, int kind, int root, const char *rel, int64_t size)
{
    return nsfw_record_allowed(a->nsfw, settings_history_on(&a->set, kind), a->set.nsfw_record_history, root, rel, size);
}

int app_root_of(const App *a, const FsNode *n)
{
    const FsNode *top = n;
    while (top && top->parent) top = top->parent;
    for (int r = 0; r < ROOT_COUNT; r++)
        if (top && (a->lib.root[r] == top || a->nlib.root[r] == top || a->full.root[r] == top)) return r;
    return ROOT_VIDEO;
}

bool app_history_ok_node(App *a, int kind, const FsNode *n)
{
    char rel[THC_PATH_MAX];
    fs_relpath(n, rel, sizeof rel);
    return app_history_ok(a, kind, app_root_of(a, n), rel, n->size);
}

/* ------------------------------------------------------------------------ */
/* Clean-up at exit                                                           */
/* ------------------------------------------------------------------------ */

/* NSFW thumbnails and screenshots are stored apart and removed when THC exits (setting). */
void app_nsfw_cleanup(App *a)
{
    if (!a->set.nsfw_clear_thumbs_on_exit) return;
    char d[THC_PATH_MAX], n[THC_PATH_MAX + 8];
    SDL_snprintf(d, sizeof d, "%sthumbs/nsfw", a->data_dir);
    files_remove_in_dir(d);
    screenshot_dir_resolve(a->data_dir, a->set.screenshot_dir, d, sizeof d);
    screenshot_nsfw_dir(d, n, sizeof n);
    files_remove_in_dir(n);
}
