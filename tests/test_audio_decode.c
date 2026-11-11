/* Decode the fixtures in tests/data through lib-Peep (WAV, FLAC) and THC's
 * Peep codec adapters (MP3 via dr_mp3, Ogg Vorbis via stb_vorbis). */
#include "peep.h"
#include "test_util.h"

#include <math.h>
#include <stdlib.h>

extern const peep_codec thc_codec_mp3;
extern const peep_codec thc_codec_vorbis;

static unsigned char *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc((size_t)n);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    *len = (size_t)n;
    return d;
}

/* Returns frames decoded; checks rate and that there is a real signal. */
static size_t decode(const char *name, const peep_codec *codec, unsigned rate, size_t min_frames)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", THC_TEST_DATA, name);
    size_t len = 0;
    unsigned char *data = load(path, &len);
    CHECK(data != NULL);
    if (!data) return 0;
    const peep_codec *codecs[] = { codec, NULL };
    peep p;
    memset(&p, 0, sizeof p);
    void *work = NULL;
    int ok = peep_open(&p, codec ? codecs : NULL, data, len, NULL, 0);
    if (!ok && p.work_need) {
        work = malloc(p.work_need);
        ok = peep_open(&p, codec ? codecs : NULL, data, len, work, p.work_need);
    }
    CHECK(ok);
    if (!ok) {
        fprintf(stderr, "%s: peep_open failed\n", name);
        free(data);
        return 0;
    }
    CHECK(p.rate == rate);
    int16_t buf[1024 * 2];
    size_t total = 0;
    double energy = 0;
    size_t n;
    while ((n = peep_fill(&p, buf, 1024)) > 0) {
        for (size_t i = 0; i < n * 2; i++) energy += (double)buf[i] * buf[i];
        total += n;
    }
    if (total < min_frames) fprintf(stderr, "%s: only %zu frames\n", name, total);
    CHECK(total >= min_frames);
    CHECK(energy / (total * 2 + 1) > 1e5); /* a 440 Hz tone, not silence */
    /* seeking back to the start decodes again */
    peep_seek(&p, 0);
    CHECK(peep_fill(&p, buf, 256) > 0);
    peep_close(&p);
    free(work);
    free(data);
    return total;
}

int main(void)
{
    decode("tone.wav", NULL, 8000, 3900);                 /* built-in 16-bit WAV */
    decode("tone24.wav", &peep_codec_wavx, 8000, 3900);   /* 24-bit WAV codec */
    decode("tone.flac", &peep_codec_flac, 8000, 3900);    /* native FLAC */
    decode("tone.mp3", &thc_codec_mp3, 22050, 10000);     /* dr_mp3 adapter */
    decode("tone.ogg", &thc_codec_vorbis, 22050, 10000);  /* stb_vorbis adapter */

    /* The wrong codec rejects data cleanly. */
    {
        char path[512];
        snprintf(path, sizeof path, "%s/tone.flac", THC_TEST_DATA);
        size_t len;
        unsigned char *d = load(path, &len);
        const peep_codec *codecs[] = { &thc_codec_vorbis, NULL };
        peep p;
        memset(&p, 0, sizeof p);
        CHECK(d && !peep_open(&p, codecs, d, len, NULL, 0));
        free(d);
    }
    TEST_DONE();
}
