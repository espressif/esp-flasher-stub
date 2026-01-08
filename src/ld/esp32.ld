/* Note: stub is deliberately loaded close to the very top
   of available RAM, to reduce change of colliding with anything
   else... */
MEMORY {
  iram : org = 0x400BE000, len = 0x1500
  dram : org = 0x3ffcc000, len = 0x14000
}

INCLUDE common.ld
