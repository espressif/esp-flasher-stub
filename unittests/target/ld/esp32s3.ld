/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
/* Enhanced ESP32-S3 Linker Script for Target Tests
 * Optimized for libc usage with proper memory layout and heap support
 */

MEMORY {
  /* Use the working ESP32-S3 addresses from the main project with increased size for Unity tests */
  iram : org = 0x40378000, len = 0x18000  /* 96KB using proven address from main project */

  /* DRAM for data, BSS, and heap - ESP32-S3 proven addresses */
  dram : org = 0x3FCA0000, len = 0x28000  /* 160KB using proven address from main project */
}

ENTRY(esp_main)

SECTIONS {
  .text : ALIGN(4) {
    /* Place all literals first to avoid Xtensa relocation issues */
    *(.literal .literal.* .literal.*.literal)
    *(.text .text.*)
    *(.iram1 .iram1.*)
    . = ALIGN(4);
  } > iram

  .bss : ALIGN(4) {
    _bss_start = ABSOLUTE(.);
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _bss_end = ABSOLUTE(.);
  } > dram

  .data : ALIGN(4) {
    *(.data .data.*)
    *(.rodata .rodata.*)
    *(.sdata .sdata.*)
    *(.srodata .srodata.*)
    . = ALIGN(4);
  } > dram

  .heap : ALIGN(4) {
    __heap_start = ABSOLUTE(.);
    _heap_start = ABSOLUTE(.);
    end = ABSOLUTE(.);
    _end = ABSOLUTE(.);
    __end = ABSOLUTE(.);

    /* Reserve heap space for ESP32-S3 - balanced with available DRAM */
    . = . + 0x8000;  /* 32KB heap space */

    __heap_end = ABSOLUTE(.);
    _heap_end = ABSOLUTE(.);
  } > dram

  .stack : ALIGN(4) {
    _stack_start = ABSOLUTE(.);
    . = . + 0x2000;  /* 8KB stack for ESP32-S3 */
    _stack_top = ABSOLUTE(.);
    __stack = _stack_top;
  } > dram

  /* Debug sections */
  .debug_info     0 : { *(.debug_info) }
  .debug_line     0 : { *(.debug_line) }
  .debug_str      0 : { *(.debug_str) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_ranges   0 : { *(.debug_ranges) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_loc      0 : { *(.debug_loc) }

  /DISCARD/ : {
    *(.note.GNU-stack)
    *(.gnu_debuglink)
    *(.gnu.lto_*)
    *(.interp)
    *(.comment)
  }
}

ASSERT(SIZEOF(.text) <= 0x18000, "Text section exceeds IRAM space")
ASSERT(_stack_top <= 0x3FCA0000 + 0x28000, "Stack exceeds DRAM space")

PROVIDE(__heap_size = __heap_end - __heap_start);
PROVIDE(__stack_size = _stack_top - _stack_start);
