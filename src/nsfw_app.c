/* nsfw_app.c: connects the NSFW tags (core) to the app's library. The normal portals read a->lib, which is a
 * filtered copy without NSFW items; the NSFW portal reads a->nlib, which holds only them. Both are rebuilt from
 * a->full after every scan and every tag change, so no view has its own filter to forget (#13). */
#include "app.h"

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
}

void app_nsfw_sync(App *a)
{
    if (!a->nsfw) return;
    NsfwConfig c;
    memset(&c, 0, sizeof c);
    c.enabled = a->set.nsfw_enabled;
    c.rules_enabled = a->set.nsfw_rules_enabled;
    SDL_strlcpy(c.rules, a->set.nsfw_rules, sizeof c.rules);
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
