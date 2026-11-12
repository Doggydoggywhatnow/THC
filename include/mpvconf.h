/* mpvconf.h: checking and editing THC's own mpv.conf / osc.conf text.
 * No SDL or libmpv here: the caller supplies a function that asks MPV whether it
 * accepts an option, so the logic can be unit tested. */
#ifndef THC_MPVCONF_H
#define THC_MPVCONF_H

#include <stdbool.h>
#include <stddef.h>

/* Ask MPV to accept `name=value`. Returns 0 if accepted, otherwise an mpv error code
 * (-5 option not found, -6 / -7 bad value). */
typedef int (*MpvOptionSetter)(void *ud, const char *name, const char *value);

typedef enum MpvConfKind {
    MCK_NOT_FOUND = 0,   /* MPV has no such option and skips the line */
    MCK_BAD_VALUE,       /* MPV rejected the value */
    MCK_OSC_OPTION,      /* an on-screen-controller setting: belongs in script-opts/osc.conf */
    MCK_THC_CONTROLLED   /* accepted by MPV, but THC overrides it (watch history) */
} MpvConfKind;

typedef struct MpvConfIssue {
    int line;            /* 1-based */
    MpvConfKind kind;
    char option[64];
    char value[128];
    char hint[200];
} MpvConfIssue;

/* Check every option line. Returns the number of issues written (up to `max`). */
int mpvconf_check(const char *text, MpvOptionSetter set, void *ud, MpvConfIssue *out, int max);

/* Is this a setting of MPV's on-screen controller (layout, windowcontrols, ...), or a
 * wrongly prefixed one (osc-layout)? */
bool mpvconf_is_osc_option(const char *name);
/* Options THC sets itself so that its own history stays the source of truth. */
bool mpvconf_is_thc_controlled(const char *name);

/* Read or set `key=value` in config text, keeping comments and order. get returns false if
 * the key is not set. set writes the new text into out (NUL-terminated) and returns false if it
 * does not fit. */
bool mpvconf_get_key(const char *text, const char *key, char *value, size_t cap);
bool mpvconf_set_key(const char *text, const char *key, const char *value, char *out, size_t cap);

/* THC's hotkey text ("Ctrl+Shift+X") as an mpv key name ("Ctrl+X"). False if it cannot be expressed. */
bool mpvconf_key_from_hotkey(const char *hotkey, char *out, size_t cap);

#endif
