MEMORY {
  iram : org = 0x40380000, len = 0x4500
  dram : org = 0x3FCA4000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x3fcdfaa4 for this chip (phy_param_rom, see esp-stub-lib esp32c2.rom.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x3fcdfaa4, "esp32c2: stub DRAM window overlaps ROM data")

INCLUDE common.ld
