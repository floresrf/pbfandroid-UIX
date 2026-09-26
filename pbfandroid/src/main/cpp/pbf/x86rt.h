/* x86rt.h -- runtime model for machine-translated 16-bit real-mode code.
 *
 * The assembly-to-C converter turns each 8086/80286 instruction into a statement written with the
 * macros below, so the generated C reads like the assembly it came from:
 *
 *     MOV16(AX, RD16(DS, D_hitpointer));
 *     ADD16(AX, 5, FL_ALL);          // last argument: which flags later code can observe
 *     JZ(L_done);
 *
 * Registers, flags and the segmented 1 MB address space are globals (rt_regs). Byte/word register
 * halves alias exactly like the hardware (little-endian host assumed, checked at startup).
 * Instruction semantics follow the 80286 (shift counts masked to 5 bits, undefined flags cleared).
 */
#ifndef X86RT_H
#define X86RT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define X_INLINE static inline __attribute__((always_inline))
#define X_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define X_LIKELY(x) __builtin_expect(!!(x), 1)

/* ---- registers -------------------------------------------------------------------------- */
typedef union {
	uint16_t x;
	struct { uint8_t l, h; } b;
} X86Reg;

extern X86Reg x_ax, x_bx, x_cx, x_dx;
extern uint16_t x_si, x_di, x_bp, x_sp;
extern uint16_t x_cs, x_ds, x_es, x_ss;
extern uint8_t x_cf, x_zf, x_sf, x_of, x_pf, x_af, x_df, x_if;

#define AX x_ax.x
#define BX x_bx.x
#define CX x_cx.x
#define DX x_dx.x
#define AL x_ax.b.l
#define AH x_ax.b.h
#define BL x_bx.b.l
#define BH x_bx.b.h
#define CL x_cx.b.l
#define CH x_cx.b.h
#define DL x_dx.b.l
#define DH x_dx.b.h
#define SI x_si
#define DI x_di
#define BP x_bp
#define SP x_sp
#define CS x_cs
#define DS x_ds
#define ES x_es
#define SS x_ss

/* ---- memory -------------------------------------------------------------------------------- */
extern uint8_t *x_mem;                    /* 1 MB + 64 KB of real-mode address space */
#define X_MEM_SIZE (0x110000u)

/* Video window hooks (planar VGA emulation lives outside this file). */
extern uint8_t (*x_vga_read8)(uint32_t linear);
extern void (*x_vga_write8)(uint32_t linear, uint8_t v);

X_INLINE uint32_t x_lin(uint16_t seg, uint16_t off) { return ((uint32_t)seg << 4) + off; }

X_INLINE int x_is_vga(uint32_t l) { return (l & 0xFFFF0u) == 0xA0000u || ((l & 0xFFFFFu) >= 0xA0000u && (l & 0xFFFFFu) < 0xB0000u); }

X_INLINE uint8_t RD8(uint16_t seg, uint16_t off)
{
	uint32_t l = x_lin(seg, off) & 0xFFFFFu;
	if (X_UNLIKELY(l >= 0xA0000u && l < 0xB0000u)) return x_vga_read8(l);
	return x_mem[l];
}
X_INLINE void WR8(uint16_t seg, uint16_t off, uint8_t v)
{
	uint32_t l = x_lin(seg, off) & 0xFFFFFu;
	if (X_UNLIKELY(l >= 0xA0000u && l < 0xB0000u)) { x_vga_write8(l, v); return; }
	x_mem[l] = v;
}
X_INLINE uint16_t RD16(uint16_t seg, uint16_t off)
{
	/* a word access wraps within the 64K segment at offset 0xFFFF only on real hardware quirks;
	 * this game never relies on it, but each byte is fetched separately to keep VGA semantics. */
	uint32_t l = x_lin(seg, off) & 0xFFFFFu;
	if (X_UNLIKELY((l >= 0xA0000u - 1) && l < 0xB0000u)) {
		return (uint16_t)(RD8(seg, off) | (RD8(seg, (uint16_t)(off + 1)) << 8));
	}
	return (uint16_t)(x_mem[l] | (x_mem[(l + 1) & 0xFFFFFu] << 8));
}
X_INLINE void WR16(uint16_t seg, uint16_t off, uint16_t v)
{
	uint32_t l = x_lin(seg, off) & 0xFFFFFu;
	if (X_UNLIKELY((l >= 0xA0000u - 1) && l < 0xB0000u)) {
		WR8(seg, off, (uint8_t)v);
		WR8(seg, (uint16_t)(off + 1), (uint8_t)(v >> 8));
		return;
	}
	x_mem[l] = (uint8_t)v;
	x_mem[(l + 1) & 0xFFFFFu] = (uint8_t)(v >> 8);
}

/* ---- flags ----------------------------------------------------------------------------------- */
#define FL_C 1u
#define FL_Z 2u
#define FL_S 4u
#define FL_O 8u
#define FL_P 16u
#define FL_A 32u
#define FL_ALL 63u
#define FL_NONE 0u

extern const uint8_t x_parity[256];

#define X_SETF(m, bit, var, expr) do { if ((m) & (bit)) (var) = (uint8_t)((expr) ? 1 : 0); } while (0)

/* Generates the arithmetic core for one operand width. T = unsigned type, S = signed type,
 * B = bits, W = suffix (8 or 16). */
#define X_DEFINE_ALU(T, S, B, W) \
X_INLINE T x_add##W(T a, T b, unsigned m) { \
	uint32_t r = (uint32_t)a + b; T t = (T)r; \
	X_SETF(m, FL_C, x_cf, r >> B); X_SETF(m, FL_Z, x_zf, t == 0); \
	X_SETF(m, FL_S, x_sf, t >> (B - 1)); X_SETF(m, FL_O, x_of, ((a ^ t) & (b ^ t)) >> (B - 1)); \
	X_SETF(m, FL_A, x_af, (a ^ b ^ t) & 0x10); X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); \
	return t; } \
X_INLINE T x_adc##W(T a, T b, unsigned m) { \
	uint32_t r = (uint32_t)a + b + x_cf; T t = (T)r; \
	X_SETF(m, FL_C, x_cf, r >> B); X_SETF(m, FL_Z, x_zf, t == 0); \
	X_SETF(m, FL_S, x_sf, t >> (B - 1)); X_SETF(m, FL_O, x_of, ((a ^ t) & (b ^ t)) >> (B - 1)); \
	X_SETF(m, FL_A, x_af, (a ^ b ^ t) & 0x10); X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); \
	return t; } \
X_INLINE T x_sub##W(T a, T b, unsigned m) { \
	T t = (T)(a - b); \
	X_SETF(m, FL_C, x_cf, a < b); X_SETF(m, FL_Z, x_zf, t == 0); \
	X_SETF(m, FL_S, x_sf, t >> (B - 1)); X_SETF(m, FL_O, x_of, ((a ^ b) & (a ^ t)) >> (B - 1)); \
	X_SETF(m, FL_A, x_af, (a ^ b ^ t) & 0x10); X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); \
	return t; } \
X_INLINE T x_sbb##W(T a, T b, unsigned m) { \
	uint32_t c = x_cf; T t = (T)(a - b - c); \
	X_SETF(m, FL_C, x_cf, (uint32_t)a < (uint32_t)b + c); X_SETF(m, FL_Z, x_zf, t == 0); \
	X_SETF(m, FL_S, x_sf, t >> (B - 1)); X_SETF(m, FL_O, x_of, ((a ^ b) & (a ^ t)) >> (B - 1)); \
	X_SETF(m, FL_A, x_af, (a ^ b ^ t) & 0x10); X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); \
	return t; } \
X_INLINE T x_logic##W(T t, unsigned m) { \
	X_SETF(m, FL_C, x_cf, 0); X_SETF(m, FL_O, x_of, 0); X_SETF(m, FL_A, x_af, 0); \
	X_SETF(m, FL_Z, x_zf, t == 0); X_SETF(m, FL_S, x_sf, t >> (B - 1)); \
	X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); return t; } \
X_INLINE T x_inc##W(T a, unsigned m) { \
	T t = (T)(a + 1); \
	X_SETF(m, FL_Z, x_zf, t == 0); X_SETF(m, FL_S, x_sf, t >> (B - 1)); \
	X_SETF(m, FL_O, x_of, t == ((T)1 << (B - 1))); X_SETF(m, FL_A, x_af, (a ^ 1 ^ t) & 0x10); \
	X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); return t; } \
X_INLINE T x_dec##W(T a, unsigned m) { \
	T t = (T)(a - 1); \
	X_SETF(m, FL_Z, x_zf, t == 0); X_SETF(m, FL_S, x_sf, t >> (B - 1)); \
	X_SETF(m, FL_O, x_of, a == ((T)1 << (B - 1))); X_SETF(m, FL_A, x_af, (a ^ 1 ^ t) & 0x10); \
	X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); return t; } \
X_INLINE T x_neg##W(T a, unsigned m) { return x_sub##W(0, a, m); } \
X_INLINE T x_shl##W(T a, unsigned n, unsigned m) { \
	n &= 31; if (n == 0) return a; \
	uint32_t r = n >= 32 ? 0 : ((uint32_t)a << n); T t = (T)r; \
	int c = n > B ? 0 : (int)((((uint32_t)a) >> (B - n)) & 1); \
	X_SETF(m, FL_C, x_cf, c); X_SETF(m, FL_Z, x_zf, t == 0); X_SETF(m, FL_S, x_sf, t >> (B - 1)); \
	X_SETF(m, FL_O, x_of, ((t >> (B - 1)) & 1) ^ c); X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); \
	X_SETF(m, FL_A, x_af, 0); return t; } \
X_INLINE T x_shr##W(T a, unsigned n, unsigned m) { \
	n &= 31; if (n == 0) return a; \
	T t = n >= B ? 0 : (T)(a >> n); \
	int c = n > B ? 0 : (int)((((uint32_t)a) >> (n - 1)) & 1); \
	X_SETF(m, FL_C, x_cf, c); X_SETF(m, FL_Z, x_zf, t == 0); X_SETF(m, FL_S, x_sf, t >> (B - 1)); \
	X_SETF(m, FL_O, x_of, (a >> (B - 1)) & 1); X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); \
	X_SETF(m, FL_A, x_af, 0); return t; } \
X_INLINE T x_sar##W(T a, unsigned n, unsigned m) { \
	n &= 31; if (n == 0) return a; \
	S sa = (S)a; int c; T t; \
	if (n >= B) { t = (T)(sa < 0 ? (T)-1 : 0); c = sa < 0; } \
	else { t = (T)(sa >> n); c = (int)((((int32_t)sa) >> (n - 1)) & 1); } \
	X_SETF(m, FL_C, x_cf, c); X_SETF(m, FL_Z, x_zf, t == 0); X_SETF(m, FL_S, x_sf, t >> (B - 1)); \
	X_SETF(m, FL_O, x_of, 0); X_SETF(m, FL_P, x_pf, x_parity[(uint8_t)t]); X_SETF(m, FL_A, x_af, 0); \
	return t; } \
X_INLINE T x_rol##W(T a, unsigned n, unsigned m) { \
	n &= 31; if (n == 0) return a; unsigned k = n % B; \
	T t = k ? (T)((a << k) | (a >> (B - k))) : a; \
	X_SETF(m, FL_C, x_cf, t & 1); X_SETF(m, FL_O, x_of, ((t >> (B - 1)) & 1) ^ (t & 1)); return t; } \
X_INLINE T x_ror##W(T a, unsigned n, unsigned m) { \
	n &= 31; if (n == 0) return a; unsigned k = n % B; \
	T t = k ? (T)((a >> k) | (a << (B - k))) : a; \
	X_SETF(m, FL_C, x_cf, (t >> (B - 1)) & 1); \
	X_SETF(m, FL_O, x_of, ((t >> (B - 1)) ^ (t >> (B - 2))) & 1); return t; } \
X_INLINE T x_rcl##W(T a, unsigned n, unsigned m) { \
	n &= 31; if (n == 0) return a; unsigned k = n % (B + 1); \
	uint32_t v = ((uint32_t)x_cf << B) | a; uint32_t r = k ? ((v << k) | (v >> (B + 1 - k))) : v; \
	r &= (((uint32_t)1 << (B + 1)) - 1); T t = (T)r; \
	X_SETF(m, FL_C, x_cf, (r >> B) & 1); X_SETF(m, FL_O, x_of, ((t >> (B - 1)) & 1) ^ ((r >> B) & 1)); \
	return t; } \
X_INLINE T x_rcr##W(T a, unsigned n, unsigned m) { \
	n &= 31; if (n == 0) return a; unsigned k = n % (B + 1); \
	uint32_t v = ((uint32_t)x_cf << B) | a; uint32_t r = k ? ((v >> k) | (v << (B + 1 - k))) : v; \
	r &= (((uint32_t)1 << (B + 1)) - 1); T t = (T)r; \
	X_SETF(m, FL_O, x_of, (((uint32_t)x_cf ^ (a >> (B - 1))) & 1)); \
	X_SETF(m, FL_C, x_cf, (r >> B) & 1); return t; }

X_DEFINE_ALU(uint8_t, int8_t, 8, 8)
X_DEFINE_ALU(uint16_t, int16_t, 16, 16)

/* Statement-style wrappers: dst is an lvalue, src any expression. The unused-flag argument m
 * lets the compiler drop flag computations nobody reads. */
#define ADD8(d, s, m) ((d) = x_add8((d), (s), (m)))
#define ADD16(d, s, m) ((d) = x_add16((d), (s), (m)))
#define ADC8(d, s, m) ((d) = x_adc8((d), (s), (m)))
#define ADC16(d, s, m) ((d) = x_adc16((d), (s), (m)))
#define SUB8(d, s, m) ((d) = x_sub8((d), (s), (m)))
#define SUB16(d, s, m) ((d) = x_sub16((d), (s), (m)))
#define SBB8(d, s, m) ((d) = x_sbb8((d), (s), (m)))
#define SBB16(d, s, m) ((d) = x_sbb16((d), (s), (m)))
#define CMP8(a, b, m) ((void)x_sub8((a), (b), (m)))
#define CMP16(a, b, m) ((void)x_sub16((a), (b), (m)))
#define AND8(d, s, m) ((d) = x_logic8((uint8_t)((d) & (s)), (m)))
#define AND16(d, s, m) ((d) = x_logic16((uint16_t)((d) & (s)), (m)))
#define OR8(d, s, m) ((d) = x_logic8((uint8_t)((d) | (s)), (m)))
#define OR16(d, s, m) ((d) = x_logic16((uint16_t)((d) | (s)), (m)))
#define XOR8(d, s, m) ((d) = x_logic8((uint8_t)((d) ^ (s)), (m)))
#define XOR16(d, s, m) ((d) = x_logic16((uint16_t)((d) ^ (s)), (m)))
#define TEST8(a, b, m) ((void)x_logic8((uint8_t)((a) & (b)), (m)))
#define TEST16(a, b, m) ((void)x_logic16((uint16_t)((a) & (b)), (m)))
#define INC8(d, m) ((d) = x_inc8((d), (m)))
#define INC16(d, m) ((d) = x_inc16((d), (m)))
#define DEC8(d, m) ((d) = x_dec8((d), (m)))
#define DEC16(d, m) ((d) = x_dec16((d), (m)))
#define NEG8(d, m) ((d) = x_neg8((d), (m)))
#define NEG16(d, m) ((d) = x_neg16((d), (m)))
#define NOT8(d) ((d) = (uint8_t)~(d))
#define NOT16(d) ((d) = (uint16_t)~(d))
#define SHL8(d, n, m) ((d) = x_shl8((d), (n), (m)))
#define SHL16(d, n, m) ((d) = x_shl16((d), (n), (m)))
#define SHR8(d, n, m) ((d) = x_shr8((d), (n), (m)))
#define SHR16(d, n, m) ((d) = x_shr16((d), (n), (m)))
#define SAR8(d, n, m) ((d) = x_sar8((d), (n), (m)))
#define SAR16(d, n, m) ((d) = x_sar16((d), (n), (m)))
#define ROL8(d, n, m) ((d) = x_rol8((d), (n), (m)))
#define ROL16(d, n, m) ((d) = x_rol16((d), (n), (m)))
#define ROR8(d, n, m) ((d) = x_ror8((d), (n), (m)))
#define ROR16(d, n, m) ((d) = x_ror16((d), (n), (m)))
#define RCL8(d, n, m) ((d) = x_rcl8((d), (n), (m)))
#define RCL16(d, n, m) ((d) = x_rcl16((d), (n), (m)))
#define RCR8(d, n, m) ((d) = x_rcr8((d), (n), (m)))
#define RCR16(d, n, m) ((d) = x_rcr16((d), (n), (m)))

/* ---- multiply / divide -------------------------------------------------------------------- */
void x_div_fault(void);

X_INLINE void x_mul8(uint8_t s) { AX = (uint16_t)(AL * s); x_cf = x_of = (AH != 0); }
X_INLINE void x_mul16(uint16_t s) { uint32_t r = (uint32_t)AX * s; AX = (uint16_t)r; DX = (uint16_t)(r >> 16); x_cf = x_of = (DX != 0); }
X_INLINE void x_imul8(uint8_t s) { int16_t r = (int16_t)(int8_t)AL * (int8_t)s; AX = (uint16_t)r; x_cf = x_of = (r != (int8_t)r); }
X_INLINE void x_imul16(uint16_t s) { int32_t r = (int32_t)(int16_t)AX * (int16_t)s; AX = (uint16_t)r; DX = (uint16_t)(r >> 16); x_cf = x_of = (r != (int16_t)r); }
X_INLINE uint16_t x_imul16_3(uint16_t a, uint16_t b) { int32_t r = (int32_t)(int16_t)a * (int16_t)b; x_cf = x_of = (r != (int16_t)r); return (uint16_t)r; }
X_INLINE void x_div8(uint8_t s) { if (s == 0 || (AX / s) > 0xFF) { x_div_fault(); return; } uint8_t q = (uint8_t)(AX / s), r = (uint8_t)(AX % s); AL = q; AH = r; }
X_INLINE void x_div16(uint16_t s) { uint32_t n = ((uint32_t)DX << 16) | AX; if (s == 0 || (n / s) > 0xFFFF) { x_div_fault(); return; } uint16_t q = (uint16_t)(n / s), r = (uint16_t)(n % s); AX = q; DX = r; }
X_INLINE void x_idiv8(uint8_t s) { int16_t n = (int16_t)AX; int8_t d = (int8_t)s; if (d == 0 || (n / d) > 127 || (n / d) < -128) { x_div_fault(); return; } int8_t q = (int8_t)(n / d), r = (int8_t)(n % d); AL = (uint8_t)q; AH = (uint8_t)r; }
X_INLINE void x_idiv16(uint16_t s) { int32_t n = (int32_t)(((uint32_t)DX << 16) | AX); int16_t d = (int16_t)s; if (d == 0 || (n / d) > 32767 || (n / d) < -32768) { x_div_fault(); return; } int16_t q = (int16_t)(n / d), r = (int16_t)(n % d); AX = (uint16_t)q; DX = (uint16_t)r; }
#define MUL8(s) x_mul8((uint8_t)(s))
#define MUL16(s) x_mul16((uint16_t)(s))
#define IMUL8(s) x_imul8((uint8_t)(s))
#define IMUL16(s) x_imul16((uint16_t)(s))
#define DIV8(s) x_div8((uint8_t)(s))
#define DIV16(s) x_div16((uint16_t)(s))
#define IDIV8(s) x_idiv8((uint8_t)(s))
#define IDIV16(s) x_idiv16((uint16_t)(s))
#define CBW() (AX = (uint16_t)(int16_t)(int8_t)AL)
#define CWD() (DX = (AX & 0x8000u) ? 0xFFFFu : 0)

/* ---- moves, stack, flags ------------------------------------------------------------------ */
#define MOV8(d, s) ((d) = (uint8_t)(s))
#define MOV16(d, s) ((d) = (uint16_t)(s))
X_INLINE void PUSH16(uint16_t v) { SP = (uint16_t)(SP - 2); WR16(SS, SP, v); }
X_INLINE uint16_t POP16(void) { uint16_t v = RD16(SS, SP); SP = (uint16_t)(SP + 2); return v; }
X_INLINE uint16_t x_getflags(void)
{
	return (uint16_t)(x_cf | 2u | (x_pf << 2) | (x_af << 4) | (x_zf << 6) | (x_sf << 7) | (x_if << 9) | (x_df << 10) | (x_of << 11));
}
X_INLINE void x_setflags(uint16_t f)
{
	x_cf = f & 1; x_pf = (f >> 2) & 1; x_af = (f >> 4) & 1; x_zf = (f >> 6) & 1; x_sf = (f >> 7) & 1;
	x_if = (f >> 9) & 1; x_df = (f >> 10) & 1; x_of = (f >> 11) & 1;
}
#define PUSHF() PUSH16(x_getflags())
#define POPF() x_setflags(POP16())
X_INLINE void PUSHA(void)
{
	uint16_t t = SP;
	PUSH16(AX); PUSH16(CX); PUSH16(DX); PUSH16(BX); PUSH16(t); PUSH16(BP); PUSH16(SI); PUSH16(DI);
}
X_INLINE void POPA(void)
{
	DI = POP16(); SI = POP16(); BP = POP16(); (void)POP16(); BX = POP16(); DX = POP16(); CX = POP16(); AX = POP16();
}
#define LAHF() (AH = (uint8_t)(x_getflags() & 0xFF))
#define SAHF() do { uint8_t t_ = AH; x_cf = t_ & 1; x_pf = (t_ >> 2) & 1; x_af = (t_ >> 4) & 1; x_zf = (t_ >> 6) & 1; x_sf = (t_ >> 7) & 1; } while (0)
#define CLC() (x_cf = 0)
#define STC() (x_cf = 1)
#define CMC() (x_cf ^= 1)
#define CLD() (x_df = 0)
#define STD() (x_df = 1)
#define CLI() (x_if = 0)
#define STI() (x_if = 1)
#define XCHG8(a, b) do { uint8_t t_ = (a); (a) = (b); (b) = t_; } while (0)
#define XCHG16(a, b) do { uint16_t t_ = (a); (a) = (b); (b) = t_; } while (0)
#define XLAT(seg) (AL = RD8((seg), (uint16_t)(BX + AL)))

/* ---- condition codes ------------------------------------------------------------------------ */
#define C_O (x_of)
#define C_NO (!x_of)
#define C_B (x_cf)
#define C_AE (!x_cf)
#define C_E (x_zf)
#define C_NE (!x_zf)
#define C_BE (x_cf || x_zf)
#define C_A (!x_cf && !x_zf)
#define C_S (x_sf)
#define C_NS (!x_sf)
#define C_P (x_pf)
#define C_NP (!x_pf)
#define C_L (x_sf != x_of)
#define C_GE (x_sf == x_of)
#define C_LE (x_zf || (x_sf != x_of))
#define C_G (!x_zf && (x_sf == x_of))

/* ---- string instructions -------------------------------------------------------------------- */
/* Each helper performs one iteration; REP forms are generated as loops so a pending hardware
 * event cannot be observed mid-instruction anyway (no interrupts are delivered inside). */
#define X_STEP(n) (x_df ? (uint16_t)-(n) : (uint16_t)(n))
X_INLINE void x_movsb(uint16_t sseg) { WR8(ES, DI, RD8(sseg, SI)); SI = (uint16_t)(SI + X_STEP(1)); DI = (uint16_t)(DI + X_STEP(1)); }
X_INLINE void x_movsw(uint16_t sseg) { WR16(ES, DI, RD16(sseg, SI)); SI = (uint16_t)(SI + X_STEP(2)); DI = (uint16_t)(DI + X_STEP(2)); }
X_INLINE void x_stosb(void) { WR8(ES, DI, AL); DI = (uint16_t)(DI + X_STEP(1)); }
X_INLINE void x_stosw(void) { WR16(ES, DI, AX); DI = (uint16_t)(DI + X_STEP(2)); }
X_INLINE void x_lodsb(uint16_t sseg) { AL = RD8(sseg, SI); SI = (uint16_t)(SI + X_STEP(1)); }
X_INLINE void x_lodsw(uint16_t sseg) { AX = RD16(sseg, SI); SI = (uint16_t)(SI + X_STEP(2)); }
X_INLINE void x_scasb(void) { (void)x_sub8(AL, RD8(ES, DI), FL_ALL); DI = (uint16_t)(DI + X_STEP(1)); }
X_INLINE void x_scasw(void) { (void)x_sub16(AX, RD16(ES, DI), FL_ALL); DI = (uint16_t)(DI + X_STEP(2)); }
X_INLINE void x_cmpsb(uint16_t sseg) { (void)x_sub8(RD8(sseg, SI), RD8(ES, DI), FL_ALL); SI = (uint16_t)(SI + X_STEP(1)); DI = (uint16_t)(DI + X_STEP(1)); }
X_INLINE void x_cmpsw(uint16_t sseg) { (void)x_sub16(RD16(sseg, SI), RD16(ES, DI), FL_ALL); SI = (uint16_t)(SI + X_STEP(2)); DI = (uint16_t)(DI + X_STEP(2)); }

/* Fast bulk versions of REP MOVS/STOS: same result as looping, plus the VGA hook when needed. */
void x_rep_movsb(uint16_t sseg);
void x_rep_movsw(uint16_t sseg);
void x_rep_stosb(void);
void x_rep_stosw(void);

#define REP_LOOP(stmt) do { while (CX != 0) { stmt; CX = (uint16_t)(CX - 1); } } while (0)
#define REPE_LOOP(stmt) do { while (CX != 0) { stmt; CX = (uint16_t)(CX - 1); if (!x_zf) break; } } while (0)
#define REPNE_LOOP(stmt) do { while (CX != 0) { stmt; CX = (uint16_t)(CX - 1); if (x_zf) break; } } while (0)

/* ---- I/O, interrupts ----------------------------------------------------------------------------- */
uint8_t x_in8(uint16_t port);
uint16_t x_in16(uint16_t port);
void x_out8(uint16_t port, uint8_t v);
void x_out16(uint16_t port, uint16_t v);
void x_int(uint8_t n);

#define IN8(p) x_in8((uint16_t)(p))
#define IN16(p) x_in16((uint16_t)(p))
#define OUT8(p, v) x_out8((uint16_t)(p), (uint8_t)(v))
#define OUT16(p, v) x_out16((uint16_t)(p), (uint16_t)(v))

/* ---- control transfer ----------------------------------------------------------------------------- */
typedef void (*x_fn)(void);

/* Indirect call/jump through a code address held in a register or memory. The translation table is
 * generated with the code. */
extern x_fn x_dispatch(uint16_t code_seg_id, uint16_t offset);
void x_bad_target(uint16_t code_seg_id, uint16_t offset);


/* interrupt checkpoint: hardware events (timer/vblank/raster/keyboard) are delivered here */
extern volatile int x_poll_budget;
void x_poll_slow(void);
#define X_POLL() do { if (X_UNLIKELY(--x_poll_budget < 0)) x_poll_slow(); } while (0)

/* ---- generated-code control flow helpers ------------------------------------------------- */
#ifdef __clang__
#define TAIL(f) do { __attribute__((musttail)) return f(); } while (0)
#define TAILPTR(p) do { x_fn tp_ = (p); __attribute__((musttail)) return tp_(); } while (0)
#else
#define TAIL(f) do { f(); return; } while (0)
#define TAILPTR(p) do { (p)(); return; } while (0)
#endif
extern uint16_t x_last_ret;
void x_bad_return(uint16_t expected, uint16_t got);
#ifdef X_CHECK_RET
#define X_RET_CHECK(ret) do { if (x_last_ret != (ret)) x_bad_return((ret), x_last_ret); } while (0)
#define X_RET_NOTE() (x_last_ret = RD16(SS, SP))
#else
#define X_RET_CHECK(ret) ((void)0)
#define X_RET_NOTE() ((void)0)
#endif
#define CALLN(fn, ret) do { PUSH16(ret); fn(); X_RET_CHECK(ret); } while (0)
#define CALLFAR(fn, seg, ret) do { uint16_t cs_ = CS; PUSH16(cs_); PUSH16(ret); CS = (seg); fn(); CS = cs_; } while (0)
/* far call through a pointer stored in memory (offset word, then segment word) */
#define CALLFARPTR(sg, ea, ret) do { uint16_t ea_ = (ea), o_ = RD16(sg, ea_), s_ = RD16(sg, (uint16_t)(ea_ + 2)), cs_ = CS; \
	PUSH16(cs_); PUSH16(ret); CS = s_; x_dispatch(s_, o_)(); CS = cs_; } while (0)
#define TAILFARPTR(sg, ea) do { uint16_t ea_ = (ea), o_ = RD16(sg, ea_), s_ = RD16(sg, (uint16_t)(ea_ + 2)); \
	CS = s_; TAILPTR(x_dispatch(s_, o_)); } while (0)
#define CALLPTR(p, ret) do { PUSH16(ret); (p)(); X_RET_CHECK(ret); } while (0)
#define RETURN_NEAR(n) do { X_RET_NOTE(); SP = (uint16_t)(SP + 2 + (n)); return; } while (0)
#define RETURN_FAR(n) do { (void)POP16(); CS = POP16(); SP = (uint16_t)(SP + (n)); return; } while (0)
#define RETURN_IRET() do { (void)POP16(); CS = POP16(); POPF(); return; } while (0)
void x_fell_off(const char *fn);
void x_hlt(void);
X_INLINE void x_aaa(void)
{
	if ((AL & 0x0F) > 9 || x_af) { AL = (uint8_t)(AL + 6); AH = (uint8_t)(AH + 1); x_af = x_cf = 1; }
	else { x_af = x_cf = 0; }
	AL &= 0x0F;
}

#ifdef __cplusplus
}
#endif
#endif
