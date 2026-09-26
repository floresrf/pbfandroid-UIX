/* generated: segment variables and program descriptor */
#include "i1_syms.h"
#include "pbfrt.h"

uint16_t i1_xseg_STACKEN;
uint16_t i1_xseg_DATA;
uint16_t i1_xseg_FONTSEG;
uint16_t i1_xseg_FONTSEG2;
uint16_t i1_xseg_INTPIC2SEG;
uint16_t i1_xseg_PRESENTSEG;
uint16_t i1_xseg_FL1SEG;
uint16_t i1_xseg_FL2SEG;
uint16_t i1_xseg_TABLE0;
uint16_t i1_xseg_TABLE1;
uint16_t i1_xseg_TABLE2;
uint16_t i1_xseg_TABLE3;
uint16_t i1_xseg_TABLE4;
uint16_t i1_xseg_HSL1SEG;
uint16_t i1_xseg_HSL2SEG;
uint16_t i1_xseg_CODE;
uint16_t i1_xseg_DUMPNINGSPLATS;
uint16_t i1_xseg_T21ST1SEG;
uint16_t i1_xseg_T21ST2SEG;
uint16_t i1_xseg_VIKING1SEG;
uint16_t i1_xseg_VIKING2SEG;
uint16_t i1_xseg_QSUNE;
extern const PbfInitRange i1_init_ranges[]; extern const PbfKeep i1_keep_prg[]; extern const PbfSegFix i1_seg_fixups[];
extern const PbfDispSeg i1_disp_segs[];
const PbfSegVar i1_segvars[] = {
	{ "STACKEN", &i1_xseg_STACKEN, 0xFFFF },
	{ "DATA", &i1_xseg_DATA, 0x80 },
	{ "FONTSEG", &i1_xseg_FONTSEG, 0x64B },
	{ "FONTSEG2", &i1_xseg_FONTSEG2, 0x844 },
	{ "INTPIC2SEG", &i1_xseg_INTPIC2SEG, 0x9D9 },
	{ "PRESENTSEG", &i1_xseg_PRESENTSEG, 0x107A },
	{ "FL1SEG", &i1_xseg_FL1SEG, 0x1196 },
	{ "FL2SEG", &i1_xseg_FL2SEG, 0x1786 },
	{ "TABLE0", &i1_xseg_TABLE0, 0x1C45 },
	{ "TABLE1", &i1_xseg_TABLE1, 0x1FD7 },
	{ "TABLE2", &i1_xseg_TABLE2, 0x2439 },
	{ "TABLE3", &i1_xseg_TABLE3, 0x28D5 },
	{ "TABLE4", &i1_xseg_TABLE4, 0x2D68 },
	{ "HSL1SEG", &i1_xseg_HSL1SEG, 0x32D5 },
	{ "HSL2SEG", &i1_xseg_HSL2SEG, 0x346D },
	{ "CODE", &i1_xseg_CODE, 0x3620 },
	{ "DUMPNINGSPLATS", &i1_xseg_DUMPNINGSPLATS, 0x3B19 },
	{ "T21ST1SEG", &i1_xseg_T21ST1SEG, 0x3B19 },
	{ "T21ST2SEG", &i1_xseg_T21ST2SEG, 0x425D },
	{ "VIKING1SEG", &i1_xseg_VIKING1SEG, 0x462B },
	{ "VIKING2SEG", &i1_xseg_VIKING2SEG, 0x4D42 },
	{ "QSUNE", &i1_xseg_QSUNE, 0xFFFF },
	{ 0, 0, 0 }
};
static const PbfExtCopy i1_extcopy[] = {
	{ "DATA", 22832, 24000, 896 },
	{ 0, 0, 0, 0 }
};
const PbfProgram i1_program = { i1_init_ranges, i1_keep_prg, i1_seg_fixups, i1_disp_segs, i1_segvars, i1_f_START, "i1_", i1_extcopy, 0x5A00, 1553 };
