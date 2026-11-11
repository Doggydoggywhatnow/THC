/* art.c: cover and poster images found next to media (read-only), decoded
 * with stb_image a few per frame and kept in a small texture cache. */
#include "app.h"

#include "stb_image.h"

#include <stdlib.h>
#include <string.h>

#define ART_SLOTS 192
#define ART_MAX_PX 600     /* covers are downscaled to at most this */
#define ART_PER_FRAME 2

typedef struct ArtSlot {
    Uint32 hash;
    char path[THC_PATH_MAX];
    SDL_Texture *tex;
    bool failed;
    Uint64 used;
} ArtSlot;

static ArtSlot g_slots[ART_SLOTS];
static int g_loaded_this_frame;
static Uint64 g_tick;

static SDL_Texture *load(App *a, const char *path)
{
    size_t len = 0;
    int w, h, comp;
    unsigned char *px;
    if (!strncmp(path, "tag:", 4)) {
        /* a cover embedded in an audio file's tags (read-only) */
        unsigned char *data = tags_read_art(path + 4, &len, NULL, 0);
        if (!data) return NULL;
        px = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 4);
        free(data);
    } else {
        void *data = SDL_LoadFile(path, &len);
        if (!data) return NULL;
        px = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 4);
        SDL_free(data);
    }
    if (!px) return NULL;
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    SDL_Texture *t = NULL;
    if (s) {
        if (w > ART_MAX_PX || h > ART_MAX_PX) {
            float k = (float)ART_MAX_PX / (w > h ? w : h);
            SDL_Surface *small = SDL_ScaleSurface(s, (int)(w * k), (int)(h * k), SDL_SCALEMODE_LINEAR);
            if (small) {
                SDL_DestroySurface(s);
                s = small;
            }
        }
        t = SDL_CreateTextureFromSurface(a->ren, s);
        SDL_DestroySurface(s);
    }
    stbi_image_free(px);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

SDL_Texture *art_get(App *a, const char *path)
{
    if (!path || !*path) return NULL;
    Uint32 h = ui_id(path);
    ArtSlot *victim = &g_slots[0];
    for (int i = 0; i < ART_SLOTS; i++) {
        ArtSlot *s = &g_slots[i];
        if (s->hash == h && !strcmp(s->path, path)) {
            s->used = g_tick;
            return s->tex;
        }
        if (s->used < victim->used) victim = s;
    }
    if (g_loaded_this_frame >= ART_PER_FRAME) {
        a->ui.needs_redraw = true; /* come back next frame */
        return NULL;
    }
    g_loaded_this_frame++;
    if (victim->tex) SDL_DestroyTexture(victim->tex);
    memset(victim, 0, sizeof *victim);
    victim->hash = h;
    SDL_strlcpy(victim->path, path, sizeof victim->path);
    victim->tex = load(a, path);
    victim->failed = victim->tex == NULL;
    victim->used = g_tick;
    return victim->tex;
}

void art_frame(App *a)
{
    (void)a;
    g_loaded_this_frame = 0;
    g_tick++;
}

void art_clear(void)
{
    for (int i = 0; i < ART_SLOTS; i++) {
        if (g_slots[i].tex) SDL_DestroyTexture(g_slots[i].tex);
        memset(&g_slots[i], 0, sizeof g_slots[i]);
    }
}
