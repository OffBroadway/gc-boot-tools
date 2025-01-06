#include <stdint.h>
#include <stddef.h>

#include "types.h"
#include "usbgecko.h"

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

void fix_ipl_state(void) {
	void (*GXFlush)() = NULL;
	void (*GXAbortFrame)() = NULL;
	u32 asset_addr = 0;

	switch (get_ipl_revision()) {
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
}
