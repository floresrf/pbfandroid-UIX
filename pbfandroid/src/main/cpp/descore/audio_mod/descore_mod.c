/* descore_mod.c -- ProTracker-compatible module replayer. See descore_mod.h for the design story.
 *
 * Timing model: the song advances in "ticks". A row is `speed` ticks long; a tick is
 * 2.5/bpm seconds (the classic CIA-timer tempo: 125 bpm and speed 6 give the 50 Hz Amiga default).
 * Row data is read on tick 0 of a row; continuous effects (slides, vibrato, arpeggio, volume
 * slides) run on ticks 1..speed-1. Mixing is per output sample from the voices' current period.
 *
 * The (order, row) pair always names the row that will be read at the NEXT tick 0; play_order and
 * play_row name the row that is sounding now, which is what force_position() reports.
 */

#include "descore_mod.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "descore_mod_periods.h"

#define MAX_CHANNELS 8
#define NUM_SAMPLES 31
#define ROWS_PER_PATTERN 64
#define PAL_CLOCK 3546895.0 /* Paula clock (PAL), Hz: sample rate = clock / period */
#define PERIOD_MIN 113
#define PERIOD_MAX 856

typedef struct Sample {
	uint32_t length;      /* bytes */
	uint32_t loop_start;  /* bytes */
	uint32_t loop_length; /* bytes; <= 2 means "no loop" */
	uint8_t finetune;     /* 0..15, as stored (8..15 are negative) */
	uint8_t volume;       /* 0..64 */
	const int8_t *data;   /* points into DescoreMod::sample_mem */
} Sample;

struct DescoreMod {
	char title[21];
	Sample samples[NUM_SAMPLES];
	uint8_t song_length;
	uint8_t restart;
	uint8_t orders[128];
	int num_patterns;
	int channels;
	uint8_t *patterns;  /* num_patterns * 64 rows * channels * 4 bytes */
	int8_t *sample_mem;
};

static const uint8_t kSine[32] = {
	0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
	255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24};

/* ---- Loading -------------------------------------------------------------------------------- */

static int channels_from_tag(const uint8_t *tag)
{
	if (!memcmp(tag, "M.K.", 4) || !memcmp(tag, "M!K!", 4) || !memcmp(tag, "FLT4", 4) ||
			!memcmp(tag, "4CHN", 4)) {
		return 4;
	}
	if (!memcmp(tag, "6CHN", 4)) return 6;
	if (!memcmp(tag, "8CHN", 4) || !memcmp(tag, "OCTA", 4) || !memcmp(tag, "CD81", 4)) return 8;
	return 0;
}

DescoreMod *descore_mod_load(const uint8_t *data, size_t length)
{
	DescoreMod *mod;
	size_t pos, pattern_bytes, total_sample_bytes = 0, avail;
	int i, ch;

	if (data == NULL || length < 1084) return NULL;
	ch = channels_from_tag(data + 1080);
	if (ch == 0) return NULL;

	mod = (DescoreMod *)calloc(1, sizeof(*mod));
	if (mod == NULL) return NULL;
	memcpy(mod->title, data, 20);
	mod->channels = ch;

	for (i = 0; i < NUM_SAMPLES; ++i) {
		const uint8_t *h = data + 20 + i * 30;
		Sample *s = &mod->samples[i];
		s->length = (uint32_t)((h[22] << 8) | h[23]) * 2u;
		s->finetune = h[24] & 15;
		s->volume = h[25] > 64 ? 64 : h[25];
		s->loop_start = (uint32_t)((h[26] << 8) | h[27]) * 2u;
		s->loop_length = (uint32_t)((h[28] << 8) | h[29]) * 2u;
		total_sample_bytes += s->length;
	}
	mod->song_length = data[950];
	mod->restart = data[951];
	memcpy(mod->orders, data + 952, 128);
	if (mod->song_length == 0 || mod->song_length > 128) mod->song_length = 128;
	for (i = 0; i < 128; ++i) {
		if ((int)mod->orders[i] + 1 > mod->num_patterns) mod->num_patterns = mod->orders[i] + 1;
	}

	pattern_bytes = (size_t)mod->num_patterns * ROWS_PER_PATTERN * (size_t)ch * 4;
	pos = 1084;
	if (pos + pattern_bytes > length) { /* truncated file: refuse rather than play garbage */
		free(mod);
		return NULL;
	}
	mod->patterns = (uint8_t *)malloc(pattern_bytes);
	mod->sample_mem = (int8_t *)calloc(total_sample_bytes + 4, 1);
	if (mod->patterns == NULL || mod->sample_mem == NULL) {
		descore_mod_free(mod);
		return NULL;
	}
	memcpy(mod->patterns, data + pos, pattern_bytes);
	pos += pattern_bytes;

	avail = length - pos;
	{
		size_t off = 0;
		for (i = 0; i < NUM_SAMPLES; ++i) {
			Sample *s = &mod->samples[i];
			size_t want = s->length;
			size_t have = want <= avail ? want : avail; /* clamp a truncated last sample */
			if (have > 0) memcpy(mod->sample_mem + off, data + pos, have);
			s->data = mod->sample_mem + off;
			if (have < want) s->length = (uint32_t)have;
			off += want;
			pos += have;
			avail -= have;
			/* A loop that runs past the (possibly truncated) data is clipped to it. */
			if (s->loop_length > 2 && s->loop_start + s->loop_length > s->length) {
				if (s->loop_start >= s->length) {
					s->loop_length = 0;
				} else {
					s->loop_length = s->length - s->loop_start;
				}
			}
		}
	}
	return mod;
}

void descore_mod_free(DescoreMod *mod)
{
	if (mod == NULL) return;
	free(mod->patterns);
	free(mod->sample_mem);
	free(mod);
}

int descore_mod_song_length(const DescoreMod *mod) { return mod ? mod->song_length : 0; }
int descore_mod_channels(const DescoreMod *mod) { return mod ? mod->channels : 0; }
int descore_mod_sample_count(void) { return NUM_SAMPLES; }

/* ---- Player state --------------------------------------------------------------------------- */

typedef struct Voice {
	int sample;          /* 1..31, 0 = none */
	int active;
	uint32_t pos;        /* integer sample index */
	uint32_t frac;       /* 16-bit fraction */
	int period;          /* base period (effects modify this) */
	int out_period;      /* period after vibrato, what the mixer actually uses */
	int volume;          /* 0..64 */
	int out_volume;      /* after tremolo */
	int finetune;
	uint8_t cmd, param;

	int porta_target, porta_speed;
	int vib_pos, vib_speed, vib_depth, vib_wave;
	int trem_pos, trem_speed, trem_depth, trem_wave;
	int sample_offset;   /* last 9xx value, for memory of the high byte behavior */
	int loop_row, loop_count;
	int delay_note;      /* pending ED note: period (0 = none) */
	int last_sample;     /* sample number last named on this channel; notes with none reuse it */
	uint32_t step;       /* 16.16 sample-index increment per output sample */
	int step_period;     /* out_period that `step` was computed for */
} Voice;

struct DescoreModPlayer {
	const DescoreMod *mod;
	int rate;
	pthread_mutex_t lock;
	DescoreModOptions opt;
	DescoreModOrderFn hook;
	DescoreModJumpFn jump_hook;
	void *jump_hook_user;
	void *hook_user;

	int playing, paused, loop;
	int order, row;           /* next row to read */
	int play_order, play_row; /* row sounding now */
	int tick, speed, bpm;
	int jump_order, break_row;   /* pending, -1 = none */
	int pattern_delay;           /* extra repeats of the current row */
	int loop_jump_row;           /* E6x request, -1 = none */
	int repeat_row;              /* re-reading the same row for EEx: skip note trigger */
	double samples_left;         /* output samples remaining in the current tick */
	int master_volume;           /* 0..64 */

	Voice voices[MAX_CHANNELS];
	float lp_l, lp_r;            /* output filter state */
	float lp_alpha;
};

/* ---- Period helpers --------------------------------------------------------------------------- */

static int clamp_int(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* Index into the finetune-0 row of the note whose period is closest (>= convention of ProTracker:
 * first row entry not greater than the period). Returns -1 if the period is out of range. */
static int note_index_for_period(int period)
{
	int i;
	if (period <= 0) return -1;
	for (i = 0; i < 36; ++i) {
		if (period >= kPeriodTable[0][i]) return i;
	}
	return 35;
}

/* Period of `note` (index in the finetune-0 row) at the given finetune. */
static int period_for(int note, int finetune)
{
	return kPeriodTable[finetune & 15][clamp_int(note, 0, 35)];
}

/* A note period as found in a pattern (always a finetune-0 value) -> that note at `finetune`. */
static int apply_finetune(int period, int finetune)
{
	int n = note_index_for_period(period);
	return n < 0 ? period : period_for(n, finetune);
}

static int waveform(int wave, int pos)
{
	int v;
	switch (wave & 3) {
	case 1: /* ramp down */
		v = 255 - ((pos & 63) * 255) / 63;
		return v;
	case 2: /* square */
		return 255;
	default: /* sine (also "random", which ProTracker treats as sine here) */
		v = kSine[pos & 31];
		return (pos & 32) ? -v : v;
	}
}

/* ---- Voice control ------------------------------------------------------------------------------ */

static void voice_trigger(Voice *v, int sample, int period, const DescoreMod *mod)
{
	v->sample = sample;
	v->pos = 0;
	v->frac = 0;
	v->period = period;
	v->out_period = period;
	v->active = (sample > 0 && mod->samples[sample - 1].length > 2 && period > 0);
	if ((v->vib_wave & 4) == 0) v->vib_pos = 0;
	if ((v->trem_wave & 4) == 0) v->trem_pos = 0;
}

static void player_silence(DescoreModPlayer *p)
{
	int i;
	for (i = 0; i < MAX_CHANNELS; ++i) {
		p->voices[i].active = 0;
		p->voices[i].sample = 0;
	}
}

/* ---- Order navigation --------------------------------------------------------------------------- */

/* Moves `order` to `to`, consulting the hook. Returns 0 if the song has ended/stopped. */
static int goto_order(DescoreModPlayer *p, int to)
{
	int from = p->play_order;
	const DescoreMod *m = p->mod;
	if (to >= m->song_length) {
		if (!p->loop) {
			p->playing = 0;
			return 0;
		}
		to = (m->restart < m->song_length) ? m->restart : 0;
	}
	if (p->hook != NULL) {
		int redirected = p->hook(p->hook_user, from, to);
		if (redirected < 0) {
			p->playing = 0;
			return 0;
		}
		to = clamp_int(redirected, 0, m->song_length - 1);
	}
	p->order = to;
	return 1;
}

/* After the last tick of a row: decide which row is next. */
static void advance_row(DescoreModPlayer *p)
{
	int next_order = p->order, next_row;

	if (p->pattern_delay > 0) { /* EEx: run this same row again */
		p->pattern_delay--;
		p->repeat_row = 1;
		return; /* order/row already name the same row */
	}
	p->repeat_row = 0;

	if (p->loop_jump_row >= 0) { /* E6x pattern loop */
		p->row = p->loop_jump_row;
		p->loop_jump_row = -1;
		return;
	}
	if (p->jump_order >= 0 || p->break_row >= 0) {
		next_order = (p->jump_order >= 0) ? p->jump_order : p->order + 1;
		next_row = (p->break_row >= 0) ? p->break_row : 0;
		p->jump_order = p->break_row = -1;
		if (next_row >= ROWS_PER_PATTERN) next_row = 0;
		if (!goto_order(p, next_order)) return;
		p->row = next_row;
		return;
	}
	p->row++;
	if (p->row >= ROWS_PER_PATTERN) {
		p->row = 0;
		if (!goto_order(p, p->order + 1)) return;
	}
}

/* ---- Row processing (tick 0) ---------------------------------------------------------------------- */

static void set_speed_or_tempo(DescoreModPlayer *p, int param)
{
	if (param == 0) return; /* ProTracker stops the song here; ignoring it is the safe choice */
	if (param < 32) p->speed = param;
	else p->bpm = param;
}

static void row_effects_tick0(DescoreModPlayer *p, Voice *v, int ch_index, int has_note_reset)
{
	int x = v->param >> 4, y = v->param & 15;
	(void)ch_index;
	(void)has_note_reset;
	switch (v->cmd) {
	case 0x3:
		if (v->param) v->porta_speed = v->param;
		break;
	case 0x4:
		if (x) v->vib_speed = x;
		if (y) v->vib_depth = y;
		break;
	case 0x7:
		if (x) v->trem_speed = x;
		if (y) v->trem_depth = y;
		break;
	case 0x9: {
		const DescoreMod *m = p->mod;
		if (v->param) v->sample_offset = v->param;
		if (v->sample > 0 && v->active) {
			uint32_t off = (uint32_t)v->sample_offset << 8;
			const Sample *s = &m->samples[v->sample - 1];
			if (off >= s->length) { /* offset past the end: ProTracker plays nothing */
				v->active = 0;
			} else {
				v->pos = off;
				v->frac = 0;
			}
		}
		break;
	}
	case 0xB:
		p->jump_order = v->param;
		if (p->jump_hook != NULL) {
			int j = p->jump_hook(p->jump_hook_user, v->param);
			p->jump_order = j < 0 ? 0 : (j > 127 ? 127 : j);
		}
		break;
	case 0xC:
		v->volume = clamp_int(v->param, 0, 64);
		break;
	case 0xD:
		p->break_row = x * 10 + y;
		if (p->break_row >= ROWS_PER_PATTERN) p->break_row = 0;
		break;
	case 0xE:
		switch (x) {
		case 0x1: v->period = clamp_int(v->period - y, PERIOD_MIN, PERIOD_MAX); break;
		case 0x2: v->period = clamp_int(v->period + y, PERIOD_MIN, PERIOD_MAX); break;
		case 0x4: v->vib_wave = y; break;
		case 0x5: /* finetune: already applied by process_row when the note was triggered */
			break;
		case 0x6: /* pattern loop: E60 marks the row, E6x loops back x times */
			if (y == 0) {
				v->loop_row = p->play_row;
			} else {
				if (v->loop_count == 0) {
					v->loop_count = y;
				} else if (--v->loop_count == 0) {
					break;
				}
				p->loop_jump_row = v->loop_row;
			}
			break;
		case 0x7: v->trem_wave = y; break;
		case 0xA: v->volume = clamp_int(v->volume + y, 0, 64); break;
		case 0xB: v->volume = clamp_int(v->volume - y, 0, 64); break;
		case 0xC:
			if (y == 0) v->volume = 0;
			break;
		case 0xE: p->pattern_delay = y; break;
		default: break;
		}
		break;
	case 0xF:
		set_speed_or_tempo(p, v->param);
		break;
	default:
		break;
	}
}

static void process_row(DescoreModPlayer *p)
{
	const DescoreMod *m = p->mod;
	const uint8_t *rowdata;
	int c;

	p->play_order = p->order;
	p->play_row = p->row;
	if (p->repeat_row) return; /* EEx: notes and tick-0 effects are not re-read */

	p->jump_order = -1;
	p->break_row = -1;
	rowdata = m->patterns +
			((size_t)m->orders[p->order] * ROWS_PER_PATTERN + (size_t)p->row) * (size_t)m->channels * 4;

	for (c = 0; c < m->channels; ++c) {
		Voice *v = &p->voices[c];
		const uint8_t *b = rowdata + c * 4;
		int note = ((b[0] & 0x0F) << 8) | b[1];
		int sample = (b[0] & 0xF0) | (b[2] >> 4);
		int cmd = b[2] & 0x0F;
		int param = b[3];
		int ft;

		v->cmd = (uint8_t)cmd;
		v->param = (uint8_t)param;
		v->delay_note = 0;

		if (sample >= 1 && sample <= NUM_SAMPLES) {
			const Sample *s = &m->samples[sample - 1];
			v->last_sample = sample;
			v->volume = s->volume;
			v->finetune = s->finetune;
		}
		ft = v->finetune;
		if (cmd == 0xE && (param >> 4) == 0x5) {
			ft = param & 15;
			v->finetune = ft;
		}

		if (note) {
			int period = apply_finetune(note, ft);
			if (cmd == 0x3 || cmd == 0x5) {
				v->porta_target = period;
			} else if (cmd == 0xE && (param >> 4) == 0xD && (param & 15) > 0) {
				v->delay_note = period; /* triggered on tick y */
			} else {
				voice_trigger(v, v->last_sample, period, m);
			}
		}
		row_effects_tick0(p, v, c, note != 0);
		v->out_period = v->period;
		v->out_volume = v->volume;
	}
}

static void process_tick(DescoreModPlayer *p)
{
	const DescoreMod *m = p->mod;
	int c;
	for (c = 0; c < m->channels; ++c) {
		Voice *v = &p->voices[c];
		int x = v->param >> 4, y = v->param & 15;

		v->out_period = v->period;
		v->out_volume = v->volume;

		switch (v->cmd) {
		case 0x0:
			if (v->param != 0) {
				int idx = 0, add, i;
				for (i = 0; i < 36; ++i) {
					if (v->period >= kPeriodTable[v->finetune & 15][i]) {
						idx = i;
						break;
					}
					idx = 35;
				}
				add = (p->tick % 3 == 0) ? 0 : ((p->tick % 3 == 1) ? x : y);
				v->out_period = period_for(idx + add, v->finetune);
			}
			break;
		case 0x1:
			v->period = clamp_int(v->period - v->param, PERIOD_MIN, PERIOD_MAX);
			v->out_period = v->period;
			break;
		case 0x2:
			v->period = clamp_int(v->period + v->param, PERIOD_MIN, PERIOD_MAX);
			v->out_period = v->period;
			break;
		case 0x3:
		case 0x5:
			if (v->porta_target > 0) {
				if (v->period > v->porta_target) {
					v->period -= v->porta_speed;
					if (v->period < v->porta_target) v->period = v->porta_target;
				} else if (v->period < v->porta_target) {
					v->period += v->porta_speed;
					if (v->period > v->porta_target) v->period = v->porta_target;
				}
			}
			v->out_period = v->period;
			if (v->cmd == 0x5) v->volume = clamp_int(v->volume + (x ? x : -y), 0, 64);
			break;
		case 0x4:
		case 0x6:
			v->out_period = v->period + ((waveform(v->vib_wave, v->vib_pos) * v->vib_depth) >> 7);
			v->out_period = clamp_int(v->out_period, PERIOD_MIN, PERIOD_MAX);
			v->vib_pos = (v->vib_pos + v->vib_speed) & 63;
			if (v->cmd == 0x6) v->volume = clamp_int(v->volume + (x ? x : -y), 0, 64);
			break;
		case 0x7:
			v->out_volume = clamp_int(
					v->volume + ((waveform(v->trem_wave, v->trem_pos) * v->trem_depth) >> 6), 0, 64);
			v->trem_pos = (v->trem_pos + v->trem_speed) & 63;
			break;
		case 0xA:
			v->volume = clamp_int(v->volume + (x ? x : -y), 0, 64);
			break;
		case 0xE:
			switch (x) {
			case 0x9: /* retrigger every y ticks */
				if (y > 0 && (p->tick % y) == 0) {
					v->pos = 0;
					v->frac = 0;
					v->active = (v->sample > 0 && m->samples[v->sample - 1].length > 2);
				}
				break;
			case 0xC: /* note cut */
				if (p->tick == y) v->volume = 0;
				break;
			case 0xD: /* note delay */
				if (p->tick == y && v->delay_note > 0) {
					voice_trigger(v, v->last_sample, v->delay_note, m);
					v->delay_note = 0;
				}
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		if (v->cmd != 0x7) v->out_volume = v->volume;
	}
}

/* One song tick: read a row on tick 0, run continuous effects on the rest, then step forward. */
static void advance_tick(DescoreModPlayer *p)
{
	if (p->tick == 0) {
		process_row(p);
	} else {
		process_tick(p);
	}
	p->tick++;
	if (p->tick >= p->speed) {
		p->tick = 0;
		advance_row(p);
	}
}

/* ---- Mixing ---------------------------------------------------------------------------------------- */

static void mix_frame(DescoreModPlayer *p, float *out_l, float *out_r)
{
	const DescoreMod *m = p->mod;
	float l = 0.0f, r = 0.0f;
	float same = 0.5f + 0.5f * (float)p->opt.stereo_separation / 100.0f;
	float other = 1.0f - same;
	float base = (m->channels <= 4 ? 2.0f : 1.0f) * ((float)p->master_volume / 64.0f);
	int c;

	for (c = 0; c < m->channels; ++c) {
		Voice *v = &p->voices[c];
		const Sample *s;
		float sample_value, amp, gl, gr;
		uint32_t idx;
		int left_side;

		if (!v->active || v->sample < 1) continue;
		s = &m->samples[v->sample - 1];
		idx = v->pos;
		if (idx >= s->length) {
			v->active = 0;
			continue;
		}
		sample_value = (float)s->data[idx];
		if (p->opt.linear_interp) {
			uint32_t nxt = idx + 1;
			float next_value;
			if (s->loop_length > 2 && nxt >= s->loop_start + s->loop_length) nxt = s->loop_start;
			next_value = (nxt < s->length) ? (float)s->data[nxt] : sample_value;
			sample_value += (next_value - sample_value) * ((float)v->frac / 65536.0f);
		}
		amp = sample_value * (float)v->out_volume * base;
		left_side = ((c & 3) == 0 || (c & 3) == 3);
		gl = left_side ? same : other;
		gr = left_side ? other : same;
		l += amp * gl;
		r += amp * gr;

		if (v->step_period != v->out_period) {
			v->step_period = v->out_period;
			v->step = v->out_period > 0
					? (uint32_t)((PAL_CLOCK / (double)v->out_period) * 65536.0 / (double)p->rate)
					: 0;
		}
		v->frac += v->step;
		v->pos += v->frac >> 16;
		v->frac &= 0xFFFF;
		if (s->loop_length > 2) {
			while (v->pos >= s->loop_start + s->loop_length) v->pos -= s->loop_length;
		} else if (v->pos >= s->length) {
			v->active = 0;
		}
	}
	*out_l = l;
	*out_r = r;
}

size_t descore_modplayer_render(void *player, int16_t *out, size_t frames)
{
	DescoreModPlayer *p = (DescoreModPlayer *)player;
	size_t i;

	if (p == NULL) return 0;
	pthread_mutex_lock(&p->lock);
	for (i = 0; i < frames; ++i) {
		float l, r;
		if (p->playing && !p->paused) {
			if (p->samples_left <= 0.0) {
				advance_tick(p);
				p->samples_left += (double)p->rate * 2.5 / (double)p->bpm;
			}
			p->samples_left -= 1.0;
		}
		mix_frame(p, &l, &r);
		if (p->opt.amiga_filter) {
			p->lp_l += (l - p->lp_l) * p->lp_alpha;
			p->lp_r += (r - p->lp_r) * p->lp_alpha;
			l = p->lp_l;
			r = p->lp_r;
		}
		out[i * 2] = (int16_t)clamp_int((int)l, -32768, 32767);
		out[i * 2 + 1] = (int16_t)clamp_int((int)r, -32768, 32767);
	}
	pthread_mutex_unlock(&p->lock);
	return frames;
}

/* ---- Public control API ------------------------------------------------------------------------------ */

DescoreModPlayer *descore_modplayer_create(const DescoreMod *mod, int sample_rate)
{
	DescoreModPlayer *p;
	if (mod == NULL || sample_rate <= 0) return NULL;
	p = (DescoreModPlayer *)calloc(1, sizeof(*p));
	if (p == NULL) return NULL;
	p->mod = mod;
	p->rate = sample_rate;
	pthread_mutex_init(&p->lock, NULL);
	p->opt.stereo_separation = 50;
	p->opt.linear_interp = 1;
	p->opt.amiga_filter = 0;
	p->lp_alpha = 1.0f - expf(-2.0f * 3.14159265f * 4900.0f / (float)sample_rate);
	p->speed = 6;
	p->bpm = 125;
	p->jump_order = p->break_row = p->loop_jump_row = -1;
	p->master_volume = 64;
	return p;
}

void descore_modplayer_destroy(DescoreModPlayer *p)
{
	if (p == NULL) return;
	pthread_mutex_destroy(&p->lock);
	free(p);
}

void descore_modplayer_set_options(DescoreModPlayer *p, const DescoreModOptions *options)
{
	if (p == NULL || options == NULL) return;
	pthread_mutex_lock(&p->lock);
	p->opt = *options;
	p->opt.stereo_separation = clamp_int(p->opt.stereo_separation, 0, 100);
	pthread_mutex_unlock(&p->lock);
}

void descore_modplayer_play(DescoreModPlayer *p, int order, int loop)
{
	int i;
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	player_silence(p);
	for (i = 0; i < MAX_CHANNELS; ++i) {
		Voice *v = &p->voices[i];
		v->porta_target = v->porta_speed = 0;
		v->vib_pos = v->vib_speed = v->vib_depth = v->vib_wave = 0;
		v->trem_pos = v->trem_speed = v->trem_depth = v->trem_wave = 0;
		v->loop_row = v->loop_count = 0;
		v->cmd = v->param = 0;
		v->last_sample = 0;
		v->volume = 0;
		v->out_volume = 0;
	}
	p->order = p->play_order = clamp_int(order, 0, p->mod->song_length - 1);
	p->row = p->play_row = 0;
	p->tick = 0;
	p->speed = 6;
	p->bpm = 125;
	p->jump_order = p->break_row = p->loop_jump_row = -1;
	p->pattern_delay = 0;
	p->repeat_row = 0;
	p->samples_left = 0.0;
	p->loop = loop;
	p->paused = 0;
	p->playing = 1;
	pthread_mutex_unlock(&p->lock);
}

void descore_modplayer_stop(DescoreModPlayer *p)
{
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	p->playing = 0;
	player_silence(p);
	pthread_mutex_unlock(&p->lock);
}

void descore_modplayer_pause(DescoreModPlayer *p, int paused)
{
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	p->paused = paused ? 1 : 0;
	pthread_mutex_unlock(&p->lock);
}

int descore_modplayer_playing(DescoreModPlayer *p)
{
	int r;
	if (p == NULL) return 0;
	pthread_mutex_lock(&p->lock);
	r = p->playing;
	pthread_mutex_unlock(&p->lock);
	return r;
}

void descore_modplayer_set_volume(DescoreModPlayer *p, int volume)
{
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	p->master_volume = clamp_int(volume, 0, 64);
	pthread_mutex_unlock(&p->lock);
}

int descore_modplayer_force_position(DescoreModPlayer *p, int order)
{
	int old;
	if (p == NULL) return -1;
	pthread_mutex_lock(&p->lock);
	old = p->playing ? p->play_order : -1;
	p->order = clamp_int(order, 0, p->mod->song_length - 1);
	p->row = 0;
	p->tick = 0;
	p->jump_order = p->break_row = p->loop_jump_row = -1;
	p->pattern_delay = 0;
	p->repeat_row = 0;
	p->samples_left = 0.0; /* start the new row on the very next output sample */
	pthread_mutex_unlock(&p->lock);
	return old;
}

int descore_modplayer_order(DescoreModPlayer *p)
{
	int r;
	if (p == NULL) return -1;
	pthread_mutex_lock(&p->lock);
	r = p->play_order;
	pthread_mutex_unlock(&p->lock);
	return r;
}

int descore_modplayer_row(DescoreModPlayer *p)
{
	int r;
	if (p == NULL) return -1;
	pthread_mutex_lock(&p->lock);
	r = p->play_row;
	pthread_mutex_unlock(&p->lock);
	return r;
}

static void trigger_locked(DescoreModPlayer *p, int channel, int sample, int period, int volume)
{
	Voice *v;
	const Sample *s;
	if (channel < 0 || channel >= p->mod->channels || sample < 1 || sample > NUM_SAMPLES) return;
	v = &p->voices[channel];
	s = &p->mod->samples[sample - 1];
	if (period <= 0) period = period_for(12, s->finetune); /* C-2 */
	voice_trigger(v, sample, period, p->mod);
	v->finetune = s->finetune;
	v->volume = (volume < 0) ? s->volume : clamp_int(volume, 0, 64);
	v->out_volume = v->volume;
	v->cmd = 0;   /* isolate from whatever effect the song's current row left on this channel */
	v->param = 0;
	v->delay_note = 0;
}

void descore_modplayer_trigger_sample(DescoreModPlayer *p, int channel, int sample, int period,
		int volume)
{
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	trigger_locked(p, channel, sample, period, volume);
	pthread_mutex_unlock(&p->lock);
}

void descore_modplayer_trigger_note(DescoreModPlayer *p, int channel, int sample, int note,
		int volume)
{
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	if (sample >= 1 && sample <= NUM_SAMPLES) {
		trigger_locked(p, channel, sample,
				period_for(note, p->mod->samples[sample - 1].finetune), volume);
	}
	pthread_mutex_unlock(&p->lock);
}

void descore_modplayer_set_jump_hook(DescoreModPlayer *p, DescoreModJumpFn fn, void *user)
{
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	p->jump_hook = fn;
	p->jump_hook_user = user;
	pthread_mutex_unlock(&p->lock);
}

void descore_modplayer_set_order_hook(DescoreModPlayer *p, DescoreModOrderFn fn, void *user)
{
	if (p == NULL) return;
	pthread_mutex_lock(&p->lock);
	p->hook = fn;
	p->hook_user = user;
	pthread_mutex_unlock(&p->lock);
}
