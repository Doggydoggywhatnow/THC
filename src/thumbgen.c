/* thumbgen.c: make one video thumbnail with a hidden libmpv instance.
 * No window and no sound: mpv's "image" video output writes a frame to a JPEG file.
 * Read-only on the video; the result goes wherever the caller says (THC's cache). */
#include "app.h"
#include "stb_image.h"
#include "thumbs.h"

#define GEN_TIMEOUT_MS 20000

static bool exists(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* Brightness of a JPEG on disk, or -1 if it cannot be read. */
static double jpeg_brightness(const char *path)
{
    size_t len = 0;
    void *data = SDL_LoadFile(path, &len);
    if (!data) return -1;
    int w, h, c;
    unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    SDL_free(data);
    if (!px) return -1;
    double b = thumb_brightness(px, w, h);
    stbi_image_free(px);
    return b;
}

/* One frame at `start` ("10%" or seconds) into `frame_dir`/00000001.jpg. */
static bool grab(const MpvApi *api, const char *video, const char *frame_dir, const char *start)
{
    mpv_handle *h = api->create();
    if (!h) return false;
    api->set_option_string(h, "config", "no");        /* never read any mpv.conf */
    api->set_option_string(h, "terminal", "no");
    api->set_option_string(h, "idle", "no");
    api->set_option_string(h, "vo", "image");         /* write the frame to a file: no window */
    api->set_option_string(h, "vo-image-format", "jpg");
    api->set_option_string(h, "vo-image-jpeg-quality", "82");
    api->set_option_string(h, "vo-image-outdir", frame_dir);
    api->set_option_string(h, "frames", "1");
    api->set_option_string(h, "start", start);
    api->set_option_string(h, "vf", "lavfi=[scale='min(480,iw)':-2]");   /* never upscale */
    api->set_option_string(h, "aid", "no");
    api->set_option_string(h, "sid", "no");
    api->set_option_string(h, "ao", "null");
    api->set_option_string(h, "hwdec", "no");
    api->set_option_string(h, "load-scripts", "no");
    api->set_option_string(h, "ytdl", "no");          /* local files only */
    api->set_option_string(h, "resume-playback", "no");
    api->set_option_string(h, "save-position-on-quit", "no");
    api->set_option_string(h, "input-default-bindings", "no");
    if (api->initialize(h) < 0) {
        api->terminate_destroy(h);
        return false;
    }
    const char *cmd[] = { "loadfile", video, NULL };
    bool ok = api->command(h, cmd) >= 0;
    Uint64 deadline = SDL_GetTicks() + GEN_TIMEOUT_MS;
    while (ok && SDL_GetTicks() < deadline) {
        mpv_event *ev = api->wait_event(h, 0.2);
        if (ev->event_id == MPV_EVENT_END_FILE || ev->event_id == MPV_EVENT_SHUTDOWN) break;
    }
    api->terminate_destroy(h);
    char frame[THC_PATH_MAX + 32];
    SDL_snprintf(frame, sizeof frame, "%s/00000001.jpg", frame_dir);
    return exists(frame);
}

/* Make a thumbnail of `video` at `out`. Tries a few points in the film and keeps the
 * first one that is not black (or the brightest if all are). */
bool thumb_generate(const MpvApi *api, const char *video, const char *out)
{
    char tmp[THC_PATH_MAX + 16], frame[THC_PATH_MAX + 40], best[THC_PATH_MAX + 16];
    SDL_snprintf(tmp, sizeof tmp, "%s.work", out);
    SDL_snprintf(best, sizeof best, "%s.best", out);
    SDL_snprintf(frame, sizeof frame, "%s/00000001.jpg", tmp);
    SDL_CreateDirectory(tmp);
    static const char *const at[] = { "10%", "35%", "65%" };
    double best_b = -1;
    for (int i = 0; i < 3; i++) {
        SDL_RemovePath(frame);
        if (!grab(api, video, tmp, at[i])) continue;
        double b = jpeg_brightness(frame);
        if (b > best_b) {
            best_b = b;
            SDL_RemovePath(best);
            SDL_RenamePath(frame, best);
        }
        if (b >= THUMB_DARK_LIMIT) break;
    }
    SDL_RemovePath(frame);
    SDL_RemovePath(tmp);
    if (best_b < 0) return false;
    SDL_RemovePath(out);
    bool ok = SDL_RenamePath(best, out);
    if (!ok) SDL_RemovePath(best);
    return ok;
}

