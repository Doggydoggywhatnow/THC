/* audio.h: THC's own music engine. lib-Peep decodes (WAV, FLAC, and through
 * its codec interface MP3 via dr_mp3 and Ogg Vorbis via stb_vorbis), SDL3
 * plays and resamples. Formats it cannot play go to libmpv instead. */
#ifndef THC_AUDIO_H
#define THC_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

/* Can the engine play this file name (by extension)? */
bool audio_engine_handles(const char *name);

bool audio_load(const char *path, char *err, size_t err_len);
void audio_unload(void);
bool audio_loaded(void);
void audio_set_paused(bool paused);
bool audio_paused(void);
void audio_seek(double seconds);
double audio_position(void);   /* seconds actually heard */
double audio_duration(void);   /* seconds, 0 = unknown */
void audio_set_volume(int percent);
void audio_set_speed(double ratio);  /* note: also changes pitch */
/* True once after the track has played to its end. */
bool audio_take_finished(void);
void audio_shutdown(void);

#endif
