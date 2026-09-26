/* generated: segment variables and program descriptor */
#include "t1_syms.h"
#include "pbfrt.h"

uint16_t t1_xseg_STACKEN;
uint16_t t1_xseg_CODE;
uint16_t t1_xseg_DATAFLIP;
uint16_t t1_xseg_DATA;
uint16_t t1_xseg_DATA2;
uint16_t t1_xseg_HIDDEN1;
uint16_t t1_xseg_HIDDEN2;
uint16_t t1_xseg_MASK1_2;
uint16_t t1_xseg_MASK1_1;
uint16_t t1_xseg_MASK2_2;
uint16_t t1_xseg_FLIPLEFT;
uint16_t t1_xseg_FLIPLEFT2;
uint16_t t1_xseg_FLIPRIGHT;
uint16_t t1_xseg_LASTSEG;
uint16_t t1_xseg_STAGE1_1;
uint16_t t1_xseg_STAGE1_2;
uint16_t t1_xseg_STAGE1_3;
uint16_t t1_xseg_STAGE1_4;
uint16_t t1_xseg_MASK1_3;
uint16_t t1_xseg_MASK2_1;
uint16_t t1_xseg_MASK2_3;
uint16_t t1_xseg_FLIPGFXSEG;
uint16_t t1_xseg_SPRINGSEG;
uint16_t t1_xseg_CODE2;
extern const PbfInitRange t1_init_ranges[]; extern const PbfKeep t1_keep_prg[]; extern const PbfSegFix t1_seg_fixups[];
extern const PbfDispSeg t1_disp_segs[];
const PbfSegVar t1_segvars[] = {
	{ "STACKEN", &t1_xseg_STACKEN, 0xFFFF },
	{ "CODE", &t1_xseg_CODE, 0x10 },
	{ "DATAFLIP", &t1_xseg_DATAFLIP, 0xC0F },
	{ "DATA", &t1_xseg_DATA, 0x19B5 },
	{ "DATA2", &t1_xseg_DATA2, 0x2056 },
	{ "HIDDEN1", &t1_xseg_HIDDEN1, 0x2F94 },
	{ "HIDDEN2", &t1_xseg_HIDDEN2, 0x358C },
	{ "MASK1_2", &t1_xseg_MASK1_2, 0x3B74 },
	{ "MASK1_1", &t1_xseg_MASK1_1, 0x4114 },
	{ "MASK2_2", &t1_xseg_MASK2_2, 0x46B4 },
	{ "FLIPLEFT", &t1_xseg_FLIPLEFT, 0x4C54 },
	{ "FLIPLEFT2", &t1_xseg_FLIPLEFT2, 0x4EB6 },
	{ "FLIPRIGHT", &t1_xseg_FLIPRIGHT, 0x4FC2 },
	{ "LASTSEG", &t1_xseg_LASTSEG, 0x5224 },
	{ "STAGE1_1", &t1_xseg_STAGE1_1, 0x5224 },
	{ "STAGE1_2", &t1_xseg_STAGE1_2, 0x5947 },
	{ "STAGE1_3", &t1_xseg_STAGE1_3, 0x617B },
	{ "STAGE1_4", &t1_xseg_STAGE1_4, 0x6A9C },
	{ "MASK1_3", &t1_xseg_MASK1_3, 0x7194 },
	{ "MASK2_1", &t1_xseg_MASK2_1, 0x7734 },
	{ "MASK2_3", &t1_xseg_MASK2_3, 0x7CD4 },
	{ "FLIPGFXSEG", &t1_xseg_FLIPGFXSEG, 0x8274 },
	{ "SPRINGSEG", &t1_xseg_SPRINGSEG, 0x82E2 },
	{ "CODE2", &t1_xseg_CODE2, 0xAC7 },
	{ 0, 0, 0 }
};
const PbfProgram t1_program = { t1_init_ranges, t1_keep_prg, t1_seg_fixups, t1_disp_segs, t1_segvars, t1_f_FANTASIES, "t1_", NULL, 0x0 };
