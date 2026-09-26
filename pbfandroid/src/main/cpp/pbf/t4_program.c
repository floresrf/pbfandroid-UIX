/* generated: segment variables and program descriptor */
#include "t4_syms.h"
#include "pbfrt.h"

uint16_t t4_xseg_STACKEN;
uint16_t t4_xseg_CODE;
uint16_t t4_xseg_DATAFLIP;
uint16_t t4_xseg_DATA;
uint16_t t4_xseg_DATA2;
uint16_t t4_xseg_HIDDEN1;
uint16_t t4_xseg_HIDDEN2;
uint16_t t4_xseg_MASK1_2;
uint16_t t4_xseg_MASK1_1;
uint16_t t4_xseg_MASK2_2;
uint16_t t4_xseg_FLIPLEFT;
uint16_t t4_xseg_FLIPLEFT2;
uint16_t t4_xseg_FLIPRIGHT;
uint16_t t4_xseg_ANIM08SEG;
uint16_t t4_xseg_LASTSEG;
uint16_t t4_xseg_STAGE1_1;
uint16_t t4_xseg_STAGE1_2;
uint16_t t4_xseg_STAGE1_3;
uint16_t t4_xseg_STAGE1_4;
uint16_t t4_xseg_MASK1_3;
uint16_t t4_xseg_MASK2_1;
uint16_t t4_xseg_MASK2_3;
uint16_t t4_xseg_FLIPGFXSEG;
uint16_t t4_xseg_SPRINGSEG;
uint16_t t4_xseg_CODE2;
extern const PbfInitRange t4_init_ranges[]; extern const PbfKeep t4_keep_prg[]; extern const PbfSegFix t4_seg_fixups[];
extern const PbfDispSeg t4_disp_segs[];
const PbfSegVar t4_segvars[] = {
	{ "STACKEN", &t4_xseg_STACKEN, 0xFFFF },
	{ "CODE", &t4_xseg_CODE, 0x10 },
	{ "DATAFLIP", &t4_xseg_DATAFLIP, 0xC88 },
	{ "DATA", &t4_xseg_DATA, 0x164D },
	{ "DATA2", &t4_xseg_DATA2, 0x1D8F },
	{ "HIDDEN1", &t4_xseg_HIDDEN1, 0x287B },
	{ "HIDDEN2", &t4_xseg_HIDDEN2, 0x2E73 },
	{ "MASK1_2", &t4_xseg_MASK1_2, 0x345B },
	{ "MASK1_1", &t4_xseg_MASK1_1, 0x39FB },
	{ "MASK2_2", &t4_xseg_MASK2_2, 0x3F9B },
	{ "FLIPLEFT", &t4_xseg_FLIPLEFT, 0x453B },
	{ "FLIPLEFT2", &t4_xseg_FLIPLEFT2, 0x479D },
	{ "FLIPRIGHT", &t4_xseg_FLIPRIGHT, 0x479D },
	{ "ANIM08SEG", &t4_xseg_ANIM08SEG, 0x49FF },
	{ "LASTSEG", &t4_xseg_LASTSEG, 0x4BA1 },
	{ "STAGE1_1", &t4_xseg_STAGE1_1, 0x4BA1 },
	{ "STAGE1_2", &t4_xseg_STAGE1_2, 0x5480 },
	{ "STAGE1_3", &t4_xseg_STAGE1_3, 0x5D87 },
	{ "STAGE1_4", &t4_xseg_STAGE1_4, 0x66C2 },
	{ "MASK1_3", &t4_xseg_MASK1_3, 0x6E67 },
	{ "MASK2_1", &t4_xseg_MASK2_1, 0x7407 },
	{ "MASK2_3", &t4_xseg_MASK2_3, 0x79A7 },
	{ "FLIPGFXSEG", &t4_xseg_FLIPGFXSEG, 0x7F47 },
	{ "SPRINGSEG", &t4_xseg_SPRINGSEG, 0x7F4F },
	{ "CODE2", &t4_xseg_CODE2, 0xB40 },
	{ 0, 0, 0 }
};
const PbfProgram t4_program = { t4_init_ranges, t4_keep_prg, t4_seg_fixups, t4_disp_segs, t4_segvars, t4_f_FANTASIES, "t4_", NULL, 0x0, 0 };
