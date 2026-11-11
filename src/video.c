#include "video.h"

#include <SDL3/SDL.h>
#include <string.h>

enum { OBS_TIME = 1, OBS_DURATION, OBS_PAUSE, OBS_EOF };

/* Progress is reported at most this often while playing. */
#define PROGRESS_INTERVAL_MS 5000

static void wakeup(void *ud)
{
    /* Called from an mpv thread: only post an event, never call mpv here. */
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = (Uint32)(uintptr_t)ud;
    SDL_PushEvent(&ev);
}

/* libmpv does not autoload the scripts/ folder of the config directory the way
 * the mpv player does, so THC loads them itself (.lua, .js, .so/.dll, and
 * folders with a main.lua or main.js). */
typedef struct ScriptCtx { const MpvApi *api; mpv_handle *h; } ScriptCtx;

static SDL_EnumerationResult SDLCALL load_script_cb(void *ud, const char *dirname, const char *fname)
{
    ScriptCtx *c = ud;
    if (fname[0] == '.') return SDL_ENUM_CONTINUE;
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    const char *dot = SDL_strrchr(fname, '.');
    bool ok = info.type == SDL_PATHTYPE_DIRECTORY ||
              (dot && (!SDL_strcasecmp(dot, ".lua") || !SDL_strcasecmp(dot, ".js") ||
                       !SDL_strcasecmp(dot, ".so") || !SDL_strcasecmp(dot, ".dll")));
    if (!ok) return SDL_ENUM_CONTINUE;
    const char *cmd[] = { "load-script", path, NULL };
    c->api->command(c->h, cmd);
    if (SDL_getenv("THC_MPV_LOG")) SDL_Log("THC: loading script %s", path);
    return SDL_ENUM_CONTINUE;
}

static void load_user_scripts(const MpvApi *api, mpv_handle *h, const char *config_dir)
{
    char dir[1100];
    SDL_snprintf(dir, sizeof dir, "%s/scripts", config_dir);
    ScriptCtx c = { api, h };
    SDL_EnumerateDirectory(dir, load_script_cb, &c);
}

bool video_play(VideoSession *s, const MpvApi *api, const char *path, const VideoOptions *opt)
{
    memset(s, 0, sizeof *s);
    s->api = api;
    if (!api || !api->lib) {
        SDL_strlcpy(s->error, "libmpv is not available", sizeof s->error);
        return false;
    }
    s->h = api->create();
    if (!s->h) {
        SDL_strlcpy(s->error, "mpv_create failed", sizeof s->error);
        return false;
    }

    /* THC's MPV is separate from any standalone MPV: load config only from
     * THC's own directory, never from the system or user mpv folder. */
    api->set_option_string(s->h, "config-dir", opt->config_dir);
    api->set_option_string(s->h, "config", "yes");

    /* THC's history database is the only source of truth for where you stopped.
     * mpv's own resume (watch_later files) would compete with it and can bring
     * back older positions, so it is switched off here, and what is left of it
     * is kept inside THC's own folder. These win over anything in mpv.conf. */
    char wl[1100];
    SDL_snprintf(wl, sizeof wl, "%s/watch_later", opt->config_dir);
    api->set_option_string(s->h, "resume-playback", "no");
    api->set_option_string(s->h, "save-position-on-quit", "no");
    api->set_option_string(s->h, "watch-later-directory", wl);

    /* Behave like the mpv player: its own window, default key bindings,
     * on-screen controller. mpv.conf (loaded at initialize) can override. */
    /* THC_MPV_LOG=1 prints mpv's own log to the terminal, for debugging. */
    if (SDL_getenv("THC_MPV_LOG")) {
        api->set_option_string(s->h, "terminal", "yes");
        api->set_option_string(s->h, "msg-level", "all=v");
    } else {
        api->set_option_string(s->h, "terminal", "no");
    }
    api->set_option_string(s->h, "input-default-bindings", "yes");
    api->set_option_string(s->h, "input-vo-keyboard", "yes");
    api->set_option_string(s->h, "osc", "yes");
    api->set_option_string(s->h, "idle", "once");
    api->set_option_string(s->h, "keep-open", "no");
    api->set_option_string(s->h, "title", "${media-title} - THC");
    if (opt->audio_only) {
        api->set_option_string(s->h, "vid", "no");
        api->set_option_string(s->h, "force-window", "no");
        api->set_option_string(s->h, "audio-display", "no");
    } else {
        api->set_option_string(s->h, "force-window", "immediate");
    }
    /* always explicit: a position of zero means "from the beginning" */
    {
        char buf[64];
        SDL_snprintf(buf, sizeof buf, "%.3f", opt->start > 1.0 ? opt->start : 0.0);
        api->set_option_string(s->h, "start", buf);
        if (SDL_getenv("THC_MPV_LOG")) SDL_Log("THC: starting %s at %s s", path, buf);
    }

    int err = api->initialize(s->h);
    if (err >= 0) {
        /* mpv.conf is loaded at initialize and overrides the options set above, so
         * set them again as properties, which win: THC's rules for resuming hold
         * whatever mpv.conf says. */
        api->set_property_string(s->h, "resume-playback", "no");
        api->set_property_string(s->h, "save-position-on-quit", "no");
        api->set_property_string(s->h, "watch-later-directory", wl);
        char sb[64];
        SDL_snprintf(sb, sizeof sb, "%.3f", opt->start > 1.0 ? opt->start : 0.0);
        api->set_property_string(s->h, "start", sb);
        load_user_scripts(api, s->h, opt->config_dir);
    }
    if (err < 0) {
        SDL_snprintf(s->error, sizeof s->error, "mpv_initialize: %s", api->error_string(err));
        api->terminate_destroy(s->h);
        s->h = NULL;
        return false;
    }

    api->observe_property(s->h, OBS_TIME, "time-pos", MPV_FORMAT_DOUBLE);
    api->observe_property(s->h, OBS_DURATION, "duration", MPV_FORMAT_DOUBLE);
    api->observe_property(s->h, OBS_PAUSE, "pause", MPV_FORMAT_FLAG);
    api->observe_property(s->h, OBS_EOF, "eof-reached", MPV_FORMAT_FLAG);
    if (opt->wake_event)
        api->set_wakeup_callback(s->h, wakeup, (void *)(uintptr_t)opt->wake_event);

    const char *cmd[] = { "loadfile", path, NULL };
    err = api->command(s->h, cmd);
    if (err < 0) {
        SDL_snprintf(s->error, sizeof s->error, "loadfile: %s", api->error_string(err));
        api->terminate_destroy(s->h);
        s->h = NULL;
        return false;
    }
    s->active = true;
    s->last_progress_ms = SDL_GetTicks();
    return true;
}

int video_pump(VideoSession *s)
{
    int flags = 0;
    if (!s->h) return 0;
    for (;;) {
        mpv_event *ev = s->api->wait_event(s->h, 0);
        if (!ev || ev->event_id == MPV_EVENT_NONE) break;
        switch (ev->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            mpv_event_property *p = ev->data;
            flags |= VIDEO_EV_CHANGED;
            if (p->format == MPV_FORMAT_NONE || !p->data) break;
            switch (ev->reply_userdata) {
            case OBS_TIME: s->pos = *(double *)p->data; break;
            case OBS_DURATION:
                s->duration = *(double *)p->data;
                flags |= VIDEO_EV_LOADED;
                break;
            case OBS_PAUSE: {
                bool was = s->paused;
                s->paused = *(int *)p->data != 0;
                if (s->paused && !was) flags |= VIDEO_EV_PAUSED;
                break;
            }
            case OBS_EOF:
                if (*(int *)p->data) s->reached_eof = true;
                break;
            }
            break;
        }
        case MPV_EVENT_END_FILE: {
            mpv_event_end_file *e = ev->data;
            if (e && e->reason == MPV_END_FILE_REASON_REDIRECT) break;
            if (e && e->reason == MPV_END_FILE_REASON_EOF) {
                s->reached_eof = true;
                if (s->duration > 0) s->pos = s->duration;
            }
            if (e && e->reason == MPV_END_FILE_REASON_ERROR) {
                s->failed = true;
                SDL_snprintf(s->error, sizeof s->error, "mpv could not play the file: %s",
                             s->api->error_string(e->error));
            }
            s->active = false;
            flags |= VIDEO_EV_ENDED | VIDEO_EV_CHANGED;
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            s->active = false;
            flags |= VIDEO_EV_ENDED | VIDEO_EV_CHANGED;
            break;
        default:
            break;
        }
        if (!s->active) break;
    }
    if (s->active && !s->paused) {
        uint64_t now = SDL_GetTicks();
        if (now - s->last_progress_ms >= PROGRESS_INTERVAL_MS) {
            s->last_progress_ms = now;
            flags |= VIDEO_EV_PROGRESS;
        }
    }
    if (flags & VIDEO_EV_ENDED) {
        s->api->terminate_destroy(s->h);
        s->h = NULL;
    }
    return flags;
}

void video_command(VideoSession *s, const char *cmd)
{
    if (s->h) s->api->command_string(s->h, cmd);
}

void video_set_pause(VideoSession *s, bool pause)
{
    if (!s->h) return;
    int flag = pause;
    s->api->set_property(s->h, "pause", MPV_FORMAT_FLAG, &flag);
}

void video_seek_abs(VideoSession *s, double seconds)
{
    if (!s->h) return;
    char buf[64];
    SDL_snprintf(buf, sizeof buf, "%.3f", seconds < 0 ? 0 : seconds);
    const char *cmd[] = { "seek", buf, "absolute", NULL };
    s->api->command(s->h, cmd);
}

void video_set_speed(VideoSession *s, double speed)
{
    if (s->h) s->api->set_property(s->h, "speed", MPV_FORMAT_DOUBLE, &speed);
}

void video_set_volume(VideoSession *s, double volume_percent)
{
    if (s->h) s->api->set_property(s->h, "volume", MPV_FORMAT_DOUBLE, &volume_percent);
}

void video_stop(VideoSession *s)
{
    if (!s->h) return;
    s->api->terminate_destroy(s->h);
    s->h = NULL;
    s->active = false;
}

/* Remove mpv's own saved positions inside THC's config folder (never anywhere else). */
static SDL_EnumerationResult SDLCALL rm_entry(void *ud, const char *dirname, const char *fname)
{
    (void)ud;
    char full[1200];
    SDL_snprintf(full, sizeof full, "%s%s", dirname, fname);
    SDL_RemovePath(full);
    return SDL_ENUM_CONTINUE;
}

void video_clear_watch_later(const char *config_dir)
{
    if (!config_dir || !*config_dir) return;
    char wl[1100];
    SDL_snprintf(wl, sizeof wl, "%s/watch_later", config_dir);
    SDL_EnumerateDirectory(wl, rm_entry, NULL);
}
