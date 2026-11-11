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
    VIDEO_EV_CHANGED  = 1 << 4  /* any observed value changed (redraw) */
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
    char error[256];
} VideoSession;

/* Mpv options that THC sets for every session. `audio_only` plays without a
 * window (used for audiobooks that need libmpv, such as M4B). */
typedef struct VideoOptions {
    const char *config_dir;  /* THC's own mpv config directory */
    double start;            /* resume position in seconds, 0 = beginning */
    bool audio_only;
    uint32_t wake_event;     /* SDL user event pushed when mpv has news */
} VideoOptions;

bool video_play(VideoSession *s, const MpvApi *api, const char *path, const VideoOptions *opt);

/* Handle pending mpv events. Returns a mask of VIDEO_EV_* flags. */
int video_pump(VideoSession *s);

/* Send a raw mpv command string (for audio-only sessions driven by THC). */
void video_command(VideoSession *s, const char *cmd);
void video_set_pause(VideoSession *s, bool pause);
void video_seek_abs(VideoSession *s, double seconds);
void video_set_speed(VideoSession *s, double speed);
void video_set_volume(VideoSession *s, double volume_percent);

/* Ask mpv to quit and release the handle. */
void video_stop(VideoSession *s);

#endif
