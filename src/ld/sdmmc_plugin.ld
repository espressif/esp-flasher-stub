/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * SDMMC plugin linker script.  PLUGIN_TEXT_ADDR / PLUGIN_BSS_ADDR are supplied
 * via -Wl,--defsym by CMake based on the base stub's text/data end addresses.
 * See docs/plugin-system.md.
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x8000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x4000
}

INCLUDE common.ld

/* Override ENTRY(esp_main) from common.ld; ENTRY alone roots only this symbol,
 * so EXTERN every other handler from PLUGIN_HANDLER_SYMBOLS['sdmmc']. */
ENTRY(sdmmc_plugin_attach)
EXTERN(sdmmc_plugin_attach);
EXTERN(sdmmc_plugin_read_flash);
EXTERN(sdmmc_plugin_write_flash_begin);
EXTERN(sdmmc_plugin_write_flash_data);
EXTERN(sdmmc_plugin_write_flash_end);
EXTERN(sdmmc_plugin_erase_region);
EXTERN(sdmmc_plugin_get_info);

ASSERT(SIZEOF(.data) == 0, "Plugin must not have initialized .data — use BSS instead")
