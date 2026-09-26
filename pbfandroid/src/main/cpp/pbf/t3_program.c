/* generated: segment variables and program descriptor */
#include "t3_syms.h"
#include "pbfrt.h"

uint16_t t3_xseg_STACKEN;
uint16_t t3_xseg_CODE;
uint16_t t3_xseg_DATAFLIP;
uint16_t t3_xseg_DATA;
uint16_t t3_xseg_HIDDEN1;
uint16_t t3_xseg_HIDDEN2;
uint16_t t3_xseg_MASK1_2;
uint16_t t3_xseg_MASK1_1;
uint16_t t3_xseg_MASK2_2;
uint16_t t3_xseg_FLIPLEFT;
uint16_t t3_xseg_FLIPLEFT2;
uint16_t t3_xseg_FLIPRIGHT;
uint16_t t3_xseg_LASTSEG;
uint16_t t3_xseg_STAGE1_1;
uint16_t t3_xseg_STAGE1_2;
uint16_t t3_xseg_STAGE1_3;
uint16_t t3_xseg_STAGE1_4;
uint16_t t3_xseg_MASK1_3;
uint16_t t3_xseg_MASK2_1;
uint16_t t3_xseg_MASK2_3;
uint16_t t3_xseg_FLIPGFXSEG;
uint16_t t3_xseg_SPRINGSEG;
uint16_t t3_xseg_DATA2;
uint16_t t3_xseg_CODE2;
extern const PbfInitRange t3_init_ranges[]; extern const PbfKeep t3_keep_prg[]; extern const PbfSegFix t3_seg_fixups[];
extern const PbfDispSeg t3_disp_segs[];
const PbfSegVar t3_segvars[] = {
	{ "STACKEN", &t3_xseg_STACKEN, 0xFFFF },
	{ "CODE", &t3_xseg_CODE, 0x10 },
	{ "DATAFLIP", &t3_xseg_DATAFLIP, 0xB37 },
	{ "DATA", &t3_xseg_DATA, 0x1896 },
	{ "HIDDEN1", &t3_xseg_HIDDEN1, 0x1F0F },
	{ "HIDDEN2", &t3_xseg_HIDDEN2, 0x2507 },
	{ "MASK1_2", &t3_xseg_MASK1_2, 0x2AEF },
	{ "MASK1_1", &t3_xseg_MASK1_1, 0x308F },
	{ "MASK2_2", &t3_xseg_MASK2_2, 0x362F },
	{ "FLIPLEFT", &t3_xseg_FLIPLEFT, 0x3BCF },
	{ "FLIPLEFT2", &t3_xseg_FLIPLEFT2, 0x3E31 },
	{ "FLIPRIGHT", &t3_xseg_FLIPRIGHT, 0x3F2A },
	{ "LASTSEG", &t3_xseg_LASTSEG, 0x4C96 },
	{ "STAGE1_1", &t3_xseg_STAGE1_1, 0x4C96 },
	{ "STAGE1_2", &t3_xseg_STAGE1_2, 0x5221 },
	{ "STAGE1_3", &t3_xseg_STAGE1_3, 0x5A4B },
	{ "STAGE1_4", &t3_xseg_STAGE1_4, 0x632D },
	{ "MASK1_3", &t3_xseg_MASK1_3, 0x6A1F },
	{ "MASK2_1", &t3_xseg_MASK2_1, 0x6FBF },
	{ "MASK2_3", &t3_xseg_MASK2_3, 0x755F },
	{ "FLIPGFXSEG", &t3_xseg_FLIPGFXSEG, 0x7AFF },
	{ "SPRINGSEG", &t3_xseg_SPRINGSEG, 0x7B0D },
	{ "DATA2", &t3_xseg_DATA2, 0x418C },
	{ "CODE2", &t3_xseg_CODE2, 0x9EF },
	{ 0, 0, 0 }
};
const PbfProgram t3_program = { t3_init_ranges, t3_keep_prg, t3_seg_fixups, t3_disp_segs, t3_segvars, t3_f_FANTASIES, "t3_", NULL, 0x0, 0 };
