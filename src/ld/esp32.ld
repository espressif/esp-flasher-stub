/* Note: stub is deliberately loaded close to the very top
   of available RAM, to reduce change of colliding with anything
   else. DRAM origin moved down 8KB to give stub more data space
   (needed for NAND handlers etc.) while keeping end within 0x3FFE0000. */
MEMORY {
  iram : org = 0x400A8000, len = 0x2000
  dram : org = 0x3ffca000, len = 0x16000
}

INCLUDE common.ld
