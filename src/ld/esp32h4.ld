MEMORY {
  iram : org = 0x40810000, len = 0x4500
  dram : org = 0x40830000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x4085ffc4 for this chip (rom_cache_internal_table_ptr, see esp-stub-lib esp32h4.rom.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x4085ffc4, "esp32h4: stub DRAM window overlaps ROM data")

INCLUDE common.ld
