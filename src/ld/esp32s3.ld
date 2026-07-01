MEMORY {
  iram : org = 0x40378000, len = 0x18000
  dram : org = 0x3FCA0000, len = 0x28000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x3fcef81c for this chip (phy_param_rom, see esp-stub-lib esp32s3.rom.ld).
   The hardware-verified 0x28000 window already fits the 32KB stub and is left unchanged. */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x3fcef81c, "esp32s3: stub DRAM window overlaps ROM data")

INCLUDE common.ld
