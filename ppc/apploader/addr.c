#include "types.h"
#include "ipl.h"

u32 find_region_check() {
	u32 inst_addr = 0;

	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		inst_addr = 0x81300ebc;
		break;
	case IPL_NTSC_11_001:
		inst_addr = 0x81300d5c;
		break;
	case IPL_NTSC_12_001:
		inst_addr = 0x81301110;
		break;
	case IPL_NTSC_12_101:
		inst_addr = 0x81301114;
		break;
	case IPL_PAL_10_001:
		inst_addr = 0x81300d5c;
		break;
	case IPL_MPAL_11:
		inst_addr = 0x81300d5c;
		break;
	case IPL_PAL_12_101:
		inst_addr = 0x81300ec8;
		break;
	default:
		break;
	}

	return inst_addr;
}

u32 find_loop_call() {
	u32 inst_addr = 0;

	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		inst_addr = 0x8130128c;
		break;
	case IPL_NTSC_11_001:
		inst_addr = 0x81301094;
		break;
	case IPL_NTSC_12_001:
		inst_addr = 0x81301448;
		break;
	case IPL_NTSC_12_101:
		inst_addr = 0x8130144c;
		break;
	case IPL_PAL_10_001:
		inst_addr = 0x81301094;
		break;
	case IPL_MPAL_11:
		inst_addr = 0x81301094;
		break;
	case IPL_PAL_12_101:
		inst_addr = 0x81301200;
		break;
	default:
		break;
	}

	return inst_addr;
}

u32 find_process_func() {
	u32 func_addr = 0;

	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		func_addr = 0x813022c0;
		break;
	case IPL_NTSC_11_001:
		func_addr = 0x813020c8;
		break;
	case IPL_NTSC_12_001:
		func_addr = 0x8130247c;
		break;
	case IPL_NTSC_12_101:
		func_addr = 0x81302494;
		break;
	case IPL_PAL_10_001:
		func_addr = 0x813020c8;
		break;
	case IPL_MPAL_11:
		func_addr = 0x813020c8;
		break;
	case IPL_PAL_12_101:
		func_addr = 0x81302248;
		break;
	default:
		break;
	}

	return func_addr;
}

u32 find_state_addr() {
	u32 var_addr = 0;

	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		var_addr = 0x8145d548;
		break;
	case IPL_NTSC_11_001:
		var_addr = 0x814813c8;
		break;
	case IPL_NTSC_12_001:
		var_addr = 0x814834a0;
		break;
	case IPL_NTSC_12_101:
		var_addr = 0x81483920;
		break;
	case IPL_PAL_10_001:
		var_addr = 0x814ad268;
		break;
	case IPL_MPAL_11:
		var_addr = 0x8147c088;
		break;
	case IPL_PAL_12_101:
		var_addr = 0x814af560;
		break;
	default:
		break;
	}

	return var_addr;
}

u32 find_boot_func() {
	u32 func_addr = 0;

	switch (get_ipl_revision()) {
	case IPL_NTSC_10_001:
		func_addr = 0x81300938;
		break;
	case IPL_NTSC_11_001:
		func_addr = 0x81300830;
		break;
	case IPL_NTSC_12_001:
		func_addr = 0x81300bb0;
		break;
	case IPL_NTSC_12_101:
		func_addr = 0x81300bb4;
		break;
	case IPL_PAL_10_001:
		func_addr = 0x81300830;
		break;
	case IPL_MPAL_11:
		func_addr = 0x81300830;
		break;
	case IPL_PAL_12_101:
		func_addr = 0x81300968;
		break;
	default:
		break;
	}

	return func_addr;
}