/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * Generic NAND plugin linker script.
 *
 * The following values must be supplied via -Wl,--defsym by CMake:
 *   PLUGIN_TEXT_ADDR          — IRAM load address (base text_end, 16-byte aligned)
 *   PLUGIN_BSS_ADDR           — DRAM load address (base data_end, 4-byte aligned)
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x8000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x4000
}

/* Override the ENTRY(esp_main) from common.ld — the plugin has no esp_main */
ENTRY(nand_plugin_attach)

INCLUDE common.ld

ASSERT(SIZEOF(.data) == 0, "Plugin must not have initialized .data — use BSS instead")
