#include <stdint.h>
#include <stddef.h>

#include "types.h"
#include "../include/system.h"

#include "usbgecko.h"

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
	asm("mfmsr 4");
	asm("rlwinm 4, 4, 0, 17, 15");
	asm("mtmsr 4");
	asm("isync");

    asm("mtsrr0 %0" : : "r" (entry_point));

    asm("li 4, 0x30");
	asm("mtsrr1 4");

    asm("rfi");
}

volatile unsigned long* dvd = (volatile unsigned long*)0xCC006000;
int dvd_read(void* dst, u32 len, uint64_t offset)
{
	if(offset>>2 > 0xFFFFFFFF)
		return -1;

	if ((((u32)dst) & 0xC0000000) == 0x80000000) // cached?
		dvd[0] = 0x2E;

	dvd[1] = 0;
	dvd[2] = 0xA8000000;
	dvd[3] = offset >> 2;
	dvd[4] = len;
	dvd[5] = (u32)dst;
	dvd[6] = len;
	dvd[7] = 3; // enable reading!
	while (dvd[7] & 1);

	if (dvd[6])
		return 1;

	return 0;
}

extern void al_enter(void (*report) (const char *text, ...));
extern int al_load(void **address, uint32_t *length, uint32_t *offset);
extern void *al_exit(void);

#ifndef DEBUG
void stub_report(const char *text, ...) {
    // stub
}
#endif

void load_dol() {
#ifdef DEBUG
    al_enter(gprintf);
#else
    al_enter(stub_report);
#endif
    while(1) {
        void *dst = 0;
        int len = 0, offset = 0;
        int res = al_load(&dst,&len,&offset);
		// gprintf("res = %d\n", res);
        if (!res) break;
        int err = dvd_read(dst,len,offset);
        if (err) panic("Apploader read failed");
		invalidate_dcache_range(dst,dst+(len>>2));
        invalidate_icache_range(dst,dst+(len>>2));
    }
	gprintf("GOT ENTRY\n");
}

void boot_dol() {
    void* entrypoint = al_exit();
    gprintf("THIS ENTRY, %p\n", entrypoint);
    run(entrypoint);
    __builtin_unreachable();
}
