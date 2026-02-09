/* Note: stub is deliberately loaded close to the very top
   of available RAM, to reduce change of colliding with anything
   else... */
MEMORY {
  iram : org = 0x400A8000, len = 0x2000
  dram : org = 0x3ffcc000, len = 0x14000
}

INCLUDE common.ld
