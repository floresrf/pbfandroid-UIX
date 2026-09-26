/* pbf_audio.h -- the game's INT 66h sound driver, on top of the descore ProTracker replayer.
 *
 * The original SB16 driver mixes in the game's idle loop and calls the game's jingle handler from inside
 * the mixer.  Here the game thread renders one video frame of audio per emulated frame into a ring, and
 * the descore mixer just drains the ring; that keeps every call into the translated game (the jingle
 * handler, run when the module executes a Bxx position jump) on the game thread. */
#ifndef PBF_AUDIO_H
#define PBF_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void pbf_audio_load(const char *name, const uint8_t *data, size_t size);
void pbf_audio_play(int mode);
void pbf_audio_stop(void);
void pbf_audio_pause(int paused);
void pbf_audio_volume(int vol);
void pbf_audio_jump(int order);
int  pbf_audio_position(void);
void pbf_audio_effect(int sample, int note, int volume, int channel);
void pbf_audio_frame(void);
void pbf_audio_shutdown(void);
/* mixer source (descore DescoreAudioSourceFn signature) */
size_t pbf_audio_pull(void *user, int16_t *out, size_t frames);
/* install the audio callbacks into pbf_plat (snd_*) */
void pbf_audio_install(void);
#ifdef __cplusplus
}
#endif
#endif
