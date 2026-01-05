MEMORY {
  iram : org = 0x40800000, len = 0x4500
  dram : org = 0x40804000, len = 0x18000
}

ENTRY(esp_main)

SECTIONS {
  .text : ALIGN(4) {
    *(.literal)
    *(.text .text.*)
  } > iram

  .bss : ALIGN(4) {
    _bss_start = ABSOLUTE(.);
    *(.bss)
    _bss_end = ABSOLUTE(.);
  } > dram

  .data : ALIGN(4) {
    *(.data)
    *(.rodata .rodata.*)
  } > dram
}
