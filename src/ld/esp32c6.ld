MEMORY {
  iram : org = 0x40800000, len = 0x4500
  dram : org = 0x40840000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x4087ffc8 for this chip (rom_cache_internal_table_ptr, see esp-stub-lib esp32c6.rom.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x4087ffc8, "esp32c6: stub DRAM window overlaps ROM data")

INCLUDE common.ld
