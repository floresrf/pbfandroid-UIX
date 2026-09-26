/* descore_mod.h -- ProTracker-compatible .MOD replayer for descore-shell ports.
 *
 * Written from the public ProTracker module format description (4-8 channel Amiga modules, "M.K."
 * and friends) rather than derived from any other player, so it has no third-party license terms
 * beyond the project's own. The finetune period table it uses is the standard ProTracker one, and
 * was cross-checked against the table embedded in Pinball Fantasies' own SB16.SDR sound driver.
 *
 * Why not just decode the module to PCM and play that: Pinball Fantasies (and other 21st Century /
 * Amiga-lineage games) drive the module live. A "jingle" is a jump to another song position
 * (order), and a sound effect is a module sample triggered on a chosen channel at a chosen period
 * and volume, mixed over the running music. Both need a real replayer with a control API, which
 * is what this is:
 *
 *   descore_modplayer_force_position()  - jump to an order now, returning the order it left
 *   descore_modplayer_trigger_sample()  - play sample N on channel C at a period/volume
 *   descore_modplayer_set_order_hook()  - be told (and optionally redirect) every order change
 *
 * Plug the player into the descore mixer with
 *   descore_audio_add_source(descore_modplayer_render, player);
 * All control functions are thread-safe against the render callback (one internal mutex). The
 * order hook is called from inside the render callback, with that mutex held: it must not call any
 * descore_modplayer_* function. */
#ifndef DESCORE_MOD_H
#define DESCORE_MOD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DescoreMod DescoreMod;
typedef struct DescoreModPlayer DescoreModPlayer;

/* Parses a module image (copied, so `data` need not stay alive). Returns NULL if it is not a
 * supported 4-8 channel ProTracker-style module. */
DescoreMod *descore_mod_load(const uint8_t *data, size_t length);
void descore_mod_free(DescoreMod *mod);

int descore_mod_song_length(const DescoreMod *mod); /* number of orders */
int descore_mod_channels(const DescoreMod *mod);
int descore_mod_sample_count(void);                 /* always 31 */

/* Playback options. */
typedef struct DescoreModOptions {
	int stereo_separation; /* 0 = mono ... 100 = hard Amiga panning (L R R L). Default 50. */
	int linear_interp;     /* 1 = linear resampling, 0 = nearest (raw Paula-style). Default 1. */
	int amiga_filter;      /* 1 = gentle low-pass like the A500 output stage. Default 0. */
} DescoreModOptions;

/* The mod must outlive the player. sample_rate is the mixer rate (DESCORE_AUDIO_RATE). */
DescoreModPlayer *descore_modplayer_create(const DescoreMod *mod, int sample_rate);
void descore_modplayer_destroy(DescoreModPlayer *p); /* remove it from the mixer first */

void descore_modplayer_set_options(DescoreModPlayer *p, const DescoreModOptions *options);

/* Start (or restart) the song at `order`. loop != 0: wrap at the end forever; otherwise stop. */
void descore_modplayer_play(DescoreModPlayer *p, int order, int loop);
void descore_modplayer_stop(DescoreModPlayer *p);   /* halts the song and silences all voices */
void descore_modplayer_pause(DescoreModPlayer *p, int paused);
int descore_modplayer_playing(DescoreModPlayer *p);

/* 0..64 (ProTracker volume). Applies to the whole player, music and triggered samples. */
void descore_modplayer_set_volume(DescoreModPlayer *p, int volume);

/* Jump to `order` (row 0) immediately, without silencing voices. Returns the order that was
 * playing, or -1 if the song was stopped. This is the "jingle" primitive. */
int descore_modplayer_force_position(DescoreModPlayer *p, int order);
int descore_modplayer_order(DescoreModPlayer *p);
int descore_modplayer_row(DescoreModPlayer *p);

/* Play sample `sample` (1..31) on `channel` (0-based) at raw Amiga `period` (113..856 typical;
 * <=0 uses the note C-2 for that sample's finetune) and `volume` (0..64, <0 = sample default),
 * from the start of the sample. Works whether or not the song is playing. */
void descore_modplayer_trigger_sample(DescoreModPlayer *p, int channel, int sample, int period,
		int volume);
/* Same, but note is an index 0..35 (C-1 .. B-3) into the sample's finetune period row. */
void descore_modplayer_trigger_note(DescoreModPlayer *p, int channel, int sample, int note,
		int volume);

/* Called just before the song moves from order `from` to order `to` (a normal advance, a Bxx/Dxx
 * jump, or the end-of-song wrap). Return `to` to allow it, another order to redirect, or a
 * negative value to stop the song. */
typedef int (*DescoreModOrderFn)(void *user, int from, int to);
void descore_modplayer_set_order_hook(DescoreModPlayer *p, DescoreModOrderFn fn, void *user);

/* Called when a Bxx (position jump) effect is processed, with its parameter; return the order to
 * actually jump to. This is the hook Pinball Fantasies' own sound driver offers its "jingle handler"
 * (the game decides, at every Bxx, whether a jingle has finished and which tune follows). Same
 * rules as the order hook: called inside the render callback with the player's mutex held. */
typedef int (*DescoreModJumpFn)(void *user, int param);
void descore_modplayer_set_jump_hook(DescoreModPlayer *p, DescoreModJumpFn fn, void *user);

/* DescoreAudioSourceFn-compatible render callback (user = the DescoreModPlayer). */
size_t descore_modplayer_render(void *player, int16_t *out, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
