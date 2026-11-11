#include "mpv_dyn.h"

#include <SDL3/SDL.h>
#include <string.h>

static const char *const k_names[] = {
#if defined(_WIN32)
    "libmpv-2.dll", "mpv-2.dll", "mpv-1.dll", "libmpv.dll",
#elif defined(__APPLE__)
    "libmpv.2.dylib", "libmpv.dylib",
    "/opt/homebrew/lib/libmpv.2.dylib", "/opt/homebrew/lib/libmpv.dylib",
    "/usr/local/lib/libmpv.2.dylib", "/usr/local/lib/libmpv.dylib",
    "/opt/local/lib/libmpv.2.dylib",
#else
    "libmpv.so.2", "libmpv.so.1", "libmpv.so",
#endif
    NULL
};

#define SYM(field, name)                                                     \
    do {                                                                     \
        SDL_FunctionPointer fp_ = SDL_LoadFunction(api->lib, name);          \
        if (!fp_) {                                                          \
            SDL_Log("libmpv: missing symbol %s", name);                      \
            goto fail;                                                       \
        }                                                                    \
        memcpy(&api->field, &fp_, sizeof fp_);                               \
    } while (0)

static bool try_open(MpvApi *api, const char *path)
{
    if (!path || !*path) return false;
    api->lib = SDL_LoadObject(path);
    if (!api->lib) return false;
    SYM(client_api_version, "mpv_client_api_version");
    SYM(create, "mpv_create");
    SYM(initialize, "mpv_initialize");
    SYM(terminate_destroy, "mpv_terminate_destroy");
    SYM(set_option_string, "mpv_set_option_string");
    SYM(set_property_string, "mpv_set_property_string");
    SYM(set_property, "mpv_set_property");
    SYM(get_property, "mpv_get_property");
    SYM(get_property_string, "mpv_get_property_string");
    SYM(command, "mpv_command");
    SYM(command_string, "mpv_command_string");
    SYM(observe_property, "mpv_observe_property");
    SYM(wait_event, "mpv_wait_event");
    SYM(set_wakeup_callback, "mpv_set_wakeup_callback");
    SYM(error_string, "mpv_error_string");
    SYM(free, "mpv_free");
    /* THC needs client API 2.x (mpv 0.35 and later). */
    if ((api->client_api_version() >> 16) < 2) {
        SDL_Log("libmpv at %s is too old", path);
        goto fail;
    }
    SDL_strlcpy(api->path, path, sizeof api->path);
    return true;
fail:
    SDL_UnloadObject(api->lib);
    api->lib = NULL;
    return false;
}

bool mpv_dyn_load(MpvApi *api, const char *override_path)
{
    memset(api, 0, sizeof *api);
    if (try_open(api, override_path)) return true;
    if (try_open(api, SDL_getenv("THC_LIBMPV"))) return true;
    for (int i = 0; k_names[i]; i++)
        if (try_open(api, k_names[i])) return true;
    /* A libmpv shipped next to the executable. */
    const char *base = SDL_GetBasePath();
    if (base) {
        for (int i = 0; k_names[i]; i++) {
            if (strchr(k_names[i], '/')) continue;
            char buf[1024];
            SDL_snprintf(buf, sizeof buf, "%s%s", base, k_names[i]);
            if (try_open(api, buf)) return true;
        }
    }
    return false;
}

void mpv_dyn_unload(MpvApi *api)
{
    if (api->lib) SDL_UnloadObject(api->lib);
    memset(api, 0, sizeof *api);
}
