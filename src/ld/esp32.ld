/* Note: stub is deliberately loaded close to the very top
   of available RAM, to reduce change of colliding with anything
   else... */
MEMORY {
  iram : org = 0x400A8000, len = 0x2100
  dram : org = 0x3ffc3000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x3ffe0000 for this chip; the first ROM DRAM global is
   gpio_pending_mask = 0x3ffe0038 (see esp-stub-lib esp32.rom.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x3ffe0000, "esp32: stub DRAM window overlaps ROM data")

INCLUDE common.ld
