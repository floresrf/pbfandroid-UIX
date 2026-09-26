/* pbfrt.h -- runtime services for the translated Pinball Fantasies engine (DOS/BIOS/VGA/driver emulation). */
#ifndef PBFRT_H
#define PBFRT_H
#include "x86rt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- tables emitted by the translator (<prefix>data.c / <prefix>dispatch.c) ------------------------ */
typedef struct { const char *seg; uint16_t off, len; const uint8_t *data; } PbfInitRange;
typedef struct { const char *seg; uint16_t off; uint8_t size; } PbfKeep;
typedef struct { const char *seg; uint16_t off; const char *target; int16_t add; } PbfSegFix;
typedef struct { uint16_t off; x_fn fn; } PbfDisp;
typedef struct { const char *name; uint16_t *segvar; const PbfDisp *tab; int n; } PbfDispSeg;
typedef struct { const char *name; uint16_t *var; uint16_t prg; } PbfSegVar;    /* prg = segment value in the PRG image */

typedef struct { const char *seg; uint16_t from, to, len; } PbfExtCopy;   /* bytes taken from the shipped data segment (PRG layout) into the source layout */
typedef struct {
	const PbfInitRange *init;
	const PbfKeep *keep;
	const PbfSegFix *segfix;
	const PbfDispSeg *disp;
	const PbfSegVar *segvars;
	x_fn entry;                 /* FANTASIES / START */
	const char *name;
	const PbfExtCopy *extcopy;  /* optional */
	uint16_t data_seg;          /* optional: run DATA in this (free) segment instead of the shipped one */
	uint16_t chk_off;           /* optional: DATA offset of the intro's protection checksum word (answered back through CMOS 2Eh/2Fh) */
} PbfProgram;

/* the launcher (START.EXE): intro/table chooser + the four tables */
typedef struct { const PbfProgram *prog; const char *prg; } PbfLaunchEntry;
/* start_table: 0 = show the intro/chooser first, 1..4 = go straight into that table (then back to the chooser) */
void pbf_session(const PbfLaunchEntry *intro, const PbfLaunchEntry tables[4], int start_table);
int pbf_running_intro(void);   /* 1 while the intro/chooser (not a table) is running */
void pbf_config_get(int cfg[6]);
void pbf_config_set(const int cfg[6]);

/* ---- platform interface (implemented by the host: Android/Descore glue or the desktop test harness) - */
typedef struct {
	uint64_t (*now_us)(void);
	/* a finished frame: w*h palette indices (stride w) and the 256*3 VGA DAC palette (6-bit values) */
	void (*present)(const uint8_t *idx, const uint8_t *pal6, int w, int h);
	/* the game has nothing to do until the next frame: sleep up to this long (optional) */
	void (*sleep_us)(uint32_t us);
	/* read a whole game file by (DOS) name; caller frees *data with free(); returns 0 on success */
	int (*read_file)(const char *name, uint8_t **data, size_t *size);
	/* persist a small file (high scores); returns 0 on success */
	int (*write_file)(const char *name, const uint8_t *data, size_t size);
	void (*log)(const char *fmt, ...);
	/* sound driver events (INT 66h) */
	void (*snd_load_module)(const char *name, const uint8_t *data, size_t size);
	void (*snd_play)(int mode);
	void (*snd_stop)(void);
	void (*snd_pause)(int paused);
	void (*snd_effect)(int sample, int note, int volume, int channel);
	void (*snd_frame)(void);              /* once per emulated frame, game thread: render this frame's audio */
	void (*snd_volume)(int vol);
	void (*snd_jump)(int order);          /* force song position */
	int  (*snd_position)(void);
	int  (*poll_keys)(void);              /* returns next scan code (with 0x80 = release) or -1 */
} PbfPlatform;

extern PbfPlatform pbf_plat;

int  pbf_load(const PbfProgram *prog, const uint8_t *prg, size_t prg_size, const char *modname);
void pbf_run(void);                       /* runs the game until it exits (or pbf_stop is called) */
void pbf_stop(void);
void pbf_end_program(void);
int pbf_in_options(void);   /* the intro is on its OPTIONS page */
const char *pbf_program_name(void);
int pbf_in_demo(void);   /* table in demo/attract mode (opening scroll) */                   /* end the current program only (table -> chooser) */
int  pbf_exit_code(void);
/* The module player reports a Bxx position jump (any thread that is the game thread); returns the order to use. */
int pbf_snd_jump_event(int param);
void pbf_set_config(int balls5, int angle, int scrolling, int music_off, int hires, int bw);

/* ---- VGA (planar) --------------------------------------------------------------------------------- */
void vga_reset(void);
uint8_t vga_mem_read(uint32_t linear);
void vga_mem_write(uint32_t linear, uint8_t v);
void vga_port_out(uint16_t port, uint8_t v);
uint8_t vga_port_in(uint16_t port);
int  vga_width(void);
int  vga_height(void);
void vga_render(uint8_t *fb, int stride, int y0, int y1);
void vga_latch_frame(void);
void vga_get_palette(uint8_t out[768]);   /* display rows [y0,y1) */
void vga_set_text_mode(int on);
void vga_set_mode12(void);
void vga_set_attr_palette(int i, uint8_t v);
int  vga_text_mode(void);
int  vga_is_x_mode(void);

/* ---- registration of native (host implemented) far/interrupt targets ------------------------------- */
void x_register_native(uint16_t seg, uint16_t off, x_fn fn);

#ifdef __cplusplus
}
#endif
#endif
