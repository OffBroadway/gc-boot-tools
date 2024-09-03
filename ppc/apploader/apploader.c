/**
 * apploader.c
 *
 * Simple apploader enabling booting DOLs from "El Torito" iso9660 discs.
 * This program is part of the cubeboot-tools package.
 *
 * Copyright (C) 2005-2006 The GameCube Linux Team
 * Copyright (C) 2005,2006 Albert Herranz
 * Copyright (C) 2020-2021 Extrems
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */

// #define PATCH_IPL 1

#include <stddef.h>
#include <string.h>

#include "../include/system.h"

#include "../../include/gcm.h"
#include "../../include/dol.h"

#include "libc.h"
#include "usbgecko.h"

#ifndef ATTRIBUTE_ALIGN
# define ATTRIBUTE_ALIGN(v)				__attribute__((aligned(v)))
#endif
#ifndef ATTRIBUTE_PACKED
# define ATTRIBUTE_PACKED				__attribute__((packed))
#endif

typedef uint16_t u16;				///< 16bit unsigned integer
typedef volatile u16 vu16;			///< 16bit unsigned volatile integer

#define DI_ALIGN_SHIFT	5
#define DI_ALIGN_SIZE	(1UL << DI_ALIGN_SHIFT)
#define DI_ALIGN_MASK	(~((1 << DI_ALIGN_SHIFT) - 1))

#define di_align(addr)	(void *) \
			((((unsigned long)(addr)) + \
				 DI_ALIGN_SIZE - 1) & DI_ALIGN_MASK)

/*
 * DVD data structures
 */

struct dolphin_debugger_info {
	uint32_t		present;
	uint32_t		exception_mask;
	uint32_t		exception_hook_address;
	uint32_t		saved_lr;
	unsigned char		__pad1[0x10];
} __attribute__ ((__packed__));

struct dolphin_lowmem {
	struct gcm_disk_info	b_disk_info;

	uint32_t		a_boot_magic;
	uint32_t		a_version;

	uint32_t		b_physical_memory_size;
	uint32_t		b_console_type;

	uint32_t		a_arena_lo;
	uint32_t		a_arena_hi;
	void			*a_fst;
	uint32_t		a_fst_max_size;

	struct dolphin_debugger_info a_debugger_info;
	unsigned char		hook_code[0x60];

	uint32_t		o_current_os_context_phys;
	uint32_t		o_previous_os_interrupt_mask;
	uint32_t		o_current_os_interrupt_mask;

	uint32_t		tv_mode;
	uint32_t		b_aram_size;

	void			*o_current_os_context;
	void			*o_default_os_thread;
	void			*o_thread_queue_head;
	void			*o_thread_queue_tail;
	void			*o_current_os_thread;

	uint32_t		a_debug_monitor_size;
	void			*a_debug_monitor;

	uint32_t		a_simulated_memory_size;

	void			*a_bi2;

	uint32_t		b_bus_clock_speed;
	uint32_t		b_cpu_clock_speed;
} __attribute__ ((__packed__));

/*
 *
 */

static void al_enter(void (*report) (const char *text, ...));
static int al_load(void **address, uint32_t * length, uint32_t * offset);
static void *al_exit(void);

/*
 * 
 */
struct apploader_control {
	unsigned 	step;
	unsigned long	fst_address;
	uint32_t	fst_offset;
	uint32_t	fst_size;
	unsigned long	bi2_address;
	void		(*report) (const char *text, ...);
};

struct bootloader_control {
	void *entry_point;
	uint32_t offset;
	uint32_t size;
	uint32_t sects_bitmap;
	uint32_t all_sects_bitmap;
};


static struct dolphin_lowmem *lowmem = (struct dolphin_lowmem *)0x80000000;

static struct apploader_control al_control = { .fst_size = ~0 };
static struct bootloader_control bl_control = { .size = ~0 };

static unsigned char di_buffer[DI_SECTOR_SIZE] __attribute__ ((aligned(32))) = "sillyplaceholder";

#if PATCH_IPL
static void patch_ipl(void);
static void skip_ipl_animation(void);
#endif

static u32 *signal_word = (void*)0x81700000;

extern void save_ipl(void);
extern void done_func(void);
extern void run_code(void);
extern void run(register void* entry_point);
extern void run_interrupt(register void *entry_point);

#define NEW_PPC_INSTR() 0

#define PPC_OPCODE_B           18

// fields
#define PPC_OPCODE_MASK  0x3F
#define PPC_OPCODE_SHIFT 26
#define PPC_GET_OPCODE(instr)       ((instr           >> PPC_OPCODE_SHIFT) & PPC_OPCODE_MASK)
#define PPC_SET_OPCODE(instr,opcode) (instr |= (opcode & PPC_OPCODE_MASK) << PPC_OPCODE_SHIFT)

#define PPC_LI_MASK      0xFFFFFF
#define PPC_LI_SHIFT     2
#define PPC_GET_LI(instr)           ((instr       >> PPC_LI_SHIFT) & PPC_LI_MASK)
#define PPC_SET_LI(instr,li)         (instr |= (li & PPC_LI_MASK) << PPC_LI_SHIFT)

#define PPC_AA_MASK      0x1
#define PPC_AA_SHIFT     1
#define PPC_GET_AA(instr)           ((instr       >> PPC_AA_SHIFT) & PPC_AA_MASK)
#define PPC_SET_AA(instr,aa)         (instr |= (aa & PPC_AA_MASK) << PPC_AA_SHIFT)

#define PPC_LK_MASK      0x1
#define PPC_LK_SHIFT     0
#define PPC_GET_LK(instr)           ((instr       >> PPC_LK_SHIFT) & PPC_LK_MASK)
#define PPC_SET_LK(instr,lk)         (instr |= (lk & PPC_LK_MASK) << PPC_LK_SHIFT)

#define GEN_B(ppc,dst,aa,lk) \
	{ ppc = NEW_PPC_INSTR(); \
	  PPC_SET_OPCODE(ppc, PPC_OPCODE_B); \
	  PPC_SET_LI    (ppc, (dst)); \
	  PPC_SET_AA    (ppc, (aa)); \
	  PPC_SET_LK    (ppc, (lk)); }

/*
 * This is our particular "main".
 * It _must_ be the first function in this file.
 */
void al_start(void **enter, void **load, void **exit)
{
	al_control.step = 0;
	gprintf("al_start\n");
	if (*signal_word != 0xfeedface) {
		gprintf("INCEPTION\n");
		*signal_word = 0xfeedface;
		{
			// allow any region
			uint32_t *address = (uint32_t *)0x81300d50;
			*address = 0x38600001; // li r3, 1
			flush_dcache_range(address, address+1);
			invalidate_icache_range(address, address+1);
		}
		{
			// force tick main thread
			uint32_t *address = (uint32_t *)0x81300654;
			*address = 0x60000000; // nop
			flush_dcache_range(address, address+1);
			invalidate_icache_range(address, address+1);
		}

		save_ipl();

		// *(u32*)0x8130bb6c = 0x38000000; // li r0, 2 (force menu)
		// {
		// 	u32 *target = (void*)0x8130bba8;
		// 	GEN_B(*target, ((u32*)done_func - target), 0, 1);
		// }
		// {
		// 	u32 *target = (void*)0x813010f0;
		// 	GEN_B(*target, ((u32*)run_code - target), 0, 1);
		// 	// TODO cache
		// }

		run_interrupt((void*)run_code);

		// void (*start_prog)() = (void*)0x813021b4;
		// start_prog();
		// __builtin_unreachable();
	}

	*enter = al_enter;
	*load = al_load;
	*exit = al_exit;

#ifdef DEBUG
	gprintf("Early code exec\n");
	// while(1);
#endif

#if PATCH_IPL

	patch_ipl();
#endif
}

// void _memset(void* s, int c, int count) {
// 	char* xs = (char*)s;
// 	while (count--)
// 		*xs++ = c;
// }

void run_code() {
	// {
	// 	// skip threaded sleep
	// 	uint32_t *address = (uint32_t *)0x81372e64;
	// 	*address = 0x60000000; // nop
	// 	flush_dcache_range(address, address+1);
	// 	invalidate_icache_range(address, address+1);
	// }
	
	// void (*GXDrawDone)() = (void*)0x81372e10;
	void (*GXFlush)() = (void*)0x81372ce8;
	void (*GXAbortFrame)() = (void*)0x81372d44;

	*(u32*)0x8137ec6c = 0x6b80;
	*(u32*)0x8137ec70 = 0xb580;
	*(u32*)0x8137ec74 = 0xffe0;
	*(u32*)0x8137ecc0 = 0x18760;
	*(u32*)0x8137ecc4 = 0x1c900;
	*(u32*)0x8137ed1c = 0x230e0;
	*(u32*)0x8137ed20 = 0x23220;
	*(u32*)0x8137ed24 = 0x2e080;
	*(u32*)0x8137ed34 = 0x35e40;
	*(u32*)0x8137ed38 = 0x37f00;
	*(u32*)0x8137ed3c = 0x3cc00;
	*(u32*)0x8137ed40 = 0x411c0;
	*(u32*)0x8137ed44 = 0x45080;
	*(u32*)0x8137ed48 = 0x45a60;

	// _memset((void*)0x80001800, 0, 0x1800);
	// _memset((void*)0x80003000, 0, 0x100);
	// *(u32*)0x800030d8 = 0x006f66c5;
	// *(u32*)0x800030dc = 0xe76aa800;

	// _memset((void*)0x80700020, 0, 0xa00000);
	// _memset((void*)0x8159d280, 0, 0x162d80);

	GXFlush();
	GXAbortFrame();
	run((void*)0x81300000);

	// void (*start_program)() = (void*)0x8130213c;
	// while(1) {
	// 	start_program();
	// }
}


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
	const u32 aram_offset = 10 * 1024 * 1024;
	const u32 ipl_size = 2 * 1024 * 1024;
	__ARWriteDMA(0x81300000, aram_offset, ipl_size);
}

void run(register void* entry_point) {
    asm("mfhid0	4");
    asm("ori 4, 4, 0x0800");
    asm("mthid0	4");
    // hwsync
    asm("sync");
    asm("isync");
    // boot
    asm("mtlr %0" : : "r" (entry_point));
    asm("blr");
}


void run_interrupt(register void *entry_point) {
    asm("mtsrr0 %0" : : "r" (entry_point));

    asm("li 4, 0x30");
	asm("mtsrr1 4");

    asm("rfi");
}

/*
 * Loads a bitmap mask with all non-void sections in a DOL file.
 */
static int al_load_dol_sects_bitmap(struct dol_header *h)
{
	int i, sects_bitmap;

	sects_bitmap = 0;
	for (i = 0; i < DOL_MAX_SECT; i++) {
		/* zero here means the section is not in use */
		if (dol_sect_size(h, i) == 0)
			continue;

		sects_bitmap |= (1 << i);
	}
	return sects_bitmap;
}

/*
 * Checks if the DOL we are trying to boot is appropiate enough.
 */
static void al_check_dol(struct dol_header *h, int dol_length)
{
	int i, valid = 0;
	uint32_t value;

	/* now perform some sanity checks */
	for (i = 0; i < DOL_MAX_SECT; i++) {
		/* DOL segment MAY NOT be physically stored in the header */
		if ((dol_sect_offset(h, i) != 0)
		    && (dol_sect_offset(h, i) < DOL_HEADER_SIZE)) {
			panic("detected segment offset within DOL header\n");
		}

		/* offsets must be aligned to 32 bytes */
		value = dol_sect_offset(h, i);
		if (value != (uint32_t) di_align(value)) {
			panic("detected unaligned section offset\n");
		}

		/* addresses must be aligned to 32 bytes */
		value = dol_sect_address(h, i);
		if (value != (uint32_t) di_align(value)) {
			panic("unaligned section address\n");
		}

		/* end of physical storage must be within file */
		if (dol_sect_offset(h, i) + dol_sect_size(h, i) > dol_length) {
			panic("segment past DOL file size\n");
		}

		if (dol_sect_address(h, i) != 0) {
			/* we only should accept DOLs with segments above 2GB */
			if (!(dol_sect_address(h, i) & 0x80000000)) {
				panic("segment below 2GB\n");
			}
			/* we only accept DOLs below 0x81200000 */
			if (dol_sect_address(h, i) > 0x81200000) {
				panic("segment above 0x81200000\n");
			}
		}

		if (i < DOL_SECT_MAX_TEXT) {
			/* remember that entrypoint was in a code segment */
			if (h->entry_point >= dol_sect_address(h, i)
			    && h->entry_point < dol_sect_address(h, i) +
			    dol_sect_size(h, i))
				valid = 1;
		}
	}

	/* if there is a BSS segment it must^H^H^H^Hshould be above 2GB, too */
	if (h->address_bss != 0 && !(h->address_bss & 0x80000000)) {
		panic("BSS segment below 2GB\n");
	}

	/* if entrypoint is not within a code segment reject this file */
	if (!valid) {
		panic("entry point out of text segment\n");
	}

	/* we've got a valid dol if we got here */
	return;
}

/*
 * Initializes the apploader related stuff.
 * Called by the IPL.
 */
static void al_enter(void (*report) (const char *text, ...))
{
	al_control.step = 1;
#ifdef DEBUG
	al_control.report = (void (*)(const char *, ...))gprintf;
#else
	al_control.report = report;
#endif
	if (al_control.report)
		al_control.report("New apploader\n");
}

/*
 * This is the apploader main processing function.
 * Called by the IPL.
 */
static int al_load(void **address, uint32_t *length, uint32_t *offset)
{
	struct gcm_disk_header *disk_header;
	struct gcm_disk_header_info *disk_header_info;

	struct dol_header *dh;
	unsigned long lowest_start;
	int j, k;

	int need_more = 1; /* this tells the IPL if we need more data or not */

	if (al_control.report)
		al_control.report("step %d\n", al_control.step);

	// while(1);

	// u32 caller0 = (u32)__builtin_return_address(0);
	// u32 caller1 = (u32)__builtin_return_address(1);
	// u32 caller2 = (u32)__builtin_return_address(2);
	// u32 caller3 = (u32)__builtin_return_address(3);
	// al_control.report("Callers = [%08x] [%08x] [%08x] [%08x]\n", caller0, caller1, caller2, caller3);

	switch (al_control.step) {
	case 0:
		/* should not get here if al_enter was called */
	case 1:
		al_control.step = 1; /* fix it to a known value */

		/* read sector 0, containing disk header and disk header info */
		*address = di_buffer;
		*length = (uint32_t) di_align(sizeof(*disk_header) + sizeof(*disk_header_info));
		*offset = 0;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 2:
		/* boot.bin and bi2.bin header loaded */

		disk_header = (struct gcm_disk_header *)di_buffer;
		disk_header_info = (struct gcm_disk_header_info *)(di_buffer + sizeof(*disk_header));

		bl_control.offset = disk_header->layout.dol_offset;
		al_control.report("Found dol, %08x\n", bl_control.offset);

		al_control.fst_offset = disk_header->layout.fst_offset;
		al_control.fst_size = disk_header->layout.fst_size;
		al_control.fst_address = (0x81800000 - al_control.fst_size) & DI_ALIGN_MASK;

		al_control.report("fst_offset = %08x\n", al_control.fst_offset);
		al_control.report("fst_size = %08x\n", al_control.fst_size);
		al_control.report("fst_address = %08x\n", al_control.fst_address);

		/* read fst.bin */
		*address = (void *)al_control.fst_address;
		*length = (uint32_t) di_align(al_control.fst_size);
		*offset = al_control.fst_offset;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 3:
		al_control.report("fst loaded\n");

		/* fst.bin loaded */
		// u32* fst_bin = (u32*)al_control.fst_address;
		// al_control.report("FST dump %08x, %08x, %08x, %08x\n", fst_bin[0], fst_bin[1], fst_bin[2], fst_bin[3]);

		al_control.bi2_address = al_control.fst_address - 0x2000;

		/* read bi2.bin */
		*address = (void *)al_control.bi2_address;
		*length = 0x2000;
		*offset = 0x440;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 4:
		/* bi2.bin loaded */

		al_control.report("Found dol, %08x\n", bl_control.offset);

		/* request the .dol header */
		*address = di_buffer;
		*length = DOL_HEADER_SIZE;
		*offset = bl_control.offset;
		invalidate_dcache_range(*address, *address + *length);

		bl_control.sects_bitmap = 0xdeadbeef;

		al_control.step++;
		while(1);
		break;
	case 5:
		/* .dol header loaded */

		dh = (struct dol_header *)di_buffer;

		/* extra work on first visit */
		if (bl_control.sects_bitmap == 0xdeadbeef) {
			/* sanity checks here */
			al_check_dol(dh, bl_control.size);

			/* save our entry point */
			bl_control.entry_point = (void *)dh->entry_point;

			/* pending and valid sections, respectively */
			bl_control.sects_bitmap = 0;
			bl_control.all_sects_bitmap = al_load_dol_sects_bitmap(dh);
		}

		/*
		 * Load the sections in ascending order.
		 * We need this because we are loading a bit more of data than
		 * strictly necessary on DOLs with unaligned lengths.
		 */

		/* find lowest start address for a pending section */
		lowest_start = 0xffffffff;
		for (j = -1, k = 0; k < DOL_MAX_SECT; k++) {
			/* continue if section is already done */
			if ((bl_control.sects_bitmap & (1 << k)) != 0)
				continue;
			/* do nothing for non sections */
			if (!(bl_control.all_sects_bitmap & (1 << k)))
				continue;
			/* found new candidate */
			if (dol_sect_address(dh, k) < lowest_start) {
				lowest_start = dol_sect_address(dh, k);
				j = k;
			}
		}
		/* mark section as being loaded */
		bl_control.sects_bitmap |= (1 << j);

		/* request a .dol section */
		*address = (void *)dol_sect_address(dh, j);;
		*length = (uint32_t) di_align(dol_sect_size(dh, j));
		*offset = bl_control.offset + dol_sect_offset(dh, j);

		invalidate_dcache_range(*address, *address + *length);
		if (dol_sect_is_text(dh, j))
			invalidate_icache_range(*address, *address + *length);

		/* check if we are going to be done with all sections */
		if (bl_control.sects_bitmap == bl_control.all_sects_bitmap) {
#ifdef DEBUG
			gprintf("BSS clear %08x len=%x\n", dh->address_bss, dh->size_bss);
#else
			al_control.report("BSS clear %08x len=%x\n", dh->address_bss, dh->size_bss);
#endif
			/* setup .bss section */
			if (dh->size_bss)
				_memset((void *)dh->address_bss, 0,
				       dh->size_bss);

			/* bye, bye */
			al_control.step++;
		}
		break;

	case 6:
		/* all .dol sections loaded */
		al_control.report("all sections loaded\n");

		lowmem->a_boot_magic = 0x0d15ea5e;
		lowmem->a_version = 1;

		lowmem->a_arena_hi = al_control.fst_address;
		lowmem->a_fst = (void *)al_control.fst_address;
		lowmem->a_fst_max_size = al_control.fst_size;
		//_memset(&lowmem->a_debugger_info, 0, sizeof(struct dolphin_debugger_info));
		//lowmem->a_debug_monitor_size = 0;
		lowmem->a_debug_monitor = (void *)0x81800000;
		lowmem->a_simulated_memory_size = 0x01800000;
		lowmem->a_bi2 = (void *)al_control.bi2_address;
		flush_dcache_range(lowmem, lowmem+1);

#if PATCH_IPL
		skip_ipl_animation();
#endif

		*length = 0;
		need_more = 0;
		al_control.step++;

		break;
	default:
		al_control.step++;
		break;
	}

	al_control.report("MADE IT TO END OF STEP\n");

	return need_more;
}

/*
 *
 */
static void *al_exit(void)
{
	return bl_control.entry_point;
}

__attribute__((used)) void done_func() {
	gprintf("FOUND done func\n");
	while(1);
}

#if PATCH_IPL

/*
 *
 */
enum ipl_revision {
	IPL_UNKNOWN,
	IPL_NTSC_10_001,
	IPL_NTSC_10_002,
	IPL_DEV_10,
	IPL_NTSC_11_001,
	IPL_PAL_10_001,
	IPL_PAL_10_002,
	IPL_MPAL_11,
	IPL_TDEV_11,
	IPL_NTSC_12_001,
	IPL_NTSC_12_101,
	IPL_PAL_12_101
};

static enum ipl_revision get_ipl_revision(void)
{
	register uint32_t sdata2 asm ("r2");
	register uint32_t sdata asm ("r13");

	if (sdata2 == 0x81465cc0 && sdata == 0x81465320)
		return IPL_NTSC_10_001;
	if (sdata2 == 0x81468fc0 && sdata == 0x814685c0)
		return IPL_NTSC_10_002;
	if (sdata2 == 0x814695e0 && sdata == 0x81468bc0)
		return IPL_DEV_10;
	if (sdata2 == 0x81489c80 && sdata == 0x81489120)
		return IPL_NTSC_11_001;
	if (sdata2 == 0x814b5b20 && sdata == 0x814b4fc0)
		return IPL_PAL_10_001;
	if (sdata2 == 0x814b4fc0 && sdata == 0x814b4400)
		return IPL_PAL_10_002;
	if (sdata2 == 0x81484940 && sdata == 0x81483de0)
		return IPL_MPAL_11;
	if (sdata2 == 0x8148fbe0 && sdata == 0x8148ef80)
		return IPL_TDEV_11;
	if (sdata2 == 0x8148a660 && sdata == 0x8148b1c0)
		return IPL_NTSC_12_001;
	if (sdata2 == 0x8148aae0 && sdata == 0x8148b640)
		return IPL_NTSC_12_101;
	if (sdata2 == 0x814b66e0 && sdata == 0x814b7280)
		return IPL_PAL_12_101;

	return IPL_UNKNOWN;
}

#define PPC_NOP 			0x60000000
#define PPC_BLR 			0x4e800020
#define PPC_NULL 			0x00000000

static void patch_ipl_anim(uint32_t address_sound_level, uint32_t address_draw_cubes, uint32_t address_draw_outer, uint32_t address_draw_inner) {
	uint32_t *address;

	// disable sound (u16 + u8[2] padding)
	address = (uint32_t *)address_sound_level;
	*address = PPC_NULL;
	flush_dcache_range(address, address+1);
	invalidate_icache_range(address, address+1);

	// disable cubes
	address = (uint32_t *)address_draw_cubes;
	*address = PPC_NOP;
	flush_dcache_range(address, address+1);
	invalidate_icache_range(address, address+1);

	// disable outer
	address = (uint32_t *)address_draw_outer;
	*address = PPC_NOP;
	flush_dcache_range(address, address+1);
	invalidate_icache_range(address, address+1);

	// disable inner
	address = (uint32_t *)address_draw_inner;
	*address = PPC_NOP;
	flush_dcache_range(address, address+1);
	invalidate_icache_range(address, address+1);
}

/*
 *
 */
static void patch_ipl(void)
{
	uint32_t *start, *end;
	uint32_t *address;

	uint32_t sound_level;
	uint32_t draw_cubes;
	uint32_t draw_outer;
	uint32_t draw_inner;

	// hide anim and disable sound
	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		sound_level = 0x8145d4d0;
		draw_cubes = 0x8131055c;
		draw_outer = 0x8130d224;
		draw_inner = 0x81310598;
		patch_ipl_anim(sound_level, draw_cubes, draw_outer, draw_inner);
		break;
	case IPL_NTSC_11_001:
		sound_level = 0x81481278;
		draw_cubes = 0x81310754;
		draw_outer = 0x8130d428;
		draw_inner = 0x81310790;
		patch_ipl_anim(sound_level, draw_cubes, draw_outer, draw_inner);
		break;
	case IPL_NTSC_12_001:
		sound_level = 0x81483340;
		draw_cubes = 0x81310aec;
		draw_outer = 0x8130d79c;
		draw_inner = 0x81310b28;
		patch_ipl_anim(sound_level, draw_cubes, draw_outer, draw_inner);
		break;
	case IPL_NTSC_12_101:
		sound_level = 0x814837c0;
		draw_cubes = 0x81310b04;
		draw_outer = 0x8130d7b4;
		draw_inner = 0x81310b40;
		patch_ipl_anim(sound_level, draw_cubes, draw_outer, draw_inner);
		break;
	case IPL_PAL_10_001:
		sound_level = 0x814ad118;
		draw_cubes = 0x81310e94;
		draw_outer = 0x8130d868;
		draw_inner = 0x81310ed0;
		patch_ipl_anim(sound_level, draw_cubes, draw_outer, draw_inner);
		break;
	case IPL_MPAL_11:
		sound_level = 0x8147bf38;
		draw_cubes = 0x81310680;
		draw_outer = 0x8130d354;
		draw_inner = 0x813106bc;
		patch_ipl_anim(sound_level, draw_cubes, draw_outer, draw_inner);
		break;
	case IPL_PAL_12_101:
		sound_level = 0x814af400;
		draw_cubes = 0x81310fd4;
		draw_outer = 0x8130d9a8;
		draw_inner = 0x81311010;
		patch_ipl_anim(sound_level, draw_cubes, draw_outer, draw_inner);
		break;
	default:
		break;
	}

	// disable region detection
	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		start = (uint32_t *)0x81300a70;
		end = (uint32_t *)0x813010b0;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x81300e88;
			if (*address == 0x38000000)
				*address |= 1;

			address = (uint32_t *)0x81300ea0;
			if (*address == 0x38000000)
				*address |= 1;

			address = (uint32_t *)0x81300ea8;
			if (*address == 0x38000000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	case IPL_NTSC_10_002:
		start = (uint32_t *)0x813008d8;
		end = (uint32_t *)0x8130096c;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x8130092c;
			if (*address == 0x38600000)
				*address |= 1;

			address = (uint32_t *)0x81300944;
			if (*address == 0x38600000)
				*address |= 1;

			address = (uint32_t *)0x8130094c;
			if (*address == 0x38600000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	case IPL_DEV_10:
		start = (uint32_t *)0x81300dfc;
		end = (uint32_t *)0x81301424;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x8130121c;
			if (*address == 0x38000000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	case IPL_NTSC_11_001:
	case IPL_PAL_10_001:
	case IPL_MPAL_11:
		start = (uint32_t *)0x813006e8;
		end = (uint32_t *)0x813007b8;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x8130077c;
			if (*address == 0x38600000)
				*address |= 1;

			address = (uint32_t *)0x813007a0;
			if (*address == 0x38600000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	case IPL_PAL_10_002:
		start = (uint32_t *)0x8130092c;
		end = (uint32_t *)0x81300a10;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x813009d4;
			if (*address == 0x38600000)
				*address |= 1;

			address = (uint32_t *)0x813009f8;
			if (*address == 0x38600000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	case IPL_TDEV_11:
		start = (uint32_t *)0x81300b58;
		end = (uint32_t *)0x81300c3c;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x81300c00;
			if (*address == 0x38600000)
				*address |= 1;

			address = (uint32_t *)0x81300c24;
			if (*address == 0x38600000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	case IPL_NTSC_12_001:
	case IPL_NTSC_12_101:
		start = (uint32_t *)0x81300a24;
		end = (uint32_t *)0x81300b08;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x81300acc;
			if (*address == 0x38600000)
				*address |= 1;

			address = (uint32_t *)0x81300af0;
			if (*address == 0x38600000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	case IPL_PAL_12_101:
		start = (uint32_t *)0x813007d8;
		end = (uint32_t *)0x813008bc;
		if (start[0] == 0x7c0802a6 && end[-1] == 0x4e800020) {
			address = (uint32_t *)0x81300880;
			if (*address == 0x38600000)
				*address |= 1;

			address = (uint32_t *)0x813008a4;
			if (*address == 0x38600000)
				*address |= 1;

			flush_dcache_range(start, end);
			invalidate_icache_range(start, end);
		}
		break;
	default:
		break;
	}
}

/*
 *
 */
static void skip_ipl_animation(void)
{
	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		if (*(uint32_t *)0x8145d6d0 == 1
			&& !(*(uint16_t *)0x8145f14c & 0x0100)
			&& *(uint32_t *)0x8145d6f0 == 0x81465728)
			*(uint8_t *)0x81465747 = 1;
		break;
	case IPL_NTSC_10_002:
		if (*(uint32_t *)0x814609c0 == 1
			&& !(*(uint16_t *)0x814624ec & 0x0100)
			&& *(uint32_t *)0x814609e0 == 0x81468ac8)
			*(uint8_t *)0x81468ae7 = 1;
		break;
	case IPL_DEV_10:
		if (*(uint32_t *)0x81460fe0 == 1
			&& !(*(uint16_t *)0x81462b0c & 0x0100)
			&& *(uint32_t *)0x81461000 == 0x814690e8)
			*(uint8_t *)0x81469107 = 1;
		break;
	case IPL_NTSC_11_001:
		if (*(uint32_t *)0x81481518 == 1
			&& !(*(uint16_t *)0x8148370c & 0x0100)
			&& *(uint32_t *)0x81481538 == 0x81489e58)
			*(uint8_t *)0x81489e77 = 1;
		break;
	case IPL_PAL_10_001:
		if (*(uint32_t *)0x814ad3b8 == 1
			&& !(*(uint16_t *)0x814af60c & 0x0100)
			&& *(uint32_t *)0x814ad3d8 == 0x814b5d58)
			*(uint8_t *)0x814b5d77 = 1;
		break;
	case IPL_PAL_10_002:
		if (*(uint32_t *)0x814ac828 == 1
			&& !(*(uint16_t *)0x814aeb2c & 0x0100)
			&& *(uint32_t *)0x814ac848 == 0x814b5278)
			*(uint8_t *)0x814b5297 = 1;
		break;
	case IPL_MPAL_11:
		if (*(uint32_t *)0x8147c1d8 == 1
			&& !(*(uint16_t *)0x8147e3cc & 0x0100)
			&& *(uint32_t *)0x8147c1f8 == 0x81484b18)
			*(uint8_t *)0x81484b37 = 1;
		break;
	case IPL_TDEV_11:
		if (*(uint32_t *)0x81487438 == 1
			&& !(*(uint16_t *)0x8148972c & 0x0100)
			&& *(uint32_t *)0x81487458 == 0x8148fe78)
			*(uint8_t *)0x8148fe97 = 1;
		break;
	case IPL_NTSC_12_001:
		if (*(uint32_t *)0x814835f0 == 1
			&& !(*(uint16_t *)0x81484cec & 0x0100)
			&& *(uint32_t *)0x81483610 == 0x8148b438)
			*(uint8_t *)0x8148b457 = 1;
		break;
	case IPL_NTSC_12_101:
		if (*(uint32_t *)0x81483a70 == 1
			&& !(*(uint16_t *)0x8148518c & 0x0100)
			&& *(uint32_t *)0x81483a90 == 0x8148b8d8)
			*(uint8_t *)0x8148b8f7 = 1;
		break;
	case IPL_PAL_12_101:
		if (*(uint32_t *)0x814af6b0 == 1
			&& !(*(uint16_t *)0x814b0dcc & 0x0100)
			&& *(uint32_t *)0x814af6d0 == 0x814b7518)
			*(uint8_t *)0x814b7537 = 1;
		break;
	default:
		break;
	}
}

#endif
