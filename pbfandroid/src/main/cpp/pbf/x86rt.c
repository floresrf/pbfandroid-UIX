/* x86rt.c -- register file, parity table and default hooks. */
#include "x86rt.h"
#include <stdio.h>

X86Reg x_ax, x_bx, x_cx, x_dx;
uint16_t x_si, x_di, x_bp, x_sp, x_cs, x_ds, x_es, x_ss;
uint8_t x_cf, x_zf, x_sf, x_of, x_pf, x_af, x_df, x_if;
uint8_t *x_mem;
volatile int x_poll_budget = 1;
uint8_t (*x_vga_read8)(uint32_t) ;
void (*x_vga_write8)(uint32_t, uint8_t);

const uint8_t x_parity[256] = {
#define P2(n) n, n ^ 1, n ^ 1, n
#define P4(n) P2(n), P2(n ^ 1), P2(n ^ 1), P2(n)
#define P6(n) P4(n), P4(n ^ 1), P4(n ^ 1), P4(n)
	P6(1), P6(0), P6(0), P6(1)
};

__attribute__((weak)) void x_div_fault(void) { fprintf(stderr, "x86rt: divide error at %04X:???\n", x_cs); }

/* ---- rep string ops (element loops honour the VGA window and wrap within the 64K segment) ------------ */
void x_rep_movsb(uint16_t sseg)
{
	uint32_t n = CX;
	if (!x_df && n && (uint32_t)DI + n <= 0x10000u && (uint32_t)SI + n <= 0x10000u &&
	    !x_is_vga(x_lin(ES, 0)) && !x_is_vga(x_lin(sseg, 0)) && !x_is_vga(x_lin(ES, 0xFFFF)) && !x_is_vga(x_lin(sseg, 0xFFFF))) {
		memmove(x_mem + x_lin(ES, DI), x_mem + x_lin(sseg, SI), n);
		DI = (uint16_t)(DI + n); SI = (uint16_t)(SI + n); CX = 0; return;
	}
	while (CX) { x_movsb(sseg); CX = (uint16_t)(CX - 1); }
}
void x_rep_movsw(uint16_t sseg)
{
	uint32_t n = CX;
	if (!x_df && n && (uint32_t)DI + 2 * n <= 0x10000u && (uint32_t)SI + 2 * n <= 0x10000u &&
	    !x_is_vga(x_lin(ES, 0)) && !x_is_vga(x_lin(sseg, 0)) && !x_is_vga(x_lin(ES, 0xFFFF)) && !x_is_vga(x_lin(sseg, 0xFFFF))) {
		memmove(x_mem + x_lin(ES, DI), x_mem + x_lin(sseg, SI), 2 * n);
		DI = (uint16_t)(DI + 2 * n); SI = (uint16_t)(SI + 2 * n); CX = 0; return;
	}
	while (CX) { x_movsw(sseg); CX = (uint16_t)(CX - 1); }
}
void x_rep_stosb(void)
{
	uint32_t n = CX;
	if (!x_df && n && (uint32_t)DI + n <= 0x10000u && !x_is_vga(x_lin(ES, 0)) && !x_is_vga(x_lin(ES, 0xFFFF))) {
		memset(x_mem + x_lin(ES, DI), AL, n); DI = (uint16_t)(DI + n); CX = 0; return;
	}
	while (CX) { x_stosb(); CX = (uint16_t)(CX - 1); }
}
void x_rep_stosw(void)
{
	uint32_t n = CX;
	if (!x_df && n && (uint32_t)DI + 2 * n <= 0x10000u && !x_is_vga(x_lin(ES, 0)) && !x_is_vga(x_lin(ES, 0xFFFF))) {
		uint8_t *d = x_mem + x_lin(ES, DI);
		for (uint32_t i = 0; i < n; i++) { d[2 * i] = AL; d[2 * i + 1] = AH; }
		DI = (uint16_t)(DI + 2 * n); CX = 0; return;
	}
	while (CX) { x_stosw(); CX = (uint16_t)(CX - 1); }
}
