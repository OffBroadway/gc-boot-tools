#include <stdint.h>
#include <stddef.h>

#include "types.h"
#include "libc.h"
#include "ipl.h"
#include "addr.h"

#include "crc32.h"
#include "usbgecko.h"

#include "instructions.h"
#include "../include/system.h"

void save_ipl(void);
void fix_ipl_state(void);
void new_routine(void);

#define CONST(...) __VA_ARGS__

typedef struct {
	uint32_t count;
	uint32_t offset[];
} asset_table_t;

void repair_asset_table(u32 asset_addr) {
	asset_table_t *table = (asset_table_t*)asset_addr;

	const u32 compressed_val = CONST(0x59617930); // yay0 compressed header
	u32 cur_asset_offset = 0;
	for (int i = 0; i < table->count; i++) {
		u32 offset = table->offset[i];
		if (offset & 0xff000000) {
			u32 *asset_values = (u32*)(asset_addr + cur_asset_offset);
			while(1) {
				u32 value = *asset_values;
				if (value == compressed_val) {
					offset = (u32)asset_values - asset_addr;
					u32 *offset_addr = &table->offset[i];
					*offset_addr = offset;

					gprintf("Fixed offset %p = %x\n", offset_addr, offset);
					break;
				}
				asset_values++;
			}
		}
		cur_asset_offset = offset + 4;
	}
}

typedef struct {
    u16 magic;
    u8 revision;
    u8 padding;
    u32 blob_checksum;
    u32 code_size;
    u32 code_checksum;
} ipl_metadata_t;

static void set_ipl_metadata(enum ipl_revision revision) {
    u32 code_end_addr = 0;
    if (*(u32*)0x81300310 == 0x81300000) {
        code_end_addr = *(u32*)0x81300324;
    } else {
        code_end_addr = *(u32*)0x8130039c;
    }
    u32 code_size = code_end_addr - 0x81300000;
    gprintf("Code size: %x\n", code_size);

    ipl_metadata_t *metadata = (void*)0x81500000 - sizeof(ipl_metadata_t);
    _memset(metadata, 0, sizeof(ipl_metadata_t));

    // hash the blob
    const u32 ipl_size = 2 * 1024 * 1024;
    metadata->blob_checksum = tinf_crc32((void*)0x81300000, ipl_size);

    // hash the code blob
    metadata->code_size = code_size;
    metadata->code_checksum = tinf_crc32((void*)0x81300000, code_size);

    metadata->magic = 0xC0DE;
    metadata->revision = (u8)revision;

    // print metadata, one entry per line
    gprintf("Metadata:\n");
    gprintf("\tMagic: %x\n", metadata->magic);
    gprintf("\tRevision: %x\n", metadata->revision);
    gprintf("\tBlob checksum: %x\n", metadata->blob_checksum);
    gprintf("\tCode size: %x\n", metadata->code_size);
    gprintf("\tCode checksum: %x\n", metadata->code_checksum);

    return;
}

void fix_ipl_state(void) {
	u32 asset_addr = 0;

    enum ipl_revision revision = get_ipl_revision();
	switch (revision) {
	case IPL_NTSC_10_001:
		asset_addr = 0x8135ea20;
		break;
	case IPL_NTSC_11_001:
		asset_addr = 0x8137ec00;
		break;
	case IPL_NTSC_12_001:
		asset_addr = 0x8137fec0;
		break;
	case IPL_NTSC_12_101:
		asset_addr = 0x81380340;
		break;
	case IPL_PAL_10_001:
		asset_addr = 0x81381820;
		break;
	case IPL_MPAL_11:
		asset_addr = 0x8137eb20;
		break;
	case IPL_PAL_12_101:
		asset_addr = 0x81382cc0;
		break;
	default:
		break;
	}

	repair_asset_table(asset_addr);
    set_ipl_metadata(revision);
}

static u32 inception_flag = 0;
void inception() {
	if (get_ipl_revision() != IPL_UNKNOWN && inception_flag != 0xbeefcafe) {
		inception_flag = 0xbeefcafe;
		gprintf("Early code exec\n");
		gprintf("INCEPTION\n");

		fix_ipl_state();
		save_ipl();

		// allow any region
		{
			uint32_t *target = (uint32_t *)find_region_check();
			GEN_LI(*target, R0, 14);
			flush_dcache_range(target, target+1);
			invalidate_icache_range(target, target+1);
		}

		// take over main thread
		{
			u32 *target = (void*)find_loop_call();
			GEN_B(*target, ((u32*)new_routine - target), 0, 1);
			flush_dcache_range(target, target+1);
			invalidate_icache_range(target, target+1);
		}
	}
}

void new_routine() {
	gprintf("New routine\n");

	u32* state = (u32*)find_state_addr();
	void (*process)() = (void*)find_process_func();

	while(1) {
		if (*state >= 14) {
			break;
		}

		process();
	}

	gprintf("Program ready\n");

	void (*boot)() = (void*)find_boot_func();
	boot();

	__builtin_unreachable();
}
