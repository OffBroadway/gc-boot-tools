#include <stdint.h>
#include <stddef.h>

#include "types.h"
#include "libc.h"

#include "crc32.h"
#include "usbgecko.h"

void save_ipl(void);
void fix_ipl_state(void);
void new_routine(void);

extern void run(register void* entry_point);
extern void run_interrupt(register void *entry_point);
extern void load_dol();
extern void boot_dol();

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
	void (*GXFlush)() = NULL;
	void (*GXAbortFrame)() = NULL;
	u32 asset_addr = 0;

    enum ipl_revision revision = get_ipl_revision();
	switch (revision) {
	case IPL_NTSC_10_001:
		GXFlush = (void*)0x8134ac94;
		GXAbortFrame = (void*)0x8134acfc;
		asset_addr = 0x8135ea20;
		break;
	case IPL_NTSC_11_001:
		GXFlush = (void*)0x81372ce8;
		GXAbortFrame = (void*)0x81372d44;
		asset_addr = 0x8137ec00;
		break;
	case IPL_NTSC_12_001:
		GXFlush = (void*)0x81374d90;
		GXAbortFrame = (void*)0x81374f58;
		asset_addr = 0x8137fec0;
		break;
	case IPL_NTSC_12_101:
		GXFlush = (void*)0x813751e4;
		GXAbortFrame = (void*)0x813753ac;
		asset_addr = 0x81380340;
		break;
	case IPL_PAL_10_001:
		GXFlush = (void*)0x813762c8;
		GXAbortFrame = (void*)0x81376324;
		asset_addr = 0x81381820;
		break;
	case IPL_MPAL_11:
		GXFlush = (void*)0x81372c08;
		GXAbortFrame = (void*)0x81372c64;
		asset_addr = 0x8137eb20;
		break;
	case IPL_PAL_12_101:
		GXFlush = (void*)0x8137855c;
		GXAbortFrame = (void*)0x81378724;
		asset_addr = 0x81382cc0;
		break;
	default:
		break;
	}

	// // Fake Draw Done
	// *(u8*)0xCC008000 = 0x61;
	// *(u32*)0xCC008000 = 0x45000002;

	GXFlush();
	GXAbortFrame();
	repair_asset_table(asset_addr);
    set_ipl_metadata(revision);
}

void inception() {
	u16 *signal_val = (void*)0x81300000;
	if (*signal_val == 0x4800) {
		gprintf("Early code exec\n");
		gprintf("INCEPTION\n");
		run_interrupt((void*)new_routine);
	}
}

void new_routine() {
    fix_ipl_state();
    save_ipl();

    load_dol();
    boot_dol();
}
