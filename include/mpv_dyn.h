/* mpv_dyn.h: libmpv, loaded at run time.
 *
 * THC does not link libmpv. It opens the shared library when it starts
 * (libmpv.so.2, libmpv-2.dll, libmpv.2.dylib, ...) and looks up the few
 * client API functions it needs. That keeps the build free of an mpv
 * dependency, lets the user pick an LGPL libmpv, and keeps mpv's license
 * separate from THC's.
 *
 * The declarations below are a minimal subset of mpv's client API
 * (libmpv/client.h, API version 2.x). Values match the upstream header.
 */
#ifndef THC_MPV_DYN_H
#define THC_MPV_DYN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct mpv_handle mpv_handle;

typedef enum mpv_format {
    MPV_FORMAT_NONE = 0,
    MPV_FORMAT_STRING = 1,
    MPV_FORMAT_OSD_STRING = 2,
    MPV_FORMAT_FLAG = 3,
    MPV_FORMAT_INT64 = 4,
    MPV_FORMAT_DOUBLE = 5,
    MPV_FORMAT_NODE = 6
} mpv_format;

typedef enum mpv_event_id {
    MPV_EVENT_NONE = 0,
    MPV_EVENT_SHUTDOWN = 1,
    MPV_EVENT_LOG_MESSAGE = 2,
    MPV_EVENT_START_FILE = 6,
    MPV_EVENT_END_FILE = 7,
    MPV_EVENT_FILE_LOADED = 8,
    MPV_EVENT_CLIENT_MESSAGE = 16,
    MPV_EVENT_IDLE = 11,
    MPV_EVENT_PLAYBACK_RESTART = 21,
    MPV_EVENT_PROPERTY_CHANGE = 22
} mpv_event_id;

typedef enum mpv_end_file_reason {
    MPV_END_FILE_REASON_EOF = 0,
    MPV_END_FILE_REASON_STOP = 2,
    MPV_END_FILE_REASON_QUIT = 3,
    MPV_END_FILE_REASON_ERROR = 4,
    MPV_END_FILE_REASON_REDIRECT = 5
} mpv_end_file_reason;

typedef struct mpv_event_property {
    const char *name;
    mpv_format format;
    void *data;
} mpv_event_property;

typedef struct mpv_event_end_file {
    mpv_end_file_reason reason;
    int error;
    /* more fields follow upstream; THC reads only these */
} mpv_event_end_file;

typedef struct mpv_event_client_message {
    int num_args;
    const char **args;
} mpv_event_client_message;

typedef struct mpv_event {
    mpv_event_id event_id;
    int error;
    uint64_t reply_userdata;
    void *data;
} mpv_event;

typedef struct MpvApi {
    void *lib;
    char path[512];
    unsigned long (*client_api_version)(void);
    mpv_handle *(*create)(void);
    int (*initialize)(mpv_handle *);
    void (*terminate_destroy)(mpv_handle *);
    int (*set_option_string)(mpv_handle *, const char *, const char *);
    int (*set_property_string)(mpv_handle *, const char *, const char *);
    int (*set_property)(mpv_handle *, const char *, mpv_format, void *);
    int (*get_property)(mpv_handle *, const char *, mpv_format, void *);
    char *(*get_property_string)(mpv_handle *, const char *);
    int (*command)(mpv_handle *, const char **);
    int (*command_string)(mpv_handle *, const char *);
    int (*observe_property)(mpv_handle *, uint64_t, const char *, mpv_format);
    mpv_event *(*wait_event)(mpv_handle *, double);
    void (*set_wakeup_callback)(mpv_handle *, void (*)(void *), void *);
    const char *(*error_string)(int);
    void (*free)(void *);
} MpvApi;

/* Load libmpv. `override_path` (may be NULL or empty) is tried first, then
 * $THC_LIBMPV, then the usual library names for this OS, then a copy next
 * to the THC executable. Returns false if no usable libmpv was found. */
bool mpv_dyn_load(MpvApi *api, const char *override_path);
void mpv_dyn_unload(MpvApi *api);

#endif
