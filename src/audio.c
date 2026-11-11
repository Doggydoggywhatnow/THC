#include "audio.h"

#include <SDL3/SDL.h>
#include <string.h>

#ifdef THC_HAVE_PEEP
#include "peep.h"

extern const peep_codec thc_codec_mp3;
extern const peep_codec thc_codec_vorbis;

static struct {
    SDL_AudioStream *stream;
    peep p;
    bool loaded;
    void *data;
    size_t len;
    void *work;
    SDL_AtomicInt finished;
    bool paused;
    int volume;
    double speed;
} g = { .volume = 80, .speed = 1.0 };

/* lib-Peep's own volume steps: a perceptual curve over its linear q8 gain. */
static const unsigned k_gain[21] = { 0, 1, 3, 6, 10, 16, 23, 31, 41, 52, 64, 77, 92, 108, 125, 144, 164,
                                     185, 207, 231, 256 };

static unsigned gain_for(int percent)
{
    if (percent <= 0) return 0;
    if (percent >= 100) return 256;
    float f = percent / 5.0f;
    int i = (int)f;
    float t = f - i;
    return (unsigned)(k_gain[i] + (k_gain[i + 1] - k_gain[i]) * t + 0.5f);
}

bool audio_engine_handles(const char *name)
{
    const char *dot = SDL_strrchr(name, '.');
    if (!dot) return false;
    return !SDL_strcasecmp(dot, ".flac") || !SDL_strcasecmp(dot, ".wav") || !SDL_strcasecmp(dot, ".mp3") ||
           !SDL_strcasecmp(dot, ".ogg") || !SDL_strcasecmp(dot, ".oga");
}

/* Called on SDL's audio thread with the stream locked: pull samples. */
static void SDLCALL feed(void *ud, SDL_AudioStream *s, int additional, int total)
{
    (void)ud;
    (void)total;
    static Sint16 buf[4096 * 2];
    if (!g.loaded || SDL_GetAtomicInt(&g.finished)) return;
    int frames = additional / 4;
    while (frames > 0) {
        size_t want = frames > 4096 ? 4096 : (size_t)frames;
        size_t n = peep_fill(&g.p, buf, want);
        if (n == 0) {
            SDL_SetAtomicInt(&g.finished, 1);
            break;
        }
        SDL_PutAudioStreamData(s, buf, (int)(n * 4));
        frames -= (int)n;
    }
}

static void unload_locked(void)
{
    if (g.loaded) peep_close(&g.p);
    g.loaded = false;
    SDL_free(g.data);
    SDL_free(g.work);
    g.data = g.work = NULL;
    g.len = 0;
}

bool audio_load(const char *path, char *err, size_t err_len)
{
    size_t len = 0;
    void *data = SDL_LoadFile(path, &len);
    if (!data) {
        SDL_snprintf(err, err_len, "Could not read the file");
        return false;
    }
    const char *dot = SDL_strrchr(path, '.');
    const peep_codec *codecs[4] = { NULL };
    int nc = 0;
    if (dot && !SDL_strcasecmp(dot, ".flac")) codecs[nc++] = &peep_codec_flac;
    else if (dot && !SDL_strcasecmp(dot, ".wav")) {
        codecs[nc++] = &peep_codec_wavx;
#ifdef THC_PEEP_WAVC
        codecs[nc++] = &peep_codec_wavc;
#endif
    } else if (dot && !SDL_strcasecmp(dot, ".mp3")) codecs[nc++] = &thc_codec_mp3;
    else if (dot && (!SDL_strcasecmp(dot, ".ogg") || !SDL_strcasecmp(dot, ".oga"))) codecs[nc++] = &thc_codec_vorbis;
    codecs[nc] = NULL;

    peep p;
    SDL_zero(p);
    void *work = NULL;
    int ok = peep_open(&p, codecs, data, len, NULL, 0);
    if (!ok && p.work_need) {
        work = SDL_malloc(p.work_need);
        ok = work && peep_open(&p, codecs, data, len, work, p.work_need);
    }
    if (!ok) {
        SDL_free(work);
        SDL_free(data);
        SDL_snprintf(err, err_len, "This file could not be decoded");
        return false;
    }
    peep_set_dither(&p, 2); /* TPDF dither when a >16-bit file goes to 16 bits */
    peep_set_gain(&p, gain_for(g.volume));

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, (int)p.rate };
    if (!g.stream) {
        g.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
        if (!g.stream) {
            peep_close(&p);
            SDL_free(work);
            SDL_free(data);
            SDL_snprintf(err, err_len, "No audio device: %s", SDL_GetError());
            return false;
        }
    }
    SDL_LockAudioStream(g.stream);
    unload_locked();
    g.p = p;
    g.data = data;
    g.len = len;
    g.work = work;
    g.loaded = true;
    SDL_SetAtomicInt(&g.finished, 0);
    SDL_ClearAudioStream(g.stream);
    SDL_SetAudioStreamFormat(g.stream, &spec, NULL);
    SDL_SetAudioStreamFrequencyRatio(g.stream, (float)g.speed);
    SDL_UnlockAudioStream(g.stream);
    audio_set_paused(false);
    return true;
}

void audio_unload(void)
{
    if (!g.stream) return;
    SDL_PauseAudioStreamDevice(g.stream);
    SDL_LockAudioStream(g.stream);
    unload_locked();
    SDL_ClearAudioStream(g.stream);
    SDL_UnlockAudioStream(g.stream);
    g.paused = true;
}

bool audio_loaded(void) { return g.loaded; }

void audio_set_paused(bool paused)
{
    if (!g.stream) return;
    g.paused = paused;
    if (paused) SDL_PauseAudioStreamDevice(g.stream);
    else SDL_ResumeAudioStreamDevice(g.stream);
}

bool audio_paused(void) { return g.paused; }

void audio_seek(double seconds)
{
    if (!g.loaded) return;
    SDL_LockAudioStream(g.stream);
    if (seconds < 0) seconds = 0;
    peep_seek(&g.p, (uint64_t)(seconds * g.p.rate));
    SDL_ClearAudioStream(g.stream);
    SDL_SetAtomicInt(&g.finished, 0);
    SDL_UnlockAudioStream(g.stream);
}

double audio_position(void)
{
    if (!g.loaded) return 0;
    SDL_LockAudioStream(g.stream);
    double queued = SDL_GetAudioStreamQueued(g.stream) / 4.0;
    double pos = ((double)g.p.pos - queued) / g.p.rate;
    SDL_UnlockAudioStream(g.stream);
    return pos < 0 ? 0 : pos;
}

double audio_duration(void)
{
    if (!g.loaded || !g.p.total) return 0;
    return (double)g.p.total / g.p.rate;
}

void audio_set_volume(int percent)
{
    g.volume = percent;
    if (!g.loaded) return;
    SDL_LockAudioStream(g.stream);
    peep_set_gain(&g.p, gain_for(percent));
    SDL_UnlockAudioStream(g.stream);
}

void audio_set_speed(double ratio)
{
    g.speed = ratio;
    if (g.stream) SDL_SetAudioStreamFrequencyRatio(g.stream, (float)ratio);
}

bool audio_take_finished(void)
{
    if (!g.loaded) return false;
    /* finished decoding, and everything queued has been heard */
    if (!SDL_GetAtomicInt(&g.finished)) return false;
    if (SDL_GetAudioStreamQueued(g.stream) > 0) return false;
    SDL_SetAtomicInt(&g.finished, 0);
    return true;
}

void audio_shutdown(void)
{
    if (g.stream) {
        SDL_DestroyAudioStream(g.stream);
        g.stream = NULL;
    }
    unload_locked();
}

#else /* no lib-Peep on this platform: everything goes through libmpv */

bool audio_engine_handles(const char *name) { (void)name; return false; }
bool audio_load(const char *path, char *err, size_t err_len)
{
    (void)path;
    SDL_snprintf(err, err_len, "Built without lib-Peep");
    return false;
}
void audio_unload(void) {}
bool audio_loaded(void) { return false; }
void audio_set_paused(bool paused) { (void)paused; }
bool audio_paused(void) { return true; }
void audio_seek(double seconds) { (void)seconds; }
double audio_position(void) { return 0; }
double audio_duration(void) { return 0; }
void audio_set_volume(int percent) { (void)percent; }
void audio_set_speed(double ratio) { (void)ratio; }
bool audio_take_finished(void) { return false; }
void audio_shutdown(void) {}

#endif
