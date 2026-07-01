MEMORY {
  iram : org = 0x40380000, len = 0x4500
  dram : org = 0x3FC84000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x3fcdf834 for this chip (g_coex_param_ptr, see esp-stub-lib esp32c3.rom.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x3fcdf834, "esp32c3: stub DRAM window overlaps ROM data")

INCLUDE common.ld
