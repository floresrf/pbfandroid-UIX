/* generated: segment variables and program descriptor */
#include "t2_syms.h"
#include "pbfrt.h"

uint16_t t2_xseg_STACKEN;
uint16_t t2_xseg_CODE;
uint16_t t2_xseg_DATAFLIP;
uint16_t t2_xseg_DATA;
uint16_t t2_xseg_DATA2;
uint16_t t2_xseg_HIDDEN1;
uint16_t t2_xseg_HIDDEN2;
uint16_t t2_xseg_MASK1_2;
uint16_t t2_xseg_MASK1_1;
uint16_t t2_xseg_MASK2_2;
uint16_t t2_xseg_FLIPLEFT;
uint16_t t2_xseg_FLIPLEFT2;
uint16_t t2_xseg_FLIPRIGHT;
uint16_t t2_xseg_LASTSEG;
uint16_t t2_xseg_STAGE1_1;
uint16_t t2_xseg_STAGE1_2;
uint16_t t2_xseg_STAGE1_3;
uint16_t t2_xseg_STAGE1_4;
uint16_t t2_xseg_MASK1_3;
uint16_t t2_xseg_MASK2_1;
uint16_t t2_xseg_MASK2_3;
uint16_t t2_xseg_FLIPGFXSEG;
uint16_t t2_xseg_SPRINGSEG;
uint16_t t2_xseg_CODE2;
extern const PbfInitRange t2_init_ranges[]; extern const PbfKeep t2_keep_prg[]; extern const PbfSegFix t2_seg_fixups[];
extern const PbfDispSeg t2_disp_segs[];
const PbfSegVar t2_segvars[] = {
	{ "STACKEN", &t2_xseg_STACKEN, 0xFFFF },
	{ "CODE", &t2_xseg_CODE, 0x10 },
	{ "DATAFLIP", &t2_xseg_DATAFLIP, 0xB8E },
	{ "DATA", &t2_xseg_DATA, 0x18CF },
	{ "DATA2", &t2_xseg_DATA2, 0x1F6F },
	{ "HIDDEN1", &t2_xseg_HIDDEN1, 0x2CD8 },
	{ "HIDDEN2", &t2_xseg_HIDDEN2, 0x32D0 },
	{ "MASK1_2", &t2_xseg_MASK1_2, 0x38B8 },
	{ "MASK1_1", &t2_xseg_MASK1_1, 0x3E58 },
	{ "MASK2_2", &t2_xseg_MASK2_2, 0x43F8 },
	{ "FLIPLEFT", &t2_xseg_FLIPLEFT, 0x4998 },
	{ "FLIPLEFT2", &t2_xseg_FLIPLEFT2, 0x4BFA },
	{ "FLIPRIGHT", &t2_xseg_FLIPRIGHT, 0x4DF2 },
	{ "LASTSEG", &t2_xseg_LASTSEG, 0x5054 },
	{ "STAGE1_1", &t2_xseg_STAGE1_1, 0x5054 },
	{ "STAGE1_2", &t2_xseg_STAGE1_2, 0x5820 },
	{ "STAGE1_3", &t2_xseg_STAGE1_3, 0x5FE4 },
	{ "STAGE1_4", &t2_xseg_STAGE1_4, 0x6791 },
	{ "MASK1_3", &t2_xseg_MASK1_3, 0x6D5D },
	{ "MASK2_1", &t2_xseg_MASK2_1, 0x72FD },
	{ "MASK2_3", &t2_xseg_MASK2_3, 0x789D },
	{ "FLIPGFXSEG", &t2_xseg_FLIPGFXSEG, 0x7E3D },
	{ "SPRINGSEG", &t2_xseg_SPRINGSEG, 0x7E48 },
	{ "CODE2", &t2_xseg_CODE2, 0xA46 },
	{ 0, 0, 0 }
};
const PbfProgram t2_program = { t2_init_ranges, t2_keep_prg, t2_seg_fixups, t2_disp_segs, t2_segvars, t2_f_FANTASIES, "t2_", NULL, 0x0, 0 };
