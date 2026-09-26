/* pbf_main.c -- Pinball Fantasies for descore: the game-side glue.
 *
 * The game itself is the original x86 assembly translated to C (t1_*.c, generated) running on a small
 * DOS/VGA/sound-driver runtime (pbf.c, vga.c, x86rt.c).  This file connects that runtime to the shared
 * descore shell: files come from the player's picked folder, frames go to descore's indexed-color present,
 * sound goes through the descore ProTracker replayer (pbf_audio.c), and keys and touches become the PC scan
 * codes the game's own keyboard interrupt expects.  Nothing here touches the game's rules or physics. */

#include "../descore/descore.h"
#include "../descore/touch/descore_touch.h"
#include "pbfrt.h"
#include "pbf_audio.h"
#include "pbf_font.h"

#include <android/log.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#define LOG_TAG "pbf"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern const PbfProgram t1_program, t2_program, t3_program, t4_program, i1_program;

static char g_dir[900];

/* PC scan codes the game reads (KEYINT / MAIN in FANTASIE.ASM) */
enum {
	SC_ESC = 0x01, SC_ENTER = 0x1C, SC_LSHIFT = 0x2A, SC_RSHIFT = 0x36, SC_CTRL = 0x1D, SC_ALT = 0x38,
	SC_SPACE = 0x39, SC_DOWN = 0x50, SC_UP = 0x48, SC_F5 = 0x3F, SC_P = 0x19, SC_BACKSPACE = 0x0E, SC_F1 = 0x3B
};

/* ---- scan-code input queue (UI thread -> game thread) --------------------------------------------- */
#define KQ 128
static pthread_mutex_t g_kq_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char g_kq[KQ];
static unsigned g_kq_r, g_kq_w;
static volatile int g_quit;

static void key_push(unsigned char sc, int down)
{
	pthread_mutex_lock(&g_kq_lock);
	/* the plunger is the (extended) down arrow: the game's keyboard interrupt wants the E0 prefix first */
	int ext = (sc == SC_DOWN || sc == SC_UP);
	int need = ext ? 2 : 1;
	if (KQ - (g_kq_w - g_kq_r) >= (unsigned)need) {
		if (ext) g_kq[g_kq_w++ % KQ] = 0xE0;
		g_kq[g_kq_w++ % KQ] = (unsigned char)(down ? sc : (sc | 0x80));
	}
	pthread_mutex_unlock(&g_kq_lock);
}

static int poll_keys(void)
{
	int r = -1;
	pthread_mutex_lock(&g_kq_lock);
	if (g_kq_r != g_kq_w) r = g_kq[g_kq_r++ % KQ];
	pthread_mutex_unlock(&g_kq_lock);
	return r;
}

/* ---- pop-up menu state (shared between the UI thread and the game thread) ------------------------- */
enum { ME_UP = 1, ME_DOWN, ME_LEFT, ME_RIGHT, ME_SELECT, ME_BACK, ME_CLOSE, ME_TAP0 = 16 };
static volatile int g_menu_req, g_menu_open;
static int g_mq[32];
static unsigned g_mq_r, g_mq_w;
static int g_display = 0;    /* 0 = 4:3 (default), 1 = fullscreen (stretch), 2 = widescreen (table colour fill) */
static int g_volume = 10;    /* 0..10 */
static int g_t_opacity = 30;  /* touch button opacity in percent, 30..100 (descore's floor is 30) */
static int g_t_scale = 2;     /* touch scale step 0..8 (2 = 1.00x) */

static void menu_push(int ev)
{
	pthread_mutex_lock(&g_kq_lock);
	if (g_mq_w - g_mq_r < 32) g_mq[g_mq_w++ % 32] = ev;
	pthread_mutex_unlock(&g_kq_lock);
}

static int menu_pop(void)
{
	int r = 0;
	pthread_mutex_lock(&g_kq_lock);
	if (g_mq_r != g_mq_w) r = g_mq[g_mq_r++ % 32];
	pthread_mutex_unlock(&g_kq_lock);
	return r;
}

static int android_to_scancode(int k)
{
	static const unsigned char letters[26] = { /* A..Z */
		0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
		0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
	static const unsigned char digits[10] = {0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
	if (k >= 29 && k <= 54) return letters[k - 29];      /* KEYCODE_A..Z */
	if (k >= 7 && k <= 16) return digits[k - 7];         /* KEYCODE_0..9 */
	if (k >= 131 && k <= 138) return SC_F1 + (k - 131);  /* KEYCODE_F1..F8 */
	switch (k) {
	case 4: case 111: return SC_ESC;                     /* BACK, ESCAPE */
	case 66: case 160: return SC_ENTER;                  /* ENTER, NUMPAD_ENTER (gamepad START opens the menu) */
	case 59: case 21: case 102: case 104: case 99: return SC_LSHIFT;   /* left flipper: shift, dpad left, L1, L2, X */
	case 60: case 22: case 103: case 105: case 97: return SC_RSHIFT;  /* right flipper: shift, dpad right, R1, R2, B */
	case 113: case 114: return SC_CTRL;
	case 57: case 58: return SC_ALT;
	case 62: case 100: return SC_SPACE;                  /* SPACE, BUTTON_Y: nudge */
	case 19: return SC_UP;                               /* DPAD_UP (menus) */
	case 20: case 96: return SC_DOWN;                    /* DPAD_DOWN, BUTTON_A: pull the plunger */
	case 109: return SC_ENTER;                           /* BUTTON_SELECT: start a game */
	case 67: return SC_BACKSPACE;
	default: return -1;
	}
}

/* ---- table chooser: scroll through the boards, then pick one -------------------------------------- */
static const char *const g_cnames[4] = { "PARTY LAND", "SPEED DEVILS", "BILLION DOLLAR GAMESHOW", "STONES N BONES" };

static void key_tap(unsigned char sc) { key_push(sc, 1); key_push(sc, 0); }


static void chooser_key(int key, int down)
{
	if (pbf_in_options()) {   /* the intro's own options page: cursor keys + enter/space, escape leaves */
		int sc = -1;
		switch (key) {
		case 19: sc = SC_UP; break;
		case 20: sc = SC_DOWN; break;
		case 96: case 108: case 66: case 160: case 23: sc = SC_ENTER; break;
		case 100: case 62: sc = SC_SPACE; break;
		case 97: case 4: case 111: sc = SC_ESC; break;
		default: break;
		}
		if (sc >= 0) key_push((unsigned char)sc, down);
		return;
	}
	switch (key) {
	case 108: if (down) g_menu_req = 1; return;                                  /* START: the main menu */
	case 96: case 66: case 160: case 23: key_push(SC_ENTER, down); return;
	case 109: key_push(SC_F5, down); return;                                    /* SELECT: options */
	case 4: case 111: key_push(SC_ESC, down); return;                           /* BACK: the game's own exit */
	case 62: key_push(SC_SPACE, down); return;
	default: break;
	}
	if (key >= 8 && key <= 11) key_push((unsigned char)(0x3B + (key - 8)), down);   /* keyboard 1-4 */
	else if (key >= 131 && key <= 135) key_push((unsigned char)(SC_F1 + (key - 131)), down);
}

void descore_game_key(int key, int down, int unicode)
{
	(void)unicode;
	if (g_menu_open) {   /* the pop-up menu owns the keys while it is showing */
		if (down) {
			switch (key) {
			case 19: menu_push(ME_UP); break;
			case 20: menu_push(ME_DOWN); break;
			case 21: menu_push(ME_LEFT); break;
			case 22: menu_push(ME_RIGHT); break;
			case 23: case 66: case 160: case 96: menu_push(ME_SELECT); break;
			case 4: case 111: case 97: menu_push(ME_BACK); break;
			case 108: menu_push(ME_CLOSE); break;
			default: break;
			}
		}
		return;
	}
	if (pbf_running_intro()) { chooser_key(key, down); return; }   /* table chooser / intro options: no pop-up menu */
	if (key == 108 && pbf_in_demo()) { key_push(SC_ENTER, down); return; }   /* START skips the opening scroll */
	if (key == 108 || key == 4 || key == 111) { if (down) g_menu_req = 1; return; }   /* START / BACK / ESC: menu */
	int sc = android_to_scancode(key);
	if (sc >= 0) key_push((unsigned char)sc, down);
}

void descore_game_axis(int axis, float value)
{
	/* menu navigation from the analog side: d-pad hat (15/16), left stick (0/1) and right stick (11/14).
	 * Up/down scroll; left/right (hat and left stick X only) adjust sliders and toggles. */
	static int state[32];   /* -1, 0, +1 per axis */
	if (axis < 0 || axis >= 32) return;
	int now = value > 0.6f ? 1 : value < -0.6f ? -1 : 0;
	if (now == 0 && (value > 0.35f || value < -0.35f)) now = state[axis];   /* hysteresis */
	if (now == state[axis]) return;
	state[axis] = now;
	if (!now) return;
	if (!g_menu_open) {
		if (!pbf_running_intro()) return;
		int dy = (axis == 1 || axis == 14 || axis == 16);
		if (pbf_in_options()) { if (dy) key_tap(now > 0 ? SC_DOWN : SC_UP); }
		return;
	}
	switch (axis) {
	case 1: case 14: case 16: menu_push(now > 0 ? ME_DOWN : ME_UP); break;    /* left stick Y, right stick Y (RZ), d-pad Y */
	case 0: case 15: menu_push(now > 0 ? ME_RIGHT : ME_LEFT); break;         /* left stick X, d-pad X */
	default: break;
	}
}

/* ---- touch controls ------------------------------------------------------------------------------- */
enum { B_LFLIP, B_RFLIP, B_PLUNGER, B_START, B_NUDGE, B_PAUSE, B_ESC,
       B_F1, B_F2, B_F3, B_F4, B_F5, B_MENTER, B_MUP, B_MDOWN,
       B_ROW0, B_ROW1, B_ROW2, B_ROW3, B_ROW4, B_ROW5, B_ROW6, B_ROW7, B_COUNT };
#define N_ROWS 8
#define B_FIRST_MENU B_F1

static void touch_key(unsigned char key, int down) { key_push(key, down); }
/* tap buttons: B_PAUSE opens the menu; while it is open B_ESC is "back" and the row buttons are its items */
static void touch_tap(int b)
{
	if (g_menu_open) {
		if (b == B_ESC) menu_push(ME_BACK);
		else if (b >= B_ROW0 && b < B_ROW0 + N_ROWS) menu_push(ME_TAP0 + (b - B_ROW0));
	} else if (b == B_PAUSE && !(pbf_running_intro() && pbf_in_options())) g_menu_req = 1;
}
static int g_menu_mode = -1;

static void layout_touch(int w, int h)
{
	static const unsigned char kl[1] = {SC_LSHIFT}, kr[1] = {SC_RSHIFT}, kd[1] = {SC_DOWN},
		ks[1] = {SC_ENTER}, kn[1] = {SC_SPACE}, kp[1] = {SC_P}, ke[1] = {SC_ESC},
		f1[1] = {0x3B}, f2[1] = {0x3C}, f3[1] = {0x3D}, f4[1] = {0x3E}, f5[1] = {SC_F5}, ku[1] = {SC_UP};
	(void)kp; (void)ke;
	float W = (float)w, H = (float)h;
	descore_touch_set_screen_size(w, h);
	/* game buttons (hidden in the menus): sized from the screen HEIGHT so they stay a sensible size on any aspect
	 * ratio, pinned to their corner, and multiplied by the Touch Controls "scale" setting */
	float s = descore_touch_scale_value(descore_touch_scale_step());
	float mg = 0.02f * H;
#define GBTN(idx, right, bottom, fw, fh, keys, n) do { \
		float bw_ = (fw) * H * s, bh_ = (fh) * H * s; \
		float bx_ = (right) ? W - mg - bw_ : mg, by_ = (bottom) ? H - mg - bh_ : mg; \
		descore_touch_set_button((idx), bx_, by_, bw_, bh_, (keys), (n), true); } while (0)
	GBTN(B_LFLIP, 0, 1, 0.40f, 0.30f, kl, 1);
	GBTN(B_RFLIP, 1, 1, 0.40f, 0.30f, kr, 1);
	GBTN(B_START, 0, 0, 0.26f, 0.14f, ks, 1);
	GBTN(B_PAUSE, 1, 0, 0.26f, 0.14f, NULL, 0);   /* menu */
	descore_touch_set_button(B_ESC, 0.0f, 0.0f, 1.0f, 1.0f, NULL, 0, true);   /* hidden: unused outside the menu */
#undef GBTN
	/* plunger / nudge sit above their flippers */
	{
		float fh = 0.30f * H * s, ph = 0.20f * H * s, pw = 0.22f * H * s;
		float py = H - mg - fh - mg - ph;
		descore_touch_set_button(B_PLUNGER, W - mg - pw, py, pw, ph, kd, 1, true);
		descore_touch_set_button(B_NUDGE, mg, py, pw, ph, kn, 1, true);
	}
	/* chooser / options menu: one row of tables along the bottom, controls above it */
	float bw = 0.15f * W, bh = 0.18f * H, by = 0.80f * H;
	descore_touch_set_button(B_F1, 0.02f * W, by, bw, bh, f1, 1, true);
	descore_touch_set_button(B_F2, 0.19f * W, by, bw, bh, f2, 1, true);
	descore_touch_set_button(B_F3, 0.36f * W, by, bw, bh, f3, 1, true);
	descore_touch_set_button(B_F4, 0.53f * W, by, bw, bh, f4, 1, true);
	descore_touch_set_button(B_F5, 0.70f * W, by, bw, bh, f5, 1, true);
	descore_touch_set_button(B_MENTER, 0.85f * W, by, 0.13f * W, bh, ks, 1, true);
	descore_touch_set_button(B_MUP, 0.85f * W, 0.35f * H, 0.13f * W, 0.20f * H, ku, 1, true);
	descore_touch_set_button(B_MDOWN, 0.85f * W, 0.58f * H, 0.13f * W, 0.20f * H, kd, 1, true);
	g_menu_mode = -1;
}

/* show the buttons that belong to what is running (game vs. chooser/options), unless a gamepad is in use */
static void update_touch_mode(void)
{
	int menu = pbf_running_intro();
	int gp = descore_touch_gamepad_connected();
	int mode = g_menu_open ? 2 : menu;
	if (mode == g_menu_mode && gp == 0) return;
	g_menu_mode = gp ? -1 : mode;
	for (int i = 0; i < B_COUNT; i++) {
		int vis;
		if (g_menu_open) vis = (i == B_ESC);   /* the row buttons are placed and shown by the menu itself */
		else if (i >= B_ROW0 || i == B_ESC) vis = 0;
		else if (menu) vis = (i >= B_FIRST_MENU && i < B_ROW0) || i == B_PAUSE;
		else vis = i < B_FIRST_MENU && i != B_ESC;
		descore_touch_set_button_visible(i, !gp && vis);
	}
}

static int g_sw, g_sh;   /* surface size in pixels, for the widescreen fill and for placing the menu's touch rows */
void descore_game_surface_resized(int w, int h) { g_sw = w; g_sh = h; layout_touch(w, h); }

/* ---- platform callbacks --------------------------------------------------------------------------- */
static uint64_t p_now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + (uint64_t)(t.tv_nsec / 1000);
}

static void p_sleep_us(uint32_t us)
{
	struct timespec t = {us / 1000000u, (long)(us % 1000000u) * 1000L};
	nanosleep(&t, NULL);
}

/* ---- settings (pbfset.cfg in the game folder) ------------------------------------------------------ */
static void settings_load(void)
{
	char path[1024], line[64];
	snprintf(path, sizeof path, "%s/pbfset.cfg", g_dir);
	FILE *f = fopen(path, "r");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		int v;
		if (sscanf(line, "display=%d", &v) == 1 && v >= 0 && v <= 2) g_display = v;
		else if (sscanf(line, "volume=%d", &v) == 1 && v >= 0 && v <= 10) g_volume = v;
		else if (sscanf(line, "touch_opacity=%d", &v) == 1 && v >= 30 && v <= 100) g_t_opacity = v;
		else if (sscanf(line, "touch_scale=%d", &v) == 1 && v >= 0 && v < DESCORE_TOUCH_SCALE_NUM_STEPS) g_t_scale = v;
	}
	fclose(f);
}

static void settings_save(void)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/pbfset.cfg", g_dir);
	FILE *f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "display=%d\nvolume=%d\ntouch_opacity=%d\ntouch_scale=%d\n", g_display, g_volume, g_t_opacity, g_t_scale);
	fclose(f);
}

static void apply_volume(void) { descore_audio_set_master_gain((float)g_volume / 10.0f); }
static void layout_touch(int w, int h);
static void apply_touch_look(void) { descore_touch_set_opacity((float)g_t_opacity / 100.0f); descore_touch_set_scale_step(g_t_scale); if (g_sw > 0) layout_touch(g_sw, g_sh); }

/* ---- display modes: 0 = 4:3 with black bars, 1 = stretch to the whole screen, 2 = widescreen ------- */
#define CB_W 1024
#define CB_H 400
static uint8_t g_cbuf[CB_W * CB_H];
static uint8_t g_cpal[768];
static struct { int bw, bh, ox, oy, vx, vy, vw, vh; } g_vp;   /* composed buffer, game image offset, screen viewport */

/* the menu being drawn (built by menu_build) */
enum { MS_MAIN, MS_AUDIO, MS_DISPLAY, MS_TOUCH, MS_QUIT, MS_SETTINGS };
enum { A_RESUME, A_AUDIO, A_DISPLAY, A_TOUCH, A_QUIT, A_VOL, A_DISP0, A_DISP1, A_DISP2, A_BACK, A_INFO, A_QDOS, A_QMENU, A_SETTINGS, A_GAMEOPT, A_OPAC, A_SCALE, A_TABLE0 };   /* A_TABLE0..+3 must stay last */
static int m_state, m_sel, m_n, m_dirty;
static int m_act[8];
static int m_ctx;                /* 1 = opened from the table chooser, 0 = from inside a table */
static int m_savesel[8];
static int g_after_sc = -1;      /* scan code to send once the menu has closed (table pick / game options) */
static char m_lbl[8][32];
static const char *m_title;
static int m_rx[8], m_ry[8], m_rw, m_rh;   /* row rectangles in composed-buffer coordinates */

static void menu_build(void)
{
	int n = 0;
	#define ITEM(a, ...) do { m_act[n] = (a); snprintf(m_lbl[n], sizeof m_lbl[n], __VA_ARGS__); n++; } while (0)
	switch (m_state) {
	case MS_MAIN:
		if (m_ctx) {
			m_title = "PINBALL FANTASIES";
			for (int t = 0; t < 4; t++) ITEM(A_TABLE0 + t, "%s", g_cnames[t]);
			ITEM(A_SETTINGS, "SETTINGS");
			ITEM(A_QDOS, "QUIT");
			break;
		}
		m_title = "PAUSED";
		ITEM(A_RESUME, "RESUME");
		ITEM(A_AUDIO, "AUDIO");
		ITEM(A_DISPLAY, "DISPLAY MODE");
		if (!descore_touch_gamepad_connected()) ITEM(A_TOUCH, "TOUCH CONTROLS");
		ITEM(A_QUIT, "QUIT");
		break;
	case MS_SETTINGS:
		m_title = "SETTINGS";
		ITEM(A_GAMEOPT, "GAME OPTIONS");
		ITEM(A_AUDIO, "AUDIO");
		ITEM(A_DISPLAY, "DISPLAY MODE");
		if (!descore_touch_gamepad_connected()) ITEM(A_TOUCH, "TOUCH CONTROLS");
		ITEM(A_BACK, "BACK");
		break;
	case MS_AUDIO:
		m_title = "AUDIO";
		ITEM(A_VOL, "VOLUME  < %d >", g_volume);
		ITEM(A_BACK, "BACK");
		break;
	case MS_DISPLAY:
		m_title = "DISPLAY MODE";
		ITEM(A_DISP0, "%s4:3 (DEFAULT)", g_display == 0 ? "+ " : "  ");
		ITEM(A_DISP1, "%sFULLSCREEN", g_display == 1 ? "+ " : "  ");
		ITEM(A_DISP2, "%sWIDESCREEN", g_display == 2 ? "+ " : "  ");
		ITEM(A_BACK, "BACK");
		break;
	case MS_TOUCH:
		m_title = "TOUCH CONTROLS";
		ITEM(A_OPAC, "OPACITY  < %d%% >", g_t_opacity);
		ITEM(A_SCALE, "SCALE  < %.2fX >", descore_touch_scale_value(g_t_scale));
		ITEM(A_BACK, "BACK");
		break;
	default:
		m_title = "QUIT";
		ITEM(A_QDOS, "QUIT TO DOS");
		if (!pbf_running_intro()) ITEM(A_QMENU, "QUIT TO MAIN MENU");
		ITEM(A_BACK, "BACK");
		break;
	}
	#undef ITEM
	m_n = n;
	if (m_sel >= n) m_sel = n - 1;
	if (m_sel < 0) m_sel = 0;
	m_dirty = 1;
}

static int menu_parent(int st) { return (m_ctx && (st == MS_AUDIO || st == MS_DISPLAY || st == MS_TOUCH)) ? MS_SETTINGS : MS_MAIN; }
static void menu_goto(int st, int sel) { m_savesel[m_state] = m_sel; m_state = st; m_sel = sel; menu_build(); }
static void menu_back(void) { int p = menu_parent(m_state); m_state = p; m_sel = m_savesel[p]; menu_build(); }

static void draw_rect(uint8_t *b, int bw, int bh, int x, int y, int w, int h, uint8_t c)
{
	for (int j = y; j < y + h; j++) {
		if (j < 0 || j >= bh) continue;
		for (int i = x; i < x + w; i++) if (i >= 0 && i < bw) b[j * bw + i] = c;
	}
}

static int glyph_of(char c)
{
	if (c >= 'a' && c <= 'z') c = (char)(c - 32);
	for (unsigned i = 0; i < sizeof pbf_font / sizeof pbf_font[0]; i++) if (pbf_font[i].c == c) return (int)i;
	return -1;
}

/* text at horizontal scale sx and vertical scale sy (the chooser's 640x240 frame has 1:2 pixels) */
static void draw_text(uint8_t *b, int bw, int bh, int x, int y, int sx, int sy, const char *t, uint8_t c)
{
	for (; *t; t++, x += 6 * sx) {
		int g = glyph_of(*t);
		if (g < 0) continue;
		for (int r = 0; r < 7; r++)
			for (int k = 0; k < 5; k++)
				if (pbf_font[g].r[r] & (0x10 >> k)) draw_rect(b, bw, bh, x + k * sx, y + r * sy, sx, sy, c);
	}
}

/* draw the panel centred on the game image; cols = panel, border, text, highlight */
static void draw_menu(uint8_t *b, int bw, int bh, int ox, int oy, int w, int h, const uint8_t cols[4])
{
	int sy = h / 120; if (sy < 1) sy = 1;
	int sx = sy * (w >= 2 * h ? 2 : 1);
	int maxc = (int)strlen(m_title);
	for (int i = 0; i < m_n; i++) if ((int)strlen(m_lbl[i]) > maxc) maxc = (int)strlen(m_lbl[i]);
	if (maxc < 18) maxc = 18;
	int padx = 4 * sx, pady = 4 * sy, rowh = 11 * sy;
	int pw = maxc * 6 * sx + 2 * padx, ph = (m_n + 1) * rowh + 2 * pady + 2 * sy;
	int px = ox + (w - pw) / 2, py = oy + (h - ph) / 2;
	draw_rect(b, bw, bh, px - sx, py - sy, pw + 2 * sx, ph + 2 * sy, cols[1]);
	draw_rect(b, bw, bh, px, py, pw, ph, cols[0]);
	draw_text(b, bw, bh, px + (pw - (int)strlen(m_title) * 6 * sx) / 2, py + pady, sx, sy, m_title, cols[1]);
	draw_rect(b, bw, bh, px + padx, py + pady + 9 * sy, pw - 2 * padx, sy, cols[1]);
	m_rw = pw - 2 * padx; m_rh = rowh;
	for (int i = 0; i < m_n; i++) {
		int ry = py + pady + 2 * sy + (i + 1) * rowh;
		if (i == m_sel) draw_rect(b, bw, bh, px + padx, ry - sy, m_rw, rowh - sy, cols[3]);
		draw_text(b, bw, bh, px + padx + 2 * sx, ry + sy, sx, sy, m_lbl[i], cols[2]);
		m_rx[i] = px + padx; m_ry[i] = ry - sy;
	}
}

static void pick_free(const unsigned *hist, int n, int skip_mask_count, const int *skip, uint8_t *out)
{
	unsigned char used[256] = {0};
	for (int i = 0; i < skip_mask_count; i++) used[skip[i]] = 1;
	for (int k = 0; k < n; k++) {
		int best = -1;
		for (int i = 255; i >= 0; i--) if (!used[i] && (best < 0 || hist[i] < hist[best])) best = i;
		used[best] = 1; out[k] = (uint8_t)best;
	}
}

/* compose one frame according to the display mode (and the menu overlay if asked) and hand it to descore */
static void emit_frame(const uint8_t *idx, const uint8_t *pal6, int w, int h, int overlay)
{
	int mode = g_display, bw = w, bh = h, ox = 0;
	int wide = 0;
	if (mode == 2) {
		if (g_sw > 0 && g_sh > 0) bw = (int)((long)h * g_sw / g_sh);
		if (bw > w && h <= CB_H) { if (bw > CB_W) bw = CB_W; wide = 1; ox = (bw - w) / 2; }
		else { mode = 0; bw = w; }
	}
	const uint8_t *src = idx, *pal = pal6;
	if (wide || overlay) {
		unsigned hist[256] = {0};
		int npix = w * h;
		for (int i = 0; i < npix; i++) hist[idx[i]]++;
		memcpy(g_cpal, pal6, 768);
		uint8_t free_idx[5]; int nfree = 0;
		pick_free(hist, (wide ? 1 : 0) + (overlay ? 4 : 0), 0, NULL, free_idx);
		if (wide) {
			/* one fixed colour per table: the panel material beside the plunger, dimmed (8-bit RGB sampled there) */
			static const unsigned char pl[4][3] = { {0, 182, 182}, {60, 211, 223}, {48, 199, 158}, {65, 81, 105} };
			const char *pn = pbf_program_name();
			int ti = (pn[0] == 't' && pn[1] >= '1' && pn[1] <= '4') ? pn[1] - '1' : -1;
			uint8_t f = free_idx[nfree++];
			for (int c = 0; c < 3; c++) g_cpal[f * 3 + c] = ti < 0 ? 0 : (uint8_t)(pl[ti][c] * 63 / 255 * 45 / 100);
			memset(g_cbuf, f, (size_t)bw * bh);
			for (int y = 0; y < h; y++) memcpy(g_cbuf + y * bw + ox, idx + y * w, (size_t)w);
		} else {
			memcpy(g_cbuf, idx, (size_t)w * h);
		}
		if (overlay) {
			uint8_t cols[4];
			for (int i = 0; i < 4; i++) cols[i] = free_idx[nfree++];
			static const uint8_t rgb[4][3] = { {4, 5, 16}, {63, 50, 10}, {63, 63, 63}, {8, 20, 50} };
			for (int i = 0; i < 4; i++) for (int c = 0; c < 3; c++) g_cpal[cols[i] * 3 + c] = rgb[i][c];
			draw_menu(g_cbuf, bw, bh, ox, 0, w, h, cols);
		}
		src = g_cbuf; pal = g_cpal;
	}
	DescoreVideoFormat fmt = {bw, bh, 6, (mode == 0) ? DESCORE_ASPECT_FIT : DESCORE_ASPECT_STRETCH, 4, 3};
	descore_video_present(src, pal, &fmt);
	/* where the composed buffer landed on the screen (for the menu's touch rows) */
	int sw = g_sw > 0 ? g_sw : 1, sh = g_sh > 0 ? g_sh : 1;
	if (mode == 0) {
		if (sw * 3 >= sh * 4) { g_vp.vh = sh; g_vp.vw = sh * 4 / 3; } else { g_vp.vw = sw; g_vp.vh = sw * 3 / 4; }
		g_vp.vx = (sw - g_vp.vw) / 2; g_vp.vy = (sh - g_vp.vh) / 2;
	} else { g_vp.vx = g_vp.vy = 0; g_vp.vw = sw; g_vp.vh = sh; }
	g_vp.bw = bw; g_vp.bh = bh; g_vp.ox = ox; g_vp.oy = 0;
}

/* put the touch buttons over the menu rows */
static void menu_place_rows(void)
{
	int gp = descore_touch_gamepad_connected();
	for (int i = 0; i < N_ROWS; i++) {
		if (i < m_n && !gp) {
			float sx = g_vp.vx + (float)m_rx[i] * g_vp.vw / g_vp.bw, sy = g_vp.vy + (float)m_ry[i] * g_vp.vh / g_vp.bh;
			float ww = (float)m_rw * g_vp.vw / g_vp.bw, hh = (float)m_rh * g_vp.vh / g_vp.bh;
			descore_touch_set_button(B_ROW0 + i, sx, sy, ww, hh, NULL, 0, true);
			descore_touch_set_button_visible(B_ROW0 + i, true);
		} else descore_touch_set_button_visible(B_ROW0 + i, false);
	}
}

static void menu_activate(void)
{
	switch (m_act[m_sel]) {
	case A_RESUME: g_menu_open = 0; break;
	case A_TABLE0: case A_TABLE0 + 1: case A_TABLE0 + 2: case A_TABLE0 + 3: g_after_sc = SC_F1 + (m_act[m_sel] - A_TABLE0); g_menu_open = 0; break;
	case A_AUDIO: menu_goto(MS_AUDIO, 0); break;
	case A_DISPLAY: menu_goto(MS_DISPLAY, g_display); break;
	case A_TOUCH: menu_goto(MS_TOUCH, 0); break;
	case A_QUIT: menu_goto(MS_QUIT, 0); break;
	case A_OPAC: g_t_opacity += 10; if (g_t_opacity > 100) g_t_opacity = 30; menu_build(); break;
	case A_SCALE: g_t_scale = (g_t_scale + 1) % DESCORE_TOUCH_SCALE_NUM_STEPS; menu_build(); break;
	case A_VOL: g_volume = (g_volume + 1) % 11; apply_volume(); menu_build(); break;
	case A_DISP0: case A_DISP1: case A_DISP2: g_display = m_act[m_sel] - A_DISP0; menu_build(); break;
	case A_INFO: break;
	case A_BACK: menu_back(); break;
	case A_SETTINGS: menu_goto(MS_SETTINGS, 0); break;
	case A_GAMEOPT: g_after_sc = SC_F5; g_menu_open = 0; break;
	case A_QDOS: g_quit = 1; g_menu_open = 0; break;
	case A_QMENU: pbf_end_program(); g_menu_open = 0; break;
	}
}

static void menu_event(int ev)
{
	switch (ev) {
	case ME_UP: m_sel = (m_sel + m_n - 1) % m_n; m_dirty = 1; break;
	case ME_DOWN: m_sel = (m_sel + 1) % m_n; m_dirty = 1; break;
	case ME_LEFT: case ME_RIGHT:
		if (m_act[m_sel] == A_OPAC) {
			g_t_opacity += (ev == ME_RIGHT) ? 10 : -10;
			if (g_t_opacity < 30) g_t_opacity = 30;
			if (g_t_opacity > 100) g_t_opacity = 100;
			menu_build();
		} else if (m_act[m_sel] == A_SCALE) {
			g_t_scale += (ev == ME_RIGHT) ? 1 : -1;
			if (g_t_scale < 0) g_t_scale = 0;
			if (g_t_scale >= DESCORE_TOUCH_SCALE_NUM_STEPS) g_t_scale = DESCORE_TOUCH_SCALE_NUM_STEPS - 1;
			menu_build();
		} else if (m_act[m_sel] == A_VOL) {
			g_volume += (ev == ME_RIGHT) ? 1 : -1;
			if (g_volume < 0) g_volume = 0;
			if (g_volume > 10) g_volume = 10;
			apply_volume(); menu_build();
		}
		break;
	case ME_SELECT: menu_activate(); break;
	case ME_BACK:
		if (m_state == MS_MAIN) g_menu_open = 0;
		else menu_back();
		break;
	case ME_CLOSE: g_menu_open = 0; break;
	default:
		if (ev >= ME_TAP0 && ev < ME_TAP0 + m_n) { m_sel = ev - ME_TAP0; menu_activate(); }
		break;
	}
}

/* the modal pop-up: the game is frozen (we are inside its frame delivery) while this runs */
static void run_menu(const uint8_t *idx, const uint8_t *pal6, int w, int h)
{
	static uint8_t snap[CB_W * CB_H];
	static uint8_t snap_pal[768];
	if ((size_t)w * h > sizeof snap) return;
	memcpy(snap, idx, (size_t)w * h);
	memcpy(snap_pal, pal6, 768);
	while (menu_pop()) {}
	g_menu_open = 1;
	descore_touch_set_opacity(0.30f); descore_touch_set_scale_step(DESCORE_TOUCH_SCALE_DEFAULT_STEP);   /* the menu rows must not be scaled/opaque */
	m_ctx = pbf_running_intro();
	g_after_sc = -1;
	memset(m_savesel, 0, sizeof m_savesel);
	m_state = MS_MAIN; m_sel = 0; menu_build();
	update_touch_mode();
	while (g_menu_open && !g_quit) {
		int ev;
		while ((ev = menu_pop()) != 0) menu_event(ev);
		if (!g_menu_open) break;
		emit_frame(snap, snap_pal, w, h, 1);
		if (m_dirty) { menu_place_rows(); m_dirty = 0; }
		if (!descore_pump()) { g_quit = 1; break; }
		p_sleep_us(16000);
	}
	g_menu_open = 0;
	apply_touch_look();
	settings_save();
	for (int i = 0; i < N_ROWS; i++) descore_touch_set_button_visible(B_ROW0 + i, false);
	/* drop anything typed meanwhile and release every key the game might think is held */
	pthread_mutex_lock(&g_kq_lock); g_kq_r = g_kq_w; pthread_mutex_unlock(&g_kq_lock);
	static const unsigned char ups[] = {SC_LSHIFT, SC_RSHIFT, SC_DOWN, SC_UP, SC_SPACE, SC_ENTER, SC_CTRL, SC_ALT};
	for (unsigned i = 0; i < sizeof ups; i++) key_push(ups[i], 0);
	if (g_after_sc >= 0 && !g_quit) { key_tap((unsigned char)g_after_sc); g_after_sc = -1; }
	g_menu_mode = -1;
	update_touch_mode();
}

static void p_present(const uint8_t *idx, const uint8_t *pal6, int w, int h)
{
	if (g_menu_req && !g_menu_open) {
		g_menu_req = 0;
		run_menu(idx, pal6, w, h);
	} else {
		emit_frame(idx, pal6, w, h, 0);
		update_touch_mode();
		if (!descore_pump()) g_quit = 1;
	}
	if (g_quit) pbf_stop();
}

static void lower(char *s) { for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

static int p_read_file(const char *name, uint8_t **data, size_t *size)
{
	char path[1024], nm[64];
	snprintf(nm, sizeof nm, "%s", name);
	lower(nm);
	snprintf(path, sizeof path, "%s/%s", g_dir, nm);
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)malloc((size_t)(n > 0 ? n : 1));
	if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return -1; }
	fclose(f);
	*data = buf;
	*size = (size_t)n;
	return 0;
}

static int p_write_file(const char *name, const uint8_t *data, size_t size)
{
	char path[1024], nm[64];
	snprintf(nm, sizeof nm, "%s", name);
	lower(nm);
	snprintf(path, sizeof path, "%s/%s", g_dir, nm);
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	size_t n = fwrite(data, 1, size, f);
	fclose(f);
	return n == size ? 0 : -1;
}

static void p_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	__android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, ap);
	va_end(ap);
}

int descore_game_main(const DescoreGameParams *params)
{
	uint8_t *prg = NULL;
	size_t prg_size = 0;
	int mixer;

	snprintf(g_dir, sizeof g_dir, "%s", params->data_dir);
	LOGI("Pinball Fantasies starting, data dir %s", g_dir);

	settings_load();
	apply_volume();
	g_sw = params->surface_w; g_sh = params->surface_h;
	descore_touch_init(B_COUNT, touch_key, touch_tap);
	layout_touch(params->surface_w, params->surface_h);
	apply_touch_look();

	pbf_plat.now_us = p_now_us;
	pbf_plat.sleep_us = p_sleep_us;
	pbf_plat.present = p_present;
	pbf_plat.read_file = p_read_file;
	pbf_plat.write_file = p_write_file;
	pbf_plat.log = p_log;
	pbf_plat.poll_keys = poll_keys;
	pbf_audio_install();

	if (p_read_file("table1.prg", &prg, &prg_size) != 0) {
		LOGE("table1.prg is missing from %s", g_dir);
		return 1;
	}
	free(prg);
	mixer = descore_audio_add_source(pbf_audio_pull, NULL);
	{
		PbfLaunchEntry intro = { &i1_program, "INTRO.PRG" };
		PbfLaunchEntry tabs[4] = { { &t1_program, "TABLE1.PRG" }, { &t2_program, "TABLE2.PRG" }, { &t3_program, "TABLE3.PRG" }, { &t4_program, "TABLE4.PRG" } };
		uint8_t *probe = NULL; size_t psz = 0;
		if (p_read_file("intro.prg", &probe, &psz) == 0) { free(probe); pbf_session(&intro, tabs, 0); }
		else pbf_session(NULL, tabs, 1);   /* no chooser available: straight into Party Land */
	}
	if (mixer >= 0) descore_audio_remove_source(mixer);
	pbf_audio_shutdown();
	LOGI("Pinball Fantasies finished (exit code %d)", pbf_exit_code());
	return 0;
}
