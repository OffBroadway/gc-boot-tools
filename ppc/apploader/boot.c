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
