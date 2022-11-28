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

#include <stddef.h>
#include <string.h>

#include "../include/system.h"

#include "../../include/gcm.h"
#include "../../include/dol.h"

#include "usbgecko.h"

#define DI_ALIGN_SHIFT	5
#define DI_ALIGN_SIZE	(1UL << DI_ALIGN_SHIFT)
#define DI_ALIGN_MASK	(~((1 << DI_ALIGN_SHIFT) - 1))

#define di_align(addr)	(void *) \
			((((unsigned long)(addr)) + \
				 DI_ALIGN_SIZE - 1) & DI_ALIGN_MASK)

/*
 * DVD data structures
 */

struct di_boot_record {
	uint8_t zero;
	uint8_t standard_id[5];	/* "CD001" */
	uint8_t version;	/* 1 */
	uint8_t boot_system_id[32];	/* "EL TORITO SPECIFICATION" */
	uint8_t boot_id[32];
	uint32_t boot_catalog_offset;	/* in media sectors */
	uint8_t align_1[21];
} __attribute__ ((__packed__));

struct di_validation_entry {
	uint8_t header_id;	/* 1 */
	uint8_t platform_id;	/* 0=80x86,1=PowerPC,2=Mac */
	uint16_t reserved;
	uint8_t id_string[24];
	uint16_t checksum;
	uint8_t key_55;		/* 55 */
	uint8_t key_AA;		/* AA */
} __attribute__ ((__packed__));

struct di_default_entry {
	uint8_t boot_indicator;	/* 0x88=bootable */
	uint8_t boot_media_type;	/* 0=no emulation */
	uint16_t load_segment;	/* multiply by 10 to get actual address */
	uint8_t system_type;
	uint8_t unused_1;
	uint16_t sector_count;	/* emulated sectors to load at segment */
	uint32_t load_rba;	/* in media sectors */
	uint8_t unused_2[20];
} __attribute__ ((__packed__));

/*
 *
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

static void al_enter(void (*report) (char *text, ...));
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
	void		(*report) (char *text, ...);
};

struct bootloader_control {
	void *entry_point;
	uint32_t offset;
	uint32_t size;
	uint32_t sects_bitmap;
	uint32_t all_sects_bitmap;
};


static struct dolphin_lowmem *lowmem =
			 (struct dolphin_lowmem *)0x80000000;

static struct apploader_control al_control = { .fst_size = ~0 };
static struct bootloader_control bl_control = { .size = ~0 };

static unsigned char di_buffer[DI_SECTOR_SIZE] __attribute__ ((aligned(32))) =
	"www.gc-linux.org";

int DVD_LowRead64(void* dst, uint32_t len, uint64_t offset);

/*
 * This is our particular "main".
 * It _must_ be the first function in this file.
 */
void al_start(void **enter, void **load, void **exit) {
	al_enter(NULL);

	while(1) {
		void *dst = NULL;
		s32 len = 0, offset = 0;

		// run apploader main function
		s32 ret = al_load(&dst, &len, &offset);
		if (!ret) {
			gprintf("DONE\n");
			break;
		}

		// read data from DVD
		DVD_LowRead64(dst, len, offset); // (s64)(offset << 2)
	}

	// set entry point from apploader
	void (*entry)() = (void (*)())al_exit();

	// run the program
	entry();
	__builtin_unreachable();
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
 * Checks if the validation entry of the boot catalog is valid.
 */
static void al_check_validation_entry(struct di_validation_entry *ve)
{
	if (ve->header_id != 1 || ve->key_55 != 0x55 || ve->key_AA != 0xAA) {
		panic("Invalid validation entry\n");
	}
}

/*
 * Checks if the default entry in the boot catalog is valid.
 */
static void al_check_default_entry(struct di_default_entry *de)
{
	if (de->boot_indicator != 0x88) {
		panic("Default entry not bootable\n");
	}
}

/*
 * Initializes the apploader related stuff.
 * Called by the IPL.
 */
static void al_enter(void (*report) (char *text, ...))
{
	al_control.step = 1;
#ifdef DEBUG
	al_control.report = gprintf;
#else
	al_control.report = report;
#endif
	if (al_control.report)
		al_control.report("\"El Torito\" apploader\n");
}

/*
 * This is the apploader main processing function.
 * Called by the IPL.
 */
static int al_load(void **address, uint32_t *length, uint32_t *offset)
{
	struct di_boot_record *br;
	struct di_validation_entry *validation_entry;
	struct di_default_entry *default_entry;

	struct gcm_disk_header *disk_header;
	struct gcm_disk_header_info *disk_header_info;

	struct dol_header *dh;
	unsigned long lowest_start;
	int j, k;

	int need_more = 1; /* this tells the IPL if we need more data or not */

	if (al_control.report)
		al_control.report("step %d\n", al_control.step);

	switch (al_control.step) {
	case 0:
		/* should not get here if al_enter was called */
	case 1:
		al_control.step = 1; /* fix it to a known value */

		/* read sector 17, containing Boot Record Volume */
		*address = di_buffer;
		*length = (uint32_t) di_align(sizeof(*br));
		*offset = 17 * DI_SECTOR_SIZE;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 2:
		/* boot record volume loaded */
		br = (struct di_boot_record *)di_buffer;

		/* check "EL TORITO SPECIFICATION" id */
		if (memcmp(br->boot_system_id, "EL TORITO SPECIFICATION", 23)) {
			panic("Can't find EL TORITO boot record\n");
		}

		le32_to_cpus(&br->boot_catalog_offset);

		/* read the boot catalog */
		*address = di_buffer;
		*length = DI_SECTOR_SIZE;
		*offset = br->boot_catalog_offset * DI_SECTOR_SIZE;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 3:
		/* boot catalog loaded */

		/* check validation entry */
		validation_entry = (struct di_validation_entry *)di_buffer;
		al_check_validation_entry(validation_entry);

		/* check default bootable entry */
		default_entry = (struct di_default_entry *)(di_buffer + 0x20);
		al_check_default_entry(default_entry);

		le16_to_cpus(&default_entry->sector_count);
		le32_to_cpus(&default_entry->load_rba);

		bl_control.size = default_entry->sector_count * 512;
		bl_control.offset = default_entry->load_rba * DI_SECTOR_SIZE;

		/* request the .dol header */
		*address = di_buffer;
		*length = DOL_HEADER_SIZE;
		*offset = bl_control.offset;
		invalidate_dcache_range(*address, *address + *length);

		bl_control.sects_bitmap = 0xdeadbeef;
		al_control.step++;
		break;
	case 4:
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
			/* setup .bss section */
			if (dh->size_bss)
				memset((void *)dh->address_bss, 0,
				       dh->size_bss);

			/* bye, bye */
			al_control.step++;
		}
		break;
	case 5:
		/* all .dol sections loaded */

		/* read sector 0, containing disk header and disk header info */
		*address = di_buffer;
		*length = (uint32_t) di_align(sizeof(*disk_header) + sizeof(*disk_header_info));
		*offset = 0;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 6:
		/* boot.bin and bi2.bin header loaded */

		disk_header = (struct gcm_disk_header *)di_buffer;
		disk_header_info = (struct gcm_disk_header_info *)(di_buffer + sizeof(*disk_header));

		al_control.fst_offset = disk_header->layout.fst_offset;
		al_control.fst_size = disk_header->layout.fst_size;
		al_control.fst_address = (0x81800000 - al_control.fst_size) & DI_ALIGN_MASK;

		/* read fst.bin */
		*address = (void *)al_control.fst_address;
		*length = (uint32_t) di_align(al_control.fst_size);
		*offset = al_control.fst_offset;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 7:
		/* fst.bin loaded */

		al_control.bi2_address = al_control.fst_address - 0x2000;

		/* read bi2.bin */
		*address = (void *)al_control.bi2_address;
		*length = 0x2000;
		*offset = 0x440;
		invalidate_dcache_range(*address, *address + *length);

		al_control.step++;
		break;
	case 8:
		/* bi2.bin loaded */

		lowmem->a_boot_magic = 0x0d15ea5e;
		lowmem->a_version = 1;

		lowmem->a_arena_hi = al_control.fst_address;
		lowmem->a_fst = (void *)al_control.fst_address;
		lowmem->a_fst_max_size = al_control.fst_size;
		//memset(&lowmem->a_debugger_info, 0, sizeof(struct dolphin_debugger_info));
		//lowmem->a_debug_monitor_size = 0;
		lowmem->a_debug_monitor = (void *)0x81800000;
		lowmem->a_simulated_memory_size = 0x01800000;
		lowmem->a_bi2 = (void *)al_control.bi2_address;
		flush_dcache_range(lowmem, lowmem+1);

		*length = 0;
		need_more = 0;
		al_control.step++;
		break;
	default:
		al_control.step++;
		break;
	}

	return need_more;
}

/*
 *
 */
static void *al_exit(void)
{
	return bl_control.entry_point;
}

// credit to swiss-gc
int DVD_LowRead64(void* dst, uint32_t len, uint64_t offset) {
	volatile uint32_t* dvd = (volatile uint32_t*)0xCC006000;
	if(offset>>2 > 0xFFFFFFFF)
		return -1;

	if ((((uint32_t)dst) & 0xC0000000) == 0x80000000) // cached?
		dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xA8000000;
	dvd[3] = offset >> 2;
	dvd[4] = len;
	dvd[5] = (uint32_t)dst;
	dvd[6] = len;
	dvd[7] = 3; // enable reading!
	while (dvd[7] & 1);

	invalidate_dcache_range(dst, dst + len);
	if (dvd[0] & 0x4)
		return 1;
	return 0;
}
