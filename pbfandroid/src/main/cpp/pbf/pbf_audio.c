#include "pbf_audio.h"
#include "pbfrt.h"
#include "../descore/audio_mod/descore_mod.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100
#define RING 16384              /* frames, power of two */
#define FRAME_US 16683

static DescoreMod *g_mod;
static DescoreModPlayer *g_player;
static pthread_mutex_t g_ring_lock = PTHREAD_MUTEX_INITIALIZER;
static int16_t g_ring[RING * 2];
static unsigned g_rd, g_wr;     /* frame counters */
static double g_carry;
static int g_volume = 64;

static int jump_hook(void *user, int param) { (void)user; return pbf_snd_jump_event(param); }

void pbf_audio_load(const char *name, const uint8_t *data, size_t size)
{
	(void)name;
	DescoreMod *m = descore_mod_load(data, size);
	if (!m) return;
	DescoreModPlayer *old = g_player; DescoreMod *oldm = g_mod;
	pthread_mutex_lock(&g_ring_lock);
	g_player = NULL;
	pthread_mutex_unlock(&g_ring_lock);
	if (old) descore_modplayer_destroy(old);
	if (oldm) descore_mod_free(oldm);
	g_mod = m;
	DescoreModPlayer *p = descore_modplayer_create(m, RATE);
	if (!p) return;
	descore_modplayer_set_jump_hook(p, jump_hook, NULL);
	descore_modplayer_set_volume(p, g_volume);
	g_player = p;
}

void pbf_audio_play(int mode) { (void)mode; if (g_player) descore_modplayer_play(g_player, 0, 1); }
void pbf_audio_stop(void) { if (g_player) descore_modplayer_stop(g_player); }
void pbf_audio_pause(int paused) { if (g_player) descore_modplayer_pause(g_player, paused); }
void pbf_audio_volume(int vol)
{
	g_volume = vol < 0 ? 0 : (vol > 64 ? 64 : vol);
	if (g_player) descore_modplayer_set_volume(g_player, g_volume);
}
void pbf_audio_jump(int order)
{
	if (!g_player) return;
	int n = descore_mod_song_length(g_mod);
	if (order < 0) order = 0;
	if (order >= n) order = n - 1;
	descore_modplayer_force_position(g_player, order);
}
int pbf_audio_position(void) { return g_player ? descore_modplayer_order(g_player) : 0; }
void pbf_audio_effect(int sample, int note, int volume, int channel)
{
	if (!g_player) return;
	if (channel < 0 || channel >= descore_mod_channels(g_mod)) channel = descore_mod_channels(g_mod) - 1;
	descore_modplayer_trigger_note(g_player, channel, sample, note, volume ? volume : -1);
}

static void push(const int16_t *s, size_t n)
{
	pthread_mutex_lock(&g_ring_lock);
	size_t room = RING - (g_wr - g_rd);
	if (n > room) n = room;
	for (size_t i = 0; i < n; i++) {
		size_t k = (g_wr + i) & (RING - 1);
		g_ring[k * 2] = s[i * 2]; g_ring[k * 2 + 1] = s[i * 2 + 1];
	}
	g_wr += (unsigned)n;
	pthread_mutex_unlock(&g_ring_lock);
}

void pbf_audio_frame(void)
{
	if (!g_player) return;
	unsigned fill;
	pthread_mutex_lock(&g_ring_lock);
	fill = g_wr - g_rd;
	pthread_mutex_unlock(&g_ring_lock);
	g_carry += (double)RATE * FRAME_US / 1e6;
	size_t n = (size_t)g_carry;
	g_carry -= (double)n;
	if (fill < 1200) n += 1200 - fill;        /* keep about 30 ms in flight */
	if (fill > 6000) return;                   /* ahead of the mixer: skip */
	static int16_t tmp[4096 * 2];
	while (n > 0) {
		size_t c = n > 4096 ? 4096 : n;
		size_t got = descore_modplayer_render(g_player, tmp, c);
		if (got < c) memset(tmp + got * 2, 0, (c - got) * 4);
		push(tmp, c);
		n -= c;
	}
}

size_t pbf_audio_pull(void *user, int16_t *out, size_t frames)
{
	(void)user;
	pthread_mutex_lock(&g_ring_lock);
	size_t avail = g_wr - g_rd;
	size_t n = frames < avail ? frames : avail;
	for (size_t i = 0; i < n; i++) {
		size_t k = (g_rd + i) & (RING - 1);
		out[i * 2] = g_ring[k * 2]; out[i * 2 + 1] = g_ring[k * 2 + 1];
	}
	g_rd += (unsigned)n;
	pthread_mutex_unlock(&g_ring_lock);
	return n;
}

void pbf_audio_shutdown(void)
{
	pthread_mutex_lock(&g_ring_lock);
	DescoreModPlayer *p = g_player; DescoreMod *m = g_mod;
	g_player = NULL; g_mod = NULL; g_rd = g_wr = 0;
	pthread_mutex_unlock(&g_ring_lock);
	if (p) descore_modplayer_destroy(p);
	if (m) descore_mod_free(m);
}

void pbf_audio_install(void)
{
	pbf_plat.snd_load_module = pbf_audio_load;
	pbf_plat.snd_play = pbf_audio_play;
	pbf_plat.snd_stop = pbf_audio_stop;
	pbf_plat.snd_pause = pbf_audio_pause;
	pbf_plat.snd_volume = pbf_audio_volume;
	pbf_plat.snd_jump = pbf_audio_jump;
	pbf_plat.snd_position = pbf_audio_position;
	pbf_plat.snd_effect = pbf_audio_effect;
	pbf_plat.snd_frame = pbf_audio_frame;
}
