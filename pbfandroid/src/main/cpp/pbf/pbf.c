/* pbf.c -- loader, DOS/BIOS/driver emulation and the hardware event loop for the translated game. */
#include "pbfrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

PbfPlatform pbf_plat;
static const PbfProgram *P;

/* ---- memory map ------------------------------------------------------------------------------------ */
#define PSP_SEG   0x0500u
#define LOAD_SEG  0x0510u
#define ARENA_LO  0x8800u
#define ARENA_HI  0xA000u

static uint16_t load_seg;
static uint16_t *segvar(const char *name);
static jmp_buf exit_jmp;
static int exit_code, running, stop_req, end_req;
static uint16_t arena_next;

static void LOG(const char *fmt, ...)
{
	if (!pbf_plat.log) return;
	char buf[512];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
	pbf_plat.log("%s", buf);
}

/* ---- dispatch ------------------------------------------------------------------------------------- */
typedef struct { uint16_t seg, off; x_fn fn; } NativeEnt;
static NativeEnt natives[64];   /* host callbacks registered by the platform layer */
static int n_natives;
void x_register_native(uint16_t seg, uint16_t off, x_fn fn)
{
	if (n_natives >= 64) return;
	natives[n_natives].seg = seg; natives[n_natives].off = off; natives[n_natives].fn = fn; n_natives++;
}

static void nat_66(void);
static void nat_65(void);
static void nat_iret(void);
x_fn x_dispatch(uint16_t seg, uint16_t off)
{
	if (seg == 0xF000) return off == 0x66 ? nat_66 : off == 0x65 ? nat_65 : nat_iret;
	for (int i = 0; i < n_natives; i++) if (natives[i].seg == seg && natives[i].off == off) return natives[i].fn;
	for (const PbfDispSeg *s = P->disp; s->name; s++) {
		if (*s->segvar != seg) continue;
		int lo = 0, hi = s->n - 1;
		while (lo <= hi) {
			int m = (lo + hi) >> 1;
			if (s->tab[m].off == off) return s->tab[m].fn;
			if (s->tab[m].off < off) lo = m + 1; else hi = m - 1;
		}
	}
	x_bad_target(seg, off);
	return NULL;
}

void x_bad_target(uint16_t seg, uint16_t off)
{
	fprintf(stderr, "pbf: bad indirect target %04X:%04X (CS=%04X)\n", seg, off, CS);
	fflush(stderr);
	exit_code = 99;
	longjmp(exit_jmp, 1);
}
void x_bad_return(uint16_t e, uint16_t g) { fprintf(stderr, "pbf: bad return expected %04X got %04X\n", e, g); }
uint16_t x_last_ret;
void x_fell_off(const char *fn) { fprintf(stderr, "pbf: fell off the end of %s\n", fn); exit_code = 98; longjmp(exit_jmp, 1); }
void x_hlt(void) { fprintf(stderr, "pbf: HLT\n"); exit_code = 97; longjmp(exit_jmp, 1); }

/* ---- register save/restore for interrupts ---------------------------------------------------------- */
typedef struct {
	X86Reg ax, bx, cx, dx; uint16_t si, di, bp, sp, cs, ds, es, ss; uint16_t fl;
} Ctx;
static void ctx_save(Ctx *c)
{
	c->ax = x_ax; c->bx = x_bx; c->cx = x_cx; c->dx = x_dx; c->si = x_si; c->di = x_di; c->bp = x_bp; c->sp = x_sp;
	c->cs = x_cs; c->ds = x_ds; c->es = x_es; c->ss = x_ss; c->fl = x_getflags();
}
static void ctx_restore(const Ctx *c)
{
	x_ax = c->ax; x_bx = c->bx; x_cx = c->cx; x_dx = c->dx; x_si = c->si; x_di = c->di; x_bp = c->bp; x_sp = c->sp;
	x_cs = c->cs; x_ds = c->ds; x_es = c->es; x_ss = c->ss; x_setflags(c->fl);
}

/* call a game interrupt handler installed at seg:off as hardware would (flags, cs, ip pushed, IF cleared) */
static void call_isr(uint16_t seg, uint16_t off)
{
	Ctx c; ctx_save(&c);
	PUSHF(); PUSH16(CS); PUSH16(0xFFF0);
	CS = seg; x_if = 0;
	x_dispatch(seg, off)();
	ctx_restore(&c);
}
/* far call (RETF-terminated) into the game: used for the vblank / raster / jingle callbacks */
static uint16_t call_far_ax(uint16_t seg, uint16_t off, uint16_t ax, uint16_t bx)
{
	Ctx c; ctx_save(&c);
	AX = ax; BX = bx;
	PUSH16(CS); PUSH16(0xFFF1);
	CS = seg;
	x_dispatch(seg, off)();
	uint16_t r = AX;
	ctx_restore(&c);
	return r;
}

/* ---- virtual DOS files --------------------------------------------------------------------------- */
typedef struct { int used; char name[64]; uint8_t *data; size_t size, pos; int writable; } VFile;
static VFile files[16];

static void upname(char *dst, const char *src, size_t n)
{
	size_t i = 0;
	for (; src[i] && i + 1 < n; i++) dst[i] = (char)((src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i]);
	dst[i] = 0;
}
static int cfg_config[6];
static char modname[64] = "TABLE1.MOD";

static int vfs_open(const char *nm, int create)
{
	int h = -1;
	for (int i = 5; i < 16; i++) if (!files[i].used) { h = i; break; }
	if (h < 0) return -1;
	VFile *f = &files[h];
	memset(f, 0, sizeof *f);
	upname(f->name, nm, sizeof f->name);
	if (create) { f->used = 1; f->writable = 1; f->data = malloc(1); f->size = 0; return h; }
	if (strstr(f->name, "SOUND") && strstr(f->name, ".CFG")) {
		static const char cfgtxt[] = "SB16.SDR\0\0\0\0\0";
		f->data = malloc(sizeof cfgtxt); memcpy(f->data, cfgtxt, sizeof cfgtxt); f->size = sizeof cfgtxt; f->used = 1;
		return h;
	}
	if (pbf_plat.read_file && pbf_plat.read_file(f->name, &f->data, &f->size) == 0) { f->used = 1; return h; }
	return -1;
}
static void vfs_close(int h)
{
	if (h < 5 || h >= 16 || !files[h].used) return;
	VFile *f = &files[h];
	if (f->writable && pbf_plat.write_file) pbf_plat.write_file(f->name, f->data, f->size);
	free(f->data); memset(f, 0, sizeof *f);
}

static void dos_string(char *dst, size_t n, uint16_t seg, uint16_t off)
{
	size_t i = 0;
	for (; i + 1 < n; i++) { char c = (char)RD8(seg, (uint16_t)(off + i)); if (!c) break; dst[i] = c; }
	dst[i] = 0;
}

static uint64_t next_frame_us;
static uint64_t now_us(void);
/* ---- sound driver state (INT 66h) ------------------------------------------------------------------ */
static uint16_t vb_seg, vb_off, ras_seg, ras_off, ras_line, jing_seg, jing_off;
static int song_pos, module_loaded;

static void int66(void)
{
	if (AH == 21) AL = 21;   /* INTRO asks the driver for the card type with `mov ah,21` */
	switch (AL) {
	case 21: AL = 0; break;
	case 24: break;   /* screen blank (music side): nothing to do */   /* bit 0 set = Gravis/no sound: we look like a Sound Blaster */
	case 0: if (pbf_plat.snd_stop) pbf_plat.snd_stop(); break;
	case 4:   /* play module: mode 2 = start from the top, mode 3 = (re)start output after a pause */
		LOG("snd: play module mode=%d rate=%d", BX, CX);
		if (BX == 3 && pbf_plat.snd_pause) pbf_plat.snd_pause(0);
		else if (pbf_plat.snd_play) pbf_plat.snd_play(BX);
		AX = 0; break;
	case 15: if (pbf_plat.snd_pause) pbf_plat.snd_pause(1); break;
	case 6: if (pbf_plat.snd_volume) pbf_plat.snd_volume(CX); break;
	case 8: {   /* "create music": the driver mixes here; for us it is the idle point of the main loop */
		uint64_t t = now_us();
		if (pbf_plat.sleep_us && next_frame_us > t + 1500) pbf_plat.sleep_us((uint32_t)(next_frame_us - t - 500));
		break; }
	case 9: AL = (uint8_t)(pbf_plat.snd_position ? pbf_plat.snd_position() : song_pos); break;
	case 11: vb_seg = ES; vb_off = DX; LOG("snd: vblank handler %04X:%04X", vb_seg, vb_off); break;
	case 12: ras_seg = ES; ras_off = DX; ras_line = CX; LOG("snd: raster handler %04X:%04X line %d", ras_seg, ras_off, ras_line); break;
	case 16: {   /* force position: BX = order; returns the order that was playing */
		uint8_t old = (uint8_t)(pbf_plat.snd_position ? pbf_plat.snd_position() : song_pos);
		LOG("snd: force position %d (was %d)", BL, old);
		song_pos = BL;
		if (pbf_plat.snd_jump) pbf_plat.snd_jump(BL);
		AL = old; break; }
	case 17: {   /* sound effect: CL sample, BL note, BH volume (0 = default), DL channel+1 */
		int ch = (int)DL - 1; if (ch < 0) ch = 0;
		LOG("snd: effect sample %d note %d vol %d chan %d", CL, BL, BH, ch);
		if (pbf_plat.snd_effect) pbf_plat.snd_effect(CL, BL, BH, ch);
		break; }
	case 18: {
		char nm[64]; dos_string(nm, sizeof nm, DS, DX);
		LOG("snd: load module '%s'", nm);
		uint8_t *d = NULL; size_t sz = 0;
		char up[64]; upname(up, nm, sizeof up);
		if (pbf_plat.read_file && pbf_plat.read_file(up, &d, &sz) == 0) {
			if (pbf_plat.snd_load_module) pbf_plat.snd_load_module(up, d, sz);
			free(d); module_loaded = 1; AX = 0;
		} else { LOG("snd: module not found"); AX = 3; }
		break; }
	case 19: jing_seg = ES; jing_off = DX; LOG("snd: jingle handler %04X:%04X", jing_seg, jing_off); break;
	default: LOG("snd: unknown INT 66h AL=%d AX=%04X", AL, AX); break;
	}
}

/* Bxx (position jump) inside the module: the real driver offers the target to the game's jingle handler
 * (AL = target order) and jumps to whatever comes back.  Must be called on the game thread. */
int pbf_snd_jump_event(int param)
{
	if (!(jing_seg | jing_off)) return param;
	int r = call_far_ax(jing_seg, jing_off, (uint16_t)(param & 0xFF), 0) & 0xFF;
	LOG("snd: Bxx jump %d -> handler -> %d", param, r);
	return r;
}

/* ---- INT 21h ---------------------------------------------------------------------------------------- */
static void set_cf(int v) { x_cf = (uint8_t)v; }
static void int21(void)
{
	switch (AH) {
	case 0x02: putc(DL, stderr); break;
	case 0x09: { uint16_t o = DX; for (int i = 0; i < 4096; i++) { char c = (char)RD8(DS, o++); if (c == '$') break; putc(c, stderr); } break; }
	case 0x25: WR16(0, (uint16_t)(AL * 4), DX); WR16(0, (uint16_t)(AL * 4 + 2), DS); break;
	case 0x35: BX = RD16(0, (uint16_t)(AL * 4)); ES = RD16(0, (uint16_t)(AL * 4 + 2)); break;
	case 0x2C: CX = 12 << 8; DX = 0; break;
	case 0x30: AX = 0x0005; BX = 0; CX = 0; break;
	case 0x1A: case 0x2F: break;
	case 0x3C: case 0x3D: {
		char nm[64]; dos_string(nm, sizeof nm, DS, DX);
		int h = vfs_open(nm, AH == 0x3C);
		if (h < 0) { AX = 2; set_cf(1); LOG("dos: open '%s' failed", nm); }
		else { AX = (uint16_t)h; set_cf(0); LOG("dos: open '%s' -> %d", nm, h); }
		break; }
	case 0x3E: vfs_close(BX); set_cf(0); break;
	case 0x3F: {
		if (BX >= 16 || !files[BX].used) { AX = 6; set_cf(1); break; }
		VFile *f = &files[BX];
		size_t n = CX; if (f->pos + n > f->size) n = f->size - f->pos;
		for (size_t i = 0; i < n; i++) WR8(DS, (uint16_t)(DX + i), f->data[f->pos + i]);
		f->pos += n; AX = (uint16_t)n; set_cf(0); break; }
	case 0x40: {
		if (BX == 1 || BX == 2) { for (int i = 0; i < CX; i++) putc(RD8(DS, (uint16_t)(DX + i)), stderr); AX = CX; set_cf(0); break; }
		if (BX >= 16 || !files[BX].used) { AX = 6; set_cf(1); break; }
		VFile *f = &files[BX];
		size_t end = f->pos + CX;
		if (end > f->size) { f->data = realloc(f->data, end); f->size = end; }
		for (int i = 0; i < CX; i++) f->data[f->pos + i] = RD8(DS, (uint16_t)(DX + i));
		f->pos = end; AX = CX; set_cf(0); break; }
	case 0x41: set_cf(0); break;
	case 0x42: {
		if (BX >= 16 || !files[BX].used) { AX = 6; set_cf(1); break; }
		VFile *f = &files[BX];
		int32_t off = (int32_t)(((uint32_t)CX << 16) | DX);
		size_t base = AL == 0 ? 0 : AL == 1 ? f->pos : f->size;
		f->pos = (size_t)((int64_t)base + off);
		DX = (uint16_t)(f->pos >> 16); AX = (uint16_t)f->pos; set_cf(0); break; }
	case 0x48: {
		uint16_t need = BX;
		if (need == 0xFFFF) { BX = 0xF000; AX = 8; set_cf(1); break; }   /* "how much is free": plenty */
		if ((uint32_t)arena_next + need > ARENA_HI) { BX = (uint16_t)(ARENA_HI - arena_next); AX = 8; set_cf(1); break; }
		AX = arena_next; arena_next = (uint16_t)(arena_next + need); set_cf(0); break; }
	case 0x49: set_cf(0); break;
	case 0x4A: set_cf(0); break;
	case 0x4B:
		LOG("dos: exec (sound driver) -> INT 66h installed");
		WR16(0, 0x66 * 4, 0x66); WR16(0, 0x66 * 4 + 2, 0xF000);
		set_cf(0); AX = 0; break;
	case 0x4D: AX = 0; set_cf(0); break;   /* return code of the child (driver / program) */
	case 0x4C: exit_code = AL; LOG("dos: exit %d", AL); longjmp(exit_jmp, 1);
	default:
		LOG("dos: unhandled INT 21h AX=%04X BX=%04X CX=%04X DX=%04X", AX, BX, CX, DX);
		set_cf(1); break;
	}
}

/* ---- INT 10h / 33h / 65h --------------------------------------------------------------------------- */
static void int10(void)
{
	if (AH == 0x00) {
		int m = AL & 0x7F;
		LOG("bios: set video mode %02X", m);
		vga_reset();
		vga_set_text_mode(m == 3);
		if (m == 0x12) vga_set_mode12();
		return;
	}
	if (AX == 0x1002) {   /* set all attribute palette registers from ES:DX (16 + overscan) */
		for (int i = 0; i < 17; i++) vga_set_attr_palette(i == 16 ? 0x11 : i, RD8(ES, (uint16_t)(DX + i)));
		return;
	}
	if (AX == 0x1012) {
		for (int i = 0; i < CX; i++) {
			vga_port_out(0x3C8, (uint8_t)(BX + i));
			for (int k = 0; k < 3; k++) vga_port_out(0x3C9, RD8(ES, (uint16_t)(DX + i * 3 + k)));
		}
		return;
	}
	LOG("bios: unhandled INT 10h AX=%04X", AX);
}
static void int33(void)
{
	if (AX == 0) { AX = 0; BX = 0; return; }   /* no mouse */
}
/* launcher (START.EXE) state shared between the intro/chooser and the tables */
static int l_first_time = 1, l_banner = 1;
static uint8_t l_scan;   /* START.EXE's INT 9 handler: last make code, read (and cleared) through INT 65h */
static void int65(void)
{
	if (AX == 0x0000) { AH = (uint8_t)l_first_time; BL = (uint8_t)l_banner; return; }
	if (AX == 0xFFFF) { l_banner = BL; LOG("launcher: table %d chosen", l_banner); return; }
	if (AX == 0x0100) {                       /* local toggles (ES:BX) -> resident */
		for (int i = 0; i < 6; i++) cfg_config[i] = RD8(ES, (uint16_t)(BX + i));
		return;
	}
	if (AX == 0x0200) {                       /* resident -> local toggles (ES:BX) */
		for (int i = 0; i < 6; i++) WR8(ES, (uint16_t)(BX + i), (uint8_t)cfg_config[i]);
		return;
	}
	AH = 0; AL = l_scan; l_scan = 0;         /* everything else: report the last scan code (AX=12h "text input" included) */
}
void pbf_config_get(int cfg[6]) { for (int i = 0; i < 6; i++) cfg[i] = cfg_config[i]; }
void pbf_config_set(const int cfg[6]) { for (int i = 0; i < 6; i++) cfg_config[i] = cfg[i]; }
void pbf_set_config(int balls5, int angle, int scrolling, int music_off, int hires, int bw)
{
	cfg_config[0] = balls5; cfg_config[1] = angle; cfg_config[2] = scrolling; cfg_config[3] = music_off;
	cfg_config[4] = hires; cfg_config[5] = bw;
}

static void native_int(uint8_t n)
{
	switch (n) {
	case 0x10: int10(); break;
	case 0x21: int21(); break;
	case 0x33: int33(); break;
	case 0x65: int65(); break;
	case 0x66: int66(); break;
	case 0x09: case 0x08: case 0x1C: break;
	default: LOG("bios: unhandled INT %02Xh", n); break;
	}
}

void x_int(uint8_t n)
{
	uint16_t seg = RD16(0, (uint16_t)(n * 4 + 2)), off = RD16(0, (uint16_t)(n * 4));
	if (seg == 0xF000 || (seg == 0 && off == 0)) { native_int(n); return; }
	PUSHF(); PUSH16(CS); PUSH16(0xFFF2);
	uint16_t cs = CS;
	CS = seg; x_if = 0;
	x_dispatch(seg, off)();
	CS = cs;
}

/* native targets reached through the vector table by `pushf / call far [n*4]` */
static void nat_66(void) { int66(); RETURN_IRET(); }
static void nat_65(void) { int65(); RETURN_IRET(); }
static void nat_iret(void) { RETURN_IRET(); }

/* ---- ports -------------------------------------------------------------------------------------- */
static uint64_t frame_epoch_us;
static int in_vretrace;
static uint8_t kbd_latch, cmos_sel;

uint8_t x_in8(uint16_t p)
{
	if (p == 0x3DA) { uint8_t r = vga_port_in(0x3DA); return (uint8_t)(r | (in_vretrace ? 9 : 0)); }
	if (p >= 0x3C0 && p <= 0x3DF) return vga_port_in(p);
	if (p == 0x60) return kbd_latch;
	if (p == 0x61) return 0;
	/* CMOS: the intro's manual-word protection reads the checksum bytes 2Eh/2Fh; answering 90h/00h makes it pass */
	if (p == 0x71 && (cmos_sel == 0x2E || cmos_sel == 0x2F) && P && P->chk_off) {
		uint16_t chk = RD16(*segvar("DATA"), P->chk_off);
		uint16_t v = (uint16_t)(((chk >> 1) | (chk << 15)) ^ 0x90);
		return cmos_sel == 0x2E ? (uint8_t)v : (uint8_t)(v >> 8);
	}
	if (p == 0x71) return 0;
	return 0xFF;
}
uint16_t x_in16(uint16_t p) { return (uint16_t)(x_in8(p) | (x_in8((uint16_t)(p + 1)) << 8)); }
void x_out8(uint16_t p, uint8_t v)
{
	if (p >= 0x3C0 && p <= 0x3DF) { vga_port_out(p, v); return; }
	if (p == 0x70) { cmos_sel = v; return; }
	/* PIC, PIT, speaker, keyboard controller, floppy: nothing to do */
}
void x_out16(uint16_t p, uint16_t v)
{
	if (p >= 0x3C0 && p <= 0x3DF) { vga_port_out(p, (uint8_t)v); vga_port_out((uint16_t)(p + 1), (uint8_t)(v >> 8)); return; }
}

/* ---- the event loop: vblank / raster / keyboard at the checkpoints --------------------------------- */
#define FRAME_US 16683u
static uint8_t framebuf[400 * 800];
static int in_event;
static unsigned long frames;

static uint64_t now_us(void) { return pbf_plat.now_us ? pbf_plat.now_us() : 0; }


static void present(int w, int h)
{
	uint8_t pal[768];
	vga_get_palette(pal);
	if (pbf_plat.present) pbf_plat.present(framebuf, pal, w, h);
}

static void deliver_frame(void)
{
	in_event = 1;
	if (pbf_plat.snd_frame) pbf_plat.snd_frame();
	in_vretrace = 1;
	if (vb_seg | vb_off) { if (x_if || 1) call_far_ax(vb_seg, vb_off, 0, 0); }
	in_vretrace = 0;
	vga_latch_frame();
	int w = vga_width(), h = vga_height();
	if (w > 800) w = 800;
	if (h > 400) h = 400;
	int split = h;
	if ((ras_seg | ras_off) && ras_line / 2 < h) split = ras_line / 2;
	vga_render(framebuf, w, 0, split);
	if (split < h) { call_far_ax(ras_seg, ras_off, 0, 0); vga_render(framebuf, w, split, h); }
	present(w, h);
	frames++;
	in_event = 0;
}

void x_poll_slow(void)
{
	x_poll_budget = 4000;
	if (stop_req || end_req) { exit_code = 0; end_req = 0; in_event = 0; longjmp(exit_jmp, 1); }
	if (in_event) return;
	uint64_t t = now_us();
	if (t >= next_frame_us) {
		next_frame_us += FRAME_US;
		if (next_frame_us < t) next_frame_us = t + FRAME_US;
		deliver_frame();
	}
	if (pbf_plat.poll_keys) {
		int sc;
		while (x_if && (sc = pbf_plat.poll_keys()) >= 0) {
			kbd_latch = (uint8_t)sc;
			uint16_t seg = RD16(0, 9 * 4 + 2), off = RD16(0, 9 * 4);
			if (seg != 0xF000 && (seg | off)) { in_event = 1; call_isr(seg, off); in_event = 0; }
			else if (!(sc & 0x80)) l_scan = (uint8_t)sc;
		}
	}
}

void pbf_stop(void) { stop_req = 1; }
/* 1 while the intro is showing its OPTIONS page (INFO_TEXT points at OPTIONS_INFO) rather than the table chooser */
int pbf_in_options(void)
{
	if (!P || !running || !P->name || strcmp(P->name, "i1_")) return 0;
	return RD16(*segvar("CODE"), 0x16F2) == 0x1677;
}

const char *pbf_program_name(void) { return (P && P->name) ? P->name : ""; }

/* 1 while a table is in its attract/demo mode (the opening scroll, before START begins a game) */
int pbf_in_demo(void)
{
	static const struct { const char *name; uint16_t off; } d[] = { {"t1_", 0x3713}, {"t2_", 0x37AA}, {"t3_", 0x2FCF}, {"t4_", 0x3CE3} };
	if (!P || !running || !P->name) return 0;
	for (unsigned i = 0; i < 4; i++)
		if (!strcmp(P->name, d[i].name)) return x_mem[((size_t)*segvar("DATA") << 4) + d[i].off] != 0;
	return 0;
}

void pbf_end_program(void) { end_req = 1; }   /* leave the running program (back to the chooser), keep the session */
int pbf_exit_code(void) { return exit_code; }

/* ---- loader ---------------------------------------------------------------------------------------- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint16_t *segvar(const char *name);
static uint16_t *segvar(const char *name)
{
	for (const PbfSegVar *s = P->segvars; s->name; s++) if (!strcmp(s->name, name)) return s->var;
	return NULL;
}

int pbf_load(const PbfProgram *prog, const uint8_t *prg, size_t n, const char *mod)
{
	P = prog;
	if (mod) snprintf(modname, sizeof modname, "%s", mod);
	if (!x_mem) x_mem = calloc(1, X_MEM_SIZE);
	memset(x_mem, 0, X_MEM_SIZE);
	x_vga_read8 = vga_mem_read; x_vga_write8 = vga_mem_write;
	vga_reset();
	if (n < 28 || prg[0] != 'M' || prg[1] != 'Z') return -1;
	unsigned lastpg = rd16(prg + 2), pages = rd16(prg + 4), nrel = rd16(prg + 6), hdr = rd16(prg + 8);
	unsigned ss = rd16(prg + 14), sp = rd16(prg + 16), relo = rd16(prg + 24);
	size_t total = (size_t)pages * 512 - (lastpg ? 512 - lastpg : 0);
	size_t img = total - (size_t)hdr * 16;
	load_seg = LOAD_SEG;
	memcpy(x_mem + ((size_t)load_seg << 4), prg + hdr * 16, img);
	for (unsigned i = 0; i < nrel; i++) {
		unsigned off = rd16(prg + relo + i * 4), seg = rd16(prg + relo + i * 4 + 2);
		uint8_t *w = x_mem + (((size_t)load_seg + seg) << 4) + off;
		uint16_t v = (uint16_t)(rd16(w) + load_seg);
		w[0] = (uint8_t)v; w[1] = (uint8_t)(v >> 8);
	}
	/* segment variables */
	for (const PbfSegVar *s = P->segvars; s->name; s++) {
		if (s->prg == 0xFFFF) continue;
		*s->var = (uint16_t)(s->prg + load_seg);
	}
	uint16_t *stk = segvar("STACKEN");
	if (stk) *stk = (uint16_t)(ss + load_seg);
	/* source-derived initial data overlays the shipped data segments */
	static uint8_t *saved;
	free(saved);
	saved = malloc(0x10000);
	memcpy(saved, x_mem + ((size_t)*segvar("DATA") << 4), 0x10000);
	if (P->data_seg) *segvar("DATA") = P->data_seg;
	for (const PbfInitRange *r = P->init; r->seg; r++)
		memcpy(x_mem + ((size_t)*segvar(r->seg) << 4) + r->off, r->data, r->len);
	for (const PbfKeep *k = P->keep; k->seg; k++) {
		if (strcmp(k->seg, "DATA")) continue;
		memcpy(x_mem + ((size_t)*segvar("DATA") << 4) + k->off, saved + k->off, k->size);
	}
	for (const PbfExtCopy *e = P->extcopy; e && e->seg; e++)
		memcpy(x_mem + ((size_t)*segvar(e->seg) << 4) + e->to, saved + e->from, e->len);
	for (const PbfSegFix *f = P->segfix; f->seg; f++) {
		uint16_t v = (uint16_t)(*segvar(f->target) + f->add);
		uint8_t *w = x_mem + ((size_t)*segvar(f->seg) << 4) + f->off;
		w[0] = (uint8_t)v; w[1] = (uint8_t)(v >> 8);
	}
	/* PSP, IVT, BIOS data */
	x_mem[((size_t)PSP_SEG << 4) + 0] = 0xCD; x_mem[((size_t)PSP_SEG << 4) + 1] = 0x20;
	WR16(PSP_SEG, 2, ARENA_HI);
	for (int i = 0; i < 256; i++) { WR16(0, (uint16_t)(i * 4), (uint16_t)i); WR16(0, (uint16_t)(i * 4 + 2), 0xF000); }
	WR16(0, 0x66 * 4 + 2, 0); WR16(0, 0x66 * 4, 0);     /* the driver installs itself via exec */
	WR16(0x40, 0x63, 0x3D4);
	n_natives = 0;
	arena_next = ARENA_LO + 0x100;
	memset(files, 0, sizeof files);
	vb_seg = vb_off = ras_seg = ras_off = jing_seg = jing_off = 0;
	frames = 0; stop_req = 0; end_req = 0; in_event = 0; exit_code = 0;
	/* entry state as DOS would leave it */
	DS = ES = PSP_SEG;
	CS = *segvar("CODE");
	SS = (uint16_t)(ss + load_seg); SP = (uint16_t)sp;
	x_if = 1; x_df = 0;
	return 0;
}

void pbf_run(void)
{
	running = 1;
	next_frame_us = now_us() + FRAME_US;
	x_poll_budget = 1;
	if (setjmp(exit_jmp) == 0) {
		P->entry();
	}
	running = 0;
}

/* ---- launcher ---------------------------------------------------------------------------------------- */
static int g_in_intro;
int pbf_running_intro(void) { return g_in_intro; }
static const PbfLaunchEntry *g_intro_entry;
static int run_entry(const PbfLaunchEntry *e)
{
	uint8_t *d = NULL; size_t sz = 0;
	if (!e || !e->prog || !e->prg) return -1;
	if (!pbf_plat.read_file || pbf_plat.read_file(e->prg, &d, &sz) != 0) { LOG("launcher: cannot read %s", e->prg); return -1; }
	if (pbf_plat.snd_stop) pbf_plat.snd_stop();
	int r = pbf_load(e->prog, d, sz, NULL);
	free(d);
	if (r != 0) { LOG("launcher: %s is not a valid program", e->prg); return -1; }
	g_in_intro = (e == g_intro_entry);
	LOG("launcher: run %s", e->prg);
	pbf_run();
	g_in_intro = 0;
	if (pbf_plat.snd_stop) pbf_plat.snd_stop();
	return 0;
}

void pbf_session(const PbfLaunchEntry *intro, const PbfLaunchEntry tables[4], int start_table)
{
	l_first_time = 1; l_banner = start_table > 0 ? start_table : 1;
	g_intro_entry = intro;
	int direct = start_table > 0;
	for (;;) {
		if (!direct) {
			if (run_entry(intro) != 0) {
				/* no intro available: fall back to the table that was last chosen */
				if (!(l_banner >= 1 && l_banner <= 4 && tables[l_banner - 1].prog)) return;
				direct = 1;
			} else {
				l_first_time = 0;
				if (stop_req) return;
				if (exit_code != 0 && exit_code != 1) { LOG("launcher: intro failed (%d)", exit_code); return; }
			}
		}
		direct = 0;
		if (l_banner == 0 || l_banner > 4) return;             /* Esc: quit */
		if (run_entry(&tables[l_banner - 1]) != 0) {
			LOG("launcher: table %d is not available", l_banner);
			if (!intro || !intro->prog) return;
			l_banner = 1;
			continue;
		}
		if (stop_req) return;
		l_first_time = 0;
		if (exit_code >= 98) return;                            /* crash */
	}
}
