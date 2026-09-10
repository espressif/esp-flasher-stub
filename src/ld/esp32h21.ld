/*
 * ESP32-H21 SRAM is one unified 320 KB I/D region: 0x40800000 - 0x40850000
 * (SOC_IRAM_LOW/HIGH == SOC_DRAM_LOW/HIGH, see soc/soc.h).
 *
 * The top of it belongs to the ROM. The boundary is 0x4083BA78, which both
 * upstream sources agree on:
 *
 *   esp-rom (release/esp32h21), rom/target/esp32h21/eagle.rom.template.esp32h21.ld:
 *     DRAM_END 0x40850000, DRAM_SIZE = DRAM_DATA_SIZE (0x14550)
 *     + INTERFACE_DATA_SIZE (56) + SDIO_EXTRA_DRAM (0, H21 has no SDIO)
 *     => DRAM_START = 0x40850000 - 0x14588 = 0x4083BA78
 *
 *   esp-idf, components/bootloader/subproject/main/ld/esp32h21/bootloader.memory.ld.in:
 *     0x4083BA78 - 0x4084D380  ROM shared buffers, download mode only
 *                              (usbdata 0x3900, defl 0xC000, UART 0x2000)
 *     0x4084D380 - 0x4084F380  ROM PRO CPU stack
 *     0x4084F380 - 0x4084FEE0  ROM .bss/.data, early boot
 *     0x4084FEE0 - 0x40850000  ROM .bss/.data, always live
 *
 * The stub is entered from ROM download mode and keeps using the ROM: it runs
 * on the ROM stack and calls into ROM UART / SPI flash / cache / MMU code that
 * owns the ROM .bss/.data above 0x4084D380. The shared buffers below that are
 * download-mode scratch, but the ROM is still using them while it receives the
 * stub itself over MEM_DATA, so the stub is placed entirely below 0x4083BA78 -
 * the part of SRAM the ROM never touches. Nothing has to be reclaimed and the
 * load itself cannot overlap the ROM's own receive buffers.
 *
 *   iram  0x40800000 - 0x40804500   stub .text
 *   dram  0x40810000 - 0x40828000   stub .bss + .data
 *                                   (0x13A78 of headroom before 0x4083BA78)
 *
 * dram starts at the next 64 KB boundary above iram, same as on the other
 * unified-SRAM targets (esp32c5/c6, esp32h2, esp32h4, esp32p4).
 */

MEMORY {
  iram : org = 0x40800000, len = 0x4500
  dram : org = 0x40810000, len = 0x18000
}

INCLUDE common.ld
