#include "mediakeys.h"

#include <string.h>

bool mediakeys_claim(int mode, bool has_item)
{
    return mode != KEYS_OFF && has_item;
}

bool mediakeys_shares(const Settings *s, int kind)
{
    switch (kind) {
    case KIND_MUSIC: return s->nowplaying_music;
    case KIND_BOOK: return s->nowplaying_books;
    default: return s->nowplaying_video;
    }
}

static void put(char *dst, size_t cap, const char *src)
{
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

void mediakeys_build(NowPlaying *np, const Settings *s, int kind, bool has_item, bool nsfw, bool playing, const char *title,
                     const char *artist, const char *album, const char *art, double position, double length)
{
    memset(np, 0, sizeof *np);
    np->claimed = mediakeys_claim(s->mediakeys_mode, has_item);
    np->is_video = kind == KIND_VIDEO;
    np->playing = playing;
    np->position = position;
    np->length = length;
    if (!np->claimed) return;
    if (nsfw) {                                     /* never a title, artist, album or picture */
        put(np->title, sizeof np->title, "Playing");
        return;
    }
    if (!mediakeys_shares(s, kind)) {
        put(np->title, sizeof np->title, "THC");        /* the system just sees THC */
        return;
    }
    put(np->title, sizeof np->title, title);
    if (s->nowplaying_titles == TITLES_FULL) {
        put(np->artist, sizeof np->artist, artist);
        put(np->album, sizeof np->album, album);
    }
    if (s->nowplaying_art) put(np->art, sizeof np->art, art);
}

double mediakeys_seek_seconds(const Settings *s, int kind)
{
    return kind == KIND_BOOK ? s->skip_seconds : s->mediakeys_seek_seconds;
}
