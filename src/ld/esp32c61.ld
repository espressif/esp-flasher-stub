/*
 * ESP32-C61 SRAM is one unified region: 0x40800000 - 0x40850000.
 * ROM static data occupies the top of it (see esp-idf
 * components/bootloader/subproject/main/ld/esp32c61/bootloader.memory.ld.in):
 *
 *   0x4083E670 - 0x4084C670  ROM shared buffers, download mode only
 *   0x4084C670 - 0x4084E670  ROM PRO CPU stack
 *   0x4084E670 - 0x40850000  ROM .bss/.data
 *
 * The stub runs on the ROM stack and calls into ROM code, so it stays entirely
 * below 0x4083E670. iram sits at the base of SRAM and dram at the next 64 KB
 * boundary above the iram window.
 */

MEMORY {
  iram : org = 0x40800000, len = 0x4500
  dram : org = 0x40810000, len = 0x18000
}

INCLUDE common.ld
