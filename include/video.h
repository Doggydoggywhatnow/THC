/* video.h: one video playback in a real mpv window, observed by THC.
 *
 * THC creates a libmpv handle per playback and lets mpv open its own window,
 * so mpv.conf, input.conf, scripts, shaders, the on-screen controller and
 * every other mpv feature work exactly as in standalone mpv. THC only reads
 * time-pos, duration, pause and eof-reached, and listens for end-of-file.
 */
#ifndef THC_VIDEO_H
#define THC_VIDEO_H

#include "mpv_dyn.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    VIDEO_EV_PROGRESS = 1 << 0, /* periodic: time to save the position */
    VIDEO_EV_PAUSED   = 1 << 1, /* playback was paused */
    VIDEO_EV_ENDED    = 1 << 2, /* the session is over (EOF, quit, error) */
    VIDEO_EV_LOADED   = 1 << 3, /* file loaded, duration known */
    VIDEO_EV_CHANGED  = 1 << 4, /* any observed value changed (redraw) */
    VIDEO_EV_FILE_ENDED = 1 << 5 /* chain mode: one file finished, the window stays open */
};

typedef struct VideoSession {
    const MpvApi *api;
    mpv_handle *h;
    bool active;
    bool paused;
    bool reached_eof;   /* playback ran to the end of the file */
    bool failed;        /* mpv could not play the file */
    double pos;         /* seconds */
    double duration;    /* seconds, 0 = unknown */
    uint64_t last_progress_ms;
    bool fullscreen;    /* the window is fullscreen (observed) */
    int ontop_applied;  /* last ontop value sent to mpv: -1 = not yet */
    bool chain;         /* the window outlives a file: THC loads the next one into it */
    int expect_stops;   /* END_FILE events caused by our own loadfile, to ignore */
    char error[256];
} VideoSession;

/* Mpv options that THC sets for every session. `audio_only` plays without a
 * window (used for audiobooks that need libmpv, such as M4B). */
typedef struct VideoOptions {
    const char *config_dir;  /* THC's own mpv config directory */
    double start;            /* resume position in seconds, 0 = beginning */
    bool audio_only;
    uint32_t wake_event;     /* SDL user event pushed when mpv has news */
    bool chain;              /* keep the window open between files (video queue) */
} VideoOptions;

bool video_play(VideoSession *s, const MpvApi *api, const char *path, const VideoOptions *opt);

/* Load another file into the same session (same window), starting at `start` seconds. */
bool video_load(VideoSession *s, const char *path, double start);

/* Handle pending mpv events. Returns a mask of VIDEO_EV_* flags. */
int video_pump(VideoSession *s);

/* Send a raw mpv command string (for audio-only sessions driven by THC). */
void video_command(VideoSession *s, const char *cmd);
void video_set_pause(VideoSession *s, bool pause);
/* Keep the window on top (or not); only talks to mpv when the value changes. macOS also gets ontop-level. */
void video_set_ontop(VideoSession *s, bool on, const char *macos_level);
void video_seek_abs(VideoSession *s, double seconds);
void video_set_speed(VideoSession *s, double speed);
void video_set_volume(VideoSession *s, double volume_percent);

/* Delete mpv's own saved positions in THC's config folder (used by "clear history"). */
void video_clear_watch_later(const char *config_dir);

/* Ask mpv to quit and release the handle. */
void video_stop(VideoSession *s);

#endif
