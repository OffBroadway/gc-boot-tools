#include "types.h"
#include "libc.h"

#include "../include/system.h"

// DSPCR bits
#define DSPCR_DSPRESET      0x0800        // Reset DSP
#define DSPCR_DSPDMA        0x0200        // ARAM dma in progress, if set
#define DSPCR_DSPINTMSK     0x0100        // * interrupt mask   (RW)
#define DSPCR_DSPINT        0x0080        // * interrupt active (RWC)
#define DSPCR_ARINTMSK      0x0040
#define DSPCR_ARINT         0x0020
#define DSPCR_AIINTMSK      0x0010
#define DSPCR_AIINT         0x0008
#define DSPCR_HALT          0x0004        // halt DSP
#define DSPCR_PIINT         0x0002        // assert DSP PI interrupt
#define DSPCR_RES           0x0001        // reset DSP

#define _SHIFTL(v, s, w)	\
    ((u32) (((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)	\
    ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))

static vu16* const _dspReg = (u16*)0xCC005000;

static __inline__ void __ARClearInterrupt()
{
	u16 cause;

	cause = _dspReg[5]&~(DSPCR_DSPINT|DSPCR_AIINT);
	_dspReg[5] = (cause|DSPCR_ARINT);
}

static __inline__ void __ARWaitDma()
{
	while(_dspReg[5]&DSPCR_DSPDMA);
}

static void __ARWriteDMA(u32 memaddr,u32 aramaddr,u32 len)
{
	// set main memory address
	_dspReg[16] = (_dspReg[16]&~0x03ff)|_SHIFTR(memaddr,16,16);
	_dspReg[17] = (_dspReg[17]&~0xffe0)|_SHIFTR(memaddr, 0,16);

	// set aram address
	_dspReg[18] = (_dspReg[18]&~0x03ff)|_SHIFTR(aramaddr,16,16);
	_dspReg[19] = (_dspReg[19]&~0xffe0)|_SHIFTR(aramaddr, 0,16);

	// set cntrl bits
	_dspReg[20] = (_dspReg[20]&~0x8000);
	_dspReg[20] = (_dspReg[20]&~0x03ff)|_SHIFTR(len,16,16);
	_dspReg[21] = (_dspReg[21]&~0xffe0)|_SHIFTR(len, 0,16);

	__ARWaitDma();
	__ARClearInterrupt();
}

void save_ipl() {
	const u32 aram_offset = 14 * 1024 * 1024;
	const u32 ipl_size = 2 * 1024 * 1024;
	const u32 ipl_start_addr = 0x81300000;
	const u32 ipl_end_addr = ipl_start_addr + ipl_size;
	flush_dcache_range((void*)ipl_start_addr, (void*)ipl_end_addr);
	__ARWriteDMA(ipl_start_addr, aram_offset, ipl_size);
	// _memcpy((void*)0x81600000, (void*)0x81300000, ipl_size);
}
