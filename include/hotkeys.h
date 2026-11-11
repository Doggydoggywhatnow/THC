/* hotkeys.h: editable keyboard shortcuts, stored as text like "Ctrl+Right". */
#ifndef THC_HOTKEYS_H
#define THC_HOTKEYS_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

/* Does a key press match a shortcut string? */
bool hotkey_matches(const char *spec, SDL_Keycode key, SDL_Keymod mod);
/* Describe a key press as a shortcut string. Returns false for lone modifiers. */
bool hotkey_from_event(SDL_Keycode key, SDL_Keymod mod, char *out, size_t cap);

#endif
