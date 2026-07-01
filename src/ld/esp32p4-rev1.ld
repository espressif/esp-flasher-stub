MEMORY {
  iram : org = 0x4FF10000, len = 0x4500
  dram : org = 0x4FF50000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   esp32p4-rev1 maps to the esp32p4 target. The window sits between the two ROM revisions'
   pointer blocks: base esp32p4.rom.ld places s_usb_osglue lower at 0x4ff3ffc8 (below org),
   while esp32p4.rom.eco5.ld places it at 0x4ffbffc8 (above the window top). Guard against the
   higher (eco5) block. */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x4ffbffc8, "esp32p4-rev1: stub DRAM window overlaps ROM data")

INCLUDE common.ld
