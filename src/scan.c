/* scan.c: read library folders in a background thread (read-only). */
#include "app.h"

#include <string.h>

#define MAX_DEPTH 10

typedef struct ScanCtx {
    FsNode *node;
    int depth;
    char path[THC_PATH_MAX];
} ScanCtx;

static void scan_dir(FsNode *node, const char *path, int depth);

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
        FsNode *d = fs_add(c->node, fname, true, 0, mtime);
        if (d) {
            scan_dir(d, full, c->depth + 1);
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

static void scan_dir(FsNode *node, const char *path, int depth)
{
    ScanCtx c;
    c.node = node;
    c.depth = depth;
    SDL_strlcpy(c.path, path, sizeof c.path);
    SDL_EnumerateDirectory(path, on_entry, &c);
}

static FsNode *scan_root(const char *path)
{
    if (!path || !*path) return NULL;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) return NULL;
    FsNode *root = fs_new(path, true);
    scan_dir(root, path, 0);
    /* folders made only of images are not media */
    fs_sort(root, true);
    return root;
}

static int SDLCALL scan_thread(void *ud)
{
    App *a = ud;
    Library *lib = SDL_calloc(1, sizeof *lib);
    SDL_strlcpy(lib->path[ROOT_VIDEO], a->set.video_dir, THC_PATH_MAX);
    SDL_strlcpy(lib->path[ROOT_MUSIC], a->set.music_dir, THC_PATH_MAX);
    SDL_strlcpy(lib->path[ROOT_BOOKS], a->set.books_dir, THC_PATH_MAX);
    for (int i = 0; i < SECTION_COUNT; i++)
        SDL_strlcpy(lib->path[ROOT_SECTION0 + i], a->set.section_dir[i], THC_PATH_MAX);
    for (int i = 0; i < ROOT_COUNT; i++) lib->root[i] = scan_root(lib->path[i]);
    a->scan_result = lib;
    SDL_SetAtomicInt(&a->scan_done, 1);
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = a->mpv_event; /* any wake-up will do */
    SDL_PushEvent(&ev);
    return 0;
}

void scan_start(App *a)
{
    if (a->scanning) return;
    a->scanning = true;
    SDL_SetAtomicInt(&a->scan_done, 0);
    a->scan_thread = SDL_CreateThread(scan_thread, "thc-scan", a);
    if (!a->scan_thread) {
        scan_thread(a);
    }
}

void library_free(Library *lib)
{
    for (int i = 0; i < ROOT_COUNT; i++) {
        fs_free(lib->root[i]);
        lib->root[i] = NULL;
    }
}

bool scan_poll(App *a)
{
    if (!a->scanning || !SDL_GetAtomicInt(&a->scan_done)) return false;
    if (a->scan_thread) SDL_WaitThread(a->scan_thread, NULL);
    a->scan_thread = NULL;
    a->scanning = false;
    library_free(&a->lib);
    a->lib = *a->scan_result;
    SDL_free(a->scan_result);
    a->scan_result = NULL;
    a->lib_version++;
    return true;
}
