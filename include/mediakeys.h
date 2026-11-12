/* mediakeys.h: media keys and the system "now playing" integration.
 *
 * The policy lives here, with no SDL and no platform code, so it can be tested: when THC claims the
 * keys, what the system is allowed to see (video shares nothing by default, so what you watch is not
 * shown on lock screens), and what a key does. A platform backend (MPRIS over D-Bus on Linux) does
 * the talking; the others fall back to media keys while THC's window is focused. */
#ifndef THC_MEDIAKEYS_H
#define THC_MEDIAKEYS_H

#include "settings.h"

#include <stdbool.h>

enum { MKA_PLAY = 1, MKA_PAUSE, MKA_TOGGLE, MKA_NEXT, MKA_PREV, MKA_STOP, MKA_SEEK_REL, MKA_SEEK_ABS };
typedef void (*MediaActionFn)(int action, double arg, void *ud);

typedef struct NowPlaying {
    bool claimed;            /* THC owns the keys right now */
    bool playing;
    bool is_video;
    char title[256], artist[256], album[256];
    char art[1024];          /* an image file path, or empty */
    double position, length; /* seconds */
} NowPlaying;

typedef struct MediaBackend {
    const char *name;
    bool (*init)(MediaActionFn fn, void *ud);
    void (*update)(const NowPlaying *np);   /* called when something changed */
    void (*pump)(void);                     /* every frame: read incoming requests */
    void (*shutdown)(void);
} MediaBackend;

/* THC claims the keys only while it has something loaded (so other players keep them otherwise),
 * and only when the mode allows it. */
bool mediakeys_claim(int mode, bool has_item);
/* May the system see details of this kind of media? (Video: no, unless the user turned it on.) */
bool mediakeys_shares(const Settings *s, int kind);
/* What the system gets. Nothing but "THC" for a video by default; an NSFW item is only ever "Playing" (#13). */
void mediakeys_build(NowPlaying *np, const Settings *s, int kind, bool has_item, bool nsfw, bool playing, const char *title,
                     const char *artist, const char *album, const char *art, double position, double length);
/* Seconds for the seek keys: the audiobook skip length for audiobooks, else mediakeys_seek_seconds. */
double mediakeys_seek_seconds(const Settings *s, int kind);

/* The Linux backend (MPRIS), or NULL where it was not built or libdbus cannot be loaded. */
const MediaBackend *mediakeys_linux_backend(void);

#endif
