/* vga.c -- planar VGA (Mode X capable) emulation: memory latches/write modes, sequencer, graphics controller,
 * CRTC (start address, offset, line compare, panning), DAC.  Renders display rows on demand. */
#include "pbfrt.h"
#include <string.h>

static uint8_t plane[4][65536];
static uint8_t seq[8], gc[16], crtc[32], attr[32];
static uint8_t seq_idx, gc_idx, crtc_idx, attr_idx, attr_flip;
static uint8_t misc_out = 0x63;
static uint8_t dac[256][3];
static uint8_t dac_wr, dac_rd, dac_wstate, dac_rstate;
static uint8_t latch[4];
static int text_mode = 1;

void vga_set_text_mode(int on) { text_mode = on; }
int vga_text_mode(void) { return text_mode; }

void vga_reset(void)
{
	memset(plane, 0, sizeof plane);
	memset(seq, 0, sizeof seq); memset(gc, 0, sizeof gc); memset(crtc, 0, sizeof crtc); memset(attr, 0, sizeof attr);
	seq[2] = 0x0F; seq[4] = 0x0E;         /* map mask all, chain-4 (mode 13h default) */
	gc[8] = 0xFF;
	gc[5] = 0x40;
	crtc[1] = 0x4F; crtc[9] = 0x41; crtc[0x12] = 0xDF; crtc[0x13] = 0x28; crtc[7] = 0x1F; crtc[0x18] = 0xFF;
	attr[0x10] = 0x41;
	for (int i = 0; i < 256; i++) { dac[i][0] = dac[i][1] = dac[i][2] = (uint8_t)(i >> 2); }
	memset(latch, 0, 4);
}

/* ---- memory ---------------------------------------------------------------------------------------- */
static inline uint8_t rorb(uint8_t v, int n) { n &= 7; return (uint8_t)((v >> n) | (v << (8 - n))); }
static inline uint8_t alu(int f, uint8_t d, uint8_t l)
{
	switch (f) { case 1: return d & l; case 2: return d | l; case 3: return d ^ l; default: return d; }
}

uint8_t vga_mem_read(uint32_t linear)
{
	uint32_t off = (linear - 0xA0000u) & 0xFFFF;
	if (seq[4] & 8) {                                  /* chain-4 */
		return plane[off & 3][(off >> 2) & 0xFFFF];
	}
	for (int p = 0; p < 4; p++) latch[p] = plane[p][off];
	if (gc[5] & 8) {                                   /* read mode 1: colour compare */
		uint8_t res = 0xFF, cc = gc[2], dc = gc[7];
		for (int p = 0; p < 4; p++) {
			if (!(dc & (1 << p))) continue;
			uint8_t m = (cc & (1 << p)) ? 0xFF : 0x00;
			res &= (uint8_t)~(latch[p] ^ m);
		}
		return res;
	}
	return latch[gc[4] & 3];
}

void vga_mem_write(uint32_t linear, uint8_t v)
{
	uint32_t off = (linear - 0xA0000u) & 0xFFFF;
	if (seq[4] & 8) {                                  /* chain-4 */
		plane[off & 3][(off >> 2) & 0xFFFF] = v;
		return;
	}
	int wmode = gc[5] & 3, rot = gc[3] & 7, fn = (gc[3] >> 3) & 3;
	uint8_t bm = gc[8], sr = gc[0], esr = gc[1], mm = seq[2] & 0x0F;
	uint8_t out[4];
	for (int p = 0; p < 4; p++) {
		uint8_t l = plane[p][off], d, r;
		switch (wmode) {
		case 0:
			d = rorb(v, rot);
			if (esr & (1 << p)) d = (sr & (1 << p)) ? 0xFF : 0x00;
			r = (uint8_t)((alu(fn, d, l) & bm) | (l & ~bm));
			break;
		case 1:
			r = latch[p];
			break;
		case 2:
			d = (v & (1 << p)) ? 0xFF : 0x00;
			r = (uint8_t)((alu(fn, d, l) & bm) | (l & ~bm));
			break;
		default: {
			uint8_t m = (uint8_t)(rorb(v, rot) & bm);
			d = (sr & (1 << p)) ? 0xFF : 0x00;
			r = (uint8_t)((alu(fn, d, l) & m) | (l & ~m));
			break; }
		}
		out[p] = r;
	}
	for (int p = 0; p < 4; p++) if (mm & (1 << p)) plane[p][off] = out[p];
}

/* ---- ports ----------------------------------------------------------------------------------------- */
void vga_port_out(uint16_t port, uint8_t v)
{
	switch (port) {
	case 0x3C0:
		if (!attr_flip) attr_idx = v & 0x1F; else attr[attr_idx & 0x1F] = v;
		attr_flip ^= 1;
		break;
	case 0x3C2: misc_out = v; break;
	case 0x3C4: seq_idx = v & 7; break;
	case 0x3C5: seq[seq_idx] = v; break;
	case 0x3C7: dac_rd = v; dac_rstate = 0; break;
	case 0x3C8: dac_wr = v; dac_wstate = 0; break;
	case 0x3C9:
		dac[dac_wr][dac_wstate] = v & 0x3F;
		if (++dac_wstate == 3) { dac_wstate = 0; dac_wr++; }
		break;
	case 0x3CE: gc_idx = v & 15; break;
	case 0x3CF: gc[gc_idx] = v; break;
	case 0x3D4: case 0x3B4: crtc_idx = v & 31; break;
	case 0x3D5: case 0x3B5:
		if (crtc_idx <= 7 && (crtc[0x11] & 0x80) && crtc_idx != 7) break;   /* write protect */
		if (crtc_idx == 7 && (crtc[0x11] & 0x80)) { crtc[7] = (uint8_t)((crtc[7] & ~0x10) | (v & 0x10)); break; }
		crtc[crtc_idx] = v;
		break;
	default: break;
	}
}

uint8_t vga_port_in(uint16_t port)
{
	switch (port) {
	case 0x3C0: return attr_idx;
	case 0x3C1: return attr[attr_idx & 0x1F];
	case 0x3C4: return seq_idx;
	case 0x3C5: return seq[seq_idx];
	case 0x3C7: return 1;
	case 0x3C8: return dac_wr;
	case 0x3C9: { uint8_t r = dac[dac_rd][dac_rstate]; if (++dac_rstate == 3) { dac_rstate = 0; dac_rd++; } return r; }
	case 0x3CC: return misc_out;
	case 0x3CE: return gc_idx;
	case 0x3CF: return gc[gc_idx];
	case 0x3D4: return crtc_idx;
	case 0x3D5: return crtc[crtc_idx];
	case 0x3DA: attr_flip = 0; return 0;   /* the event loop overrides this with the retrace state */
	default: return 0xFF;
	}
}

void vga_set_mode12(void)
{
	seq[4] = 0x06; attr[0x10] = 0x01; for (int i = 0; i < 16; i++) attr[i] = (uint8_t)i;
	gc[5] = 0; gc[8] = 0xFF; gc[6] = 0x05; crtc[1] = 0x4F; crtc[9] = 0x40; crtc[0x12] = 0xDF; crtc[0x13] = 0x28; crtc[7] = 0x3E;
}
void vga_set_attr_palette(int i, uint8_t v) { attr[i & 31] = v; }
int vga_is_x_mode(void) { return !(seq[4] & 8); }

/* ---- geometry / rendering ------------------------------------------------------------------------ */
static int total_lines(void) { return crtc[0x12] | ((crtc[7] & 2) << 7) | ((crtc[7] & 0x40) << 3); }
static int lines_per_row(void) { return ((crtc[9] & 0x1F) + 1) * ((crtc[9] & 0x80) ? 2 : 1); }
static int is16(void) { return !(attr[0x10] & 0x40); }   /* 16-colour planar (the intro/chooser) */
int vga_width(void) { return (crtc[1] + 1) * (is16() ? 8 : 4); }
int vga_height(void) { int h = (total_lines() + 1) / lines_per_row(); return h < 1 ? 1 : h; }

static uint32_t lat_start; static int lat_pan = 0, lat_valid;
/* the display start address and pel panning are latched at vertical retrace; writes during the frame apply to the next one */
void vga_latch_frame(void) { lat_start = ((uint32_t)crtc[0xC] << 8) | crtc[0xD]; lat_pan = (attr[0x13] & 7) >> 1; lat_valid = 1; }

void vga_get_palette(uint8_t out[768])
{
	for (int i = 0; i < 256; i++) { out[i * 3] = dac[i][0]; out[i * 3 + 1] = dac[i][1]; out[i * 3 + 2] = dac[i][2]; }
}

/* renders display rows [y0,y1) as palette indices */
void vga_render(uint8_t *fb, int stride, int y0, int y1)
{
	int w = vga_width(), h = vga_height();
	if (y1 > h) y1 = h;
	int lpr = lines_per_row();
	uint32_t start = lat_valid ? lat_start : (((uint32_t)crtc[0xC] << 8) | crtc[0xD]);
	int offs = crtc[0x13] * 2;                 /* address units (of 4 pixels) per row */
	int lc = crtc[0x18] | ((crtc[7] & 0x10) << 4) | ((crtc[9] & 0x40) << 3);
	int pan = lat_valid ? lat_pan : ((attr[0x13] & 7) >> 1);
	int panlock = (attr[0x10] & 0x20) != 0;
	int splitrow = -1;
	for (int y = y0; y < y1; y++) {
		int line = y * lpr;
		uint32_t base;
		int p = pan;
		if (line > lc) {
			if (splitrow < 0) splitrow = y;
			base = (uint32_t)((y - splitrow) * offs);
			if (panlock) p = 0;
		} else {
			base = start + (uint32_t)y * offs;
		}
		uint8_t *dst = fb + (size_t)y * stride;
		if (is16()) {
			int pan16 = lat_valid ? (attr[0x13] & 7) : (attr[0x13] & 7);
			uint8_t hi = (attr[0x10] & 0x80) ? (uint8_t)((attr[0x14] & 0x0F) << 4) : (uint8_t)((attr[0x14] & 0x0C) << 4);
			uint8_t mask = (attr[0x10] & 0x80) ? 0x0F : 0x3F;
			for (int x = 0; x < w; x++) {
				int sx = x + (panlock && line > lc ? 0 : pan16);
				uint32_t a = (base + (uint32_t)(sx >> 3)) & 0xFFFF;
				int bit = 7 - (sx & 7);
				int c = ((plane[0][a] >> bit) & 1) | (((plane[1][a] >> bit) & 1) << 1) | (((plane[2][a] >> bit) & 1) << 2) | (((plane[3][a] >> bit) & 1) << 3);
				dst[x] = (uint8_t)((attr[c] & mask) | hi);
			}
			continue;
		}
		for (int x = 0; x < w; x++) {
			int sx = x + p;
			dst[x] = plane[sx & 3][(base + (uint32_t)(sx >> 2)) & 0xFFFF];
		}
	}
}
int vga_dbg_count(int p, int off, int n) { int c = 0; for (int i = 0; i < n; i++) c += plane[p][(off + i) & 0xFFFF] != 0; return c; }
