MEMORY {
  iram : org = 0x2F001000, len = 0x4500
  dram : org = 0x2F00A000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x2f07ffac for this chip (s_usb_osglue, see esp-stub-lib esp32s31.rom.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x2f07ffac, "esp32s31: stub DRAM window overlaps ROM data")

INCLUDE common.ld
