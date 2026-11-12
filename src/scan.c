/* scan.c: read library folders in a background thread (read-only). */
#include "app.h"
#include "scantree.h"
#include "nsfw.h"

static int SDLCALL scan_thread(void *ud)
{
    App *a = ud;
    Library *lib = SDL_calloc(1, sizeof *lib);
    SDL_strlcpy(lib->path[ROOT_VIDEO], a->set.video_dir, THC_PATH_MAX);
    SDL_strlcpy(lib->path[ROOT_MUSIC], a->set.music_dir, THC_PATH_MAX);
    SDL_strlcpy(lib->path[ROOT_BOOKS], a->set.books_dir, THC_PATH_MAX);
    for (int i = 0; i < SECTION_COUNT; i++)
        SDL_strlcpy(lib->path[ROOT_SECTION0 + i], a->set.section_dir[i], THC_PATH_MAX);
    SDL_strlcpy(lib->path[ROOT_NSFW], a->set.nsfw_dir, THC_PATH_MAX);
    const char *nd = a->set.nsfw_dir;
    for (int i = 0; i < ROOT_COUNT; i++) {
        /* what is in the NSFW folder is scanned once, as its own root: other folders leave it out, and a library
         * folder that lies inside it is not scanned at all */
        if (i != ROOT_NSFW && nd[0] && (nsfw_path_same(nd, lib->path[i]) || nsfw_path_inside(nd, lib->path[i]))) continue;
        lib->root[i] = scan_tree(lib->path[i], i == ROOT_NSFW ? NULL : nd);
    }
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
    tagcache_shutdown();
    app_free_library(a);                 /* the old filtered copies and the old scan */
    a->full = *a->scan_result;
    SDL_free(a->scan_result);
    a->scan_result = NULL;
    app_nsfw_apply(a);                   /* a->lib / a->nlib from the new scan */
    a->lib_version++;
    return true;
}
