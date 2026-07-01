MEMORY {
  iram : org = 0x40028000, len = 0x18000
  dram : org = 0x3FFD0000, len = 0x28000
}

/* Compile-time guard against overrunning into the ROM download stack.
   The 0x28000 window is the upstream (espressif) value, kept unchanged. It deliberately spans
   the ROM shared-buffer scratch region near the top of DRAM (e.g. g_shared_buffers = 0x3ffeab04,
   just above the _dram0_0_start = 0x3ffeab00 marker), which the stub is free to reuse while it
   runs. The ceiling 0x3fffc410 is _stack_sentry -- the bottom of the ROM download stack, the real
   limit the window must not cross (see esp-stub-lib esp32s2.rom.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x3fffc410, "esp32s2: stub DRAM window reaches the ROM download stack")

INCLUDE common.ld
