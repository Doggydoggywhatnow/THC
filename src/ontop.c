#include "ontop.h"

#include <string.h>

bool ontop_wanted(int mode, bool playing, bool fullscreen, bool ignore_fullscreen)
{
    if (fullscreen && ignore_fullscreen) return false;
    switch (mode) {
    case ONTOP_ALWAYS: return true;
    case ONTOP_PLAYING: return playing;
    default: return false;
    }
}

const char *ontop_level_name(int macos_level) { return macos_level == 1 ? "system" : "window"; }

int ontop_next_mode(int mode)
{
    return mode == ONTOP_OFF ? ONTOP_PLAYING : mode == ONTOP_PLAYING ? ONTOP_ALWAYS : ONTOP_OFF;
}

const char *ontop_mode_label(int mode)
{
    return mode == ONTOP_PLAYING ? "While playing" : mode == ONTOP_ALWAYS ? "Always" : "Off";
}

bool ontop_is_wayland(const char *session_type, const char *wayland_display)
{
    if (session_type && !strcmp(session_type, "wayland")) return true;
    if (session_type && !strcmp(session_type, "x11")) return false;
    return wayland_display && *wayland_display;
}
