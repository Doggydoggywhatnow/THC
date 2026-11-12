/* scantree.c: read one library folder into a tree (read-only; never writes, moves or renames). The only part of the
 * scan that needs SDL, apart from the thread in scan.c, so it is tested on its own. */
#include "scantree.h"
#include "nsfw.h"

#include <SDL3/SDL.h>
#include <string.h>

#define MAX_DEPTH 10

typedef struct ScanCtx {
    FsNode *node;
    int depth;
    char path[THC_PATH_MAX];
    const char *skip;           /* a folder to leave out (the NSFW folder inside a library folder, #34) */
} ScanCtx;

static void scan_dir(FsNode *node, const char *path, int depth, const char *skip);

static SDL_EnumerationResult SDLCALL on_entry(void *ud, const char *dirname, const char *fname)
{
    ScanCtx *c = ud;
    if (fname[0] == '.') return SDL_ENUM_CONTINUE;   /* hidden files */
    if (fname[0] == '$') return SDL_ENUM_CONTINUE;   /* $RECYCLE.BIN etc. */
    char full[THC_PATH_MAX];
    SDL_snprintf(full, sizeof full, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(full, &info)) return SDL_ENUM_CONTINUE;
    Sint64 mtime = (Sint64)(info.modify_time / SDL_NS_PER_SECOND);
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (c->depth >= MAX_DEPTH) return SDL_ENUM_CONTINUE;
        if (c->skip && nsfw_path_same(c->skip, full)) return SDL_ENUM_CONTINUE;
        FsNode *d = fs_add(c->node, fname, true, 0, mtime);
        if (d) {
            scan_dir(d, full, c->depth + 1, c->skip);
            if (d->nkids == 0) {
                /* drop folders with nothing THC can show */
                c->node->nkids--;
                fs_free(d);
            }
        }
    } else if (info.type == SDL_PATHTYPE_FILE) {
        MediaKind k = media_kind(fname);
        if (k != MK_OTHER) fs_add(c->node, fname, false, (int64_t)info.size, mtime);
    }
    return SDL_ENUM_CONTINUE;
}

static void scan_dir(FsNode *node, const char *path, int depth, const char *skip)
{
    ScanCtx c;
    c.node = node;
    c.depth = depth;
    SDL_strlcpy(c.path, path, sizeof c.path);
    c.skip = skip;
    SDL_EnumerateDirectory(path, on_entry, &c);
}

FsNode *scan_tree(const char *path, const char *skip)
{
    if (!path || !*path) return NULL;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) return NULL;
    FsNode *root = fs_new(path, true);
    scan_dir(root, path, 0, skip && *skip ? skip : NULL);
    /* folders made only of images are not media */
    fs_sort(root, true);
    return root;
}

