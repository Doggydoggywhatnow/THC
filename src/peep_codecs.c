/* peep_codecs.c: MP3 and Ogg Vorbis for lib-Peep, through its four-function
 * codec interface (see lib-Peep's AGENTS.md, "Writing a codec").
 *   MP3:  dr_mp3 by David Reid (public domain / MIT-0)
 *   Ogg:  stb_vorbis by Sean Barrett (public domain / MIT)
 * Both allocate, so peep_open (which calls open) runs off the audio thread. */
#include "peep.h"

#include <stdlib.h>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

/* ---- MP3 ---- */

static int mp3_open(peep *p, const void *data, size_t len)
{
    drmp3 *m = malloc(sizeof *m);
    if (!m) return 0;
    if (!drmp3_init_memory(m, data, len, NULL)) {
        free(m);
        return 0;
    }
    if (m->channels < 1 || m->channels > 8 || m->sampleRate == 0) {
        drmp3_uninit(m);
        free(m);
        return 0;
    }
    p->ctx = m;
    p->rate = m->sampleRate;
    p->channels = m->channels;
    p->bits = 16;
    p->total = drmp3_get_pcm_frame_count(m);
    drmp3_seek_to_pcm_frame(m, 0);
    return 1;
}

static size_t mp3_read(peep *p, int16_t *out, size_t frames)
{
    return (size_t)drmp3_read_pcm_frames_s16((drmp3 *)p->ctx, frames, out);
}

static void mp3_seek(peep *p, uint64_t frame)
{
    drmp3_seek_to_pcm_frame((drmp3 *)p->ctx, frame);
}

static void mp3_close(peep *p)
{
    if (!p->ctx) return;
    drmp3_uninit((drmp3 *)p->ctx);
    free(p->ctx);
    p->ctx = NULL;
}

const peep_codec thc_codec_mp3 = { mp3_open, mp3_read, mp3_seek, mp3_close, NULL };

/* ---- Ogg Vorbis ---- */

static int vorbis_open(peep *p, const void *data, size_t len)
{
    const unsigned char *d = data;
    if (len < 4 || d[0] != 'O' || d[1] != 'g' || d[2] != 'g' || d[3] != 'S') return 0;
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory(d, (int)len, &err, NULL);
    if (!v) return 0;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.channels < 1 || info.channels > 8) {
        stb_vorbis_close(v);
        return 0;
    }
    p->ctx = v;
    p->rate = info.sample_rate;
    p->channels = (uint32_t)info.channels;
    p->bits = 16;
    p->total = stb_vorbis_stream_length_in_samples(v);
    return 1;
}

static size_t vorbis_read(peep *p, int16_t *out, size_t frames)
{
    stb_vorbis *v = p->ctx;
    int ch = (int)p->channels;
    return (size_t)stb_vorbis_get_samples_short_interleaved(v, ch, out, (int)(frames * (size_t)ch));
}

static void vorbis_seek(peep *p, uint64_t frame)
{
    stb_vorbis_seek((stb_vorbis *)p->ctx, (unsigned int)frame);
}

static void vorbis_close(peep *p)
{
    if (!p->ctx) return;
    stb_vorbis_close((stb_vorbis *)p->ctx);
    p->ctx = NULL;
}

const peep_codec thc_codec_vorbis = { vorbis_open, vorbis_read, vorbis_seek, vorbis_close, NULL };
