#include "hotkeys.h"

#include <string.h>

static SDL_Keymod norm(SDL_Keymod m)
{
    SDL_Keymod r = 0;
    if (m & SDL_KMOD_CTRL) r |= SDL_KMOD_CTRL;
    if (m & SDL_KMOD_SHIFT) r |= SDL_KMOD_SHIFT;
    if (m & SDL_KMOD_ALT) r |= SDL_KMOD_ALT;
    if (m & SDL_KMOD_GUI) r |= SDL_KMOD_GUI;
    return r;
}

static bool parse(const char *spec, SDL_Keycode *key, SDL_Keymod *mod)
{
    *mod = 0;
    *key = SDLK_UNKNOWN;
    char buf[64];
    SDL_strlcpy(buf, spec, sizeof buf);
    char *p = buf;
    for (;;) {
        char *plus = strchr(p, '+');
        /* a trailing "+" key, as in "Ctrl++", is the plus key itself */
        if (plus && plus[1] == 0 && plus == p) plus = NULL;
        if (!plus) break;
        *plus = 0;
        if (!SDL_strcasecmp(p, "Ctrl") || !SDL_strcasecmp(p, "Control")) *mod |= SDL_KMOD_CTRL;
        else if (!SDL_strcasecmp(p, "Shift")) *mod |= SDL_KMOD_SHIFT;
        else if (!SDL_strcasecmp(p, "Alt") || !SDL_strcasecmp(p, "Option")) *mod |= SDL_KMOD_ALT;
        else if (!SDL_strcasecmp(p, "Cmd") || !SDL_strcasecmp(p, "Super") || !SDL_strcasecmp(p, "Win") ||
                 !SDL_strcasecmp(p, "Gui"))
            *mod |= SDL_KMOD_GUI;
        else return false;
        p = plus + 1;
    }
    if (!*p) return false;
    *key = SDL_GetKeyFromName(p);
    return *key != SDLK_UNKNOWN;
}

bool hotkey_matches(const char *spec, SDL_Keycode key, SDL_Keymod mod)
{
    SDL_Keycode k;
    SDL_Keymod m;
    if (!spec || !*spec || !parse(spec, &k, &m)) return false;
    SDL_Keymod have = norm(mod);
    if (k != key) return false;
    /* Shift is ignored for symbol keys that need it on some layouts ("=" vs "+"). */
    if (!(m & SDL_KMOD_SHIFT) && (key < 'a' || key > 'z')) have &= ~SDL_KMOD_SHIFT;
    return have == m;
}

bool hotkey_from_event(SDL_Keycode key, SDL_Keymod mod, char *out, size_t cap)
{
    switch (key) {
    case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LSHIFT: case SDLK_RSHIFT:
    case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI:
        return false;
    default: break;
    }
    const char *name = SDL_GetKeyName(key);
    if (!name || !*name) return false;
    SDL_Keymod m = norm(mod);
    SDL_snprintf(out, cap, "%s%s%s%s%s", (m & SDL_KMOD_CTRL) ? "Ctrl+" : "", (m & SDL_KMOD_ALT) ? "Alt+" : "",
                 (m & SDL_KMOD_GUI) ? "Cmd+" : "", (m & SDL_KMOD_SHIFT) ? "Shift+" : "", name);
    return true;
}
