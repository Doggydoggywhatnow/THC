/* ontop.h: when THC's MPV video window should stay on top of other windows.
 * Pure logic (no SDL, no mpv), so it is tested on its own. */
#ifndef THC_ONTOP_H
#define THC_ONTOP_H

#include "settings.h"

#include <stdbool.h>


/* Should the window be on top right now? `playing` = a video is playing and not paused. */
bool ontop_wanted(int mode, bool playing, bool fullscreen, bool ignore_fullscreen);
/* mpv's ontop-level value for the setting (only macOS uses it): "window" or "system". */
const char *ontop_level_name(int macos_level);
/* The next mode for the hotkey: Off -> While playing -> Always -> Off. */
int ontop_next_mode(int mode);
const char *ontop_mode_label(int mode);
/* Wayland compositors, not applications, decide stacking. Pass XDG_SESSION_TYPE and WAYLAND_DISPLAY. */
bool ontop_is_wayland(const char *session_type, const char *wayland_display);

#endif
