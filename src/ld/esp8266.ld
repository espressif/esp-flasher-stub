/* Note: stub is deliberately loaded close to the very top
   of available RAM, to reduce change of colliding with anything
   else... */
MEMORY {
  iram : org = 0x4010C500, len = 0x3500
  dram : org = 0x3FFE8100, len = 0x13f00
}

INCLUDE common.ld

/* Override the commonly defined entrypoint */
ENTRY(esp_main_esp8266)

PROVIDE(SPIFlashModeConfig = 0x40004568);
PROVIDE(SPIParamCfg = 0x40004c2c);
