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

INCLUDE common.ld

/* Override the ENTRY(esp_main) from common.ld — the plugin has no esp_main */
ENTRY(nand_plugin_attach)

/*
 * Root every handler exported to esptool via tools/elf2json.py
 * PLUGIN_HANDLER_SYMBOLS['nand'].  Handlers are only invoked after the FPT is
 * patched at upload time, so the plugin link has no in-image references; without
 * these EXTERN declarations --gc-sections would drop them (ENTRY alone keeps only
 * the attach handler).  Keep this list in sync with elf2json.py.
 */
EXTERN(nand_plugin_attach);
EXTERN(nand_plugin_read_bbm);
EXTERN(nand_plugin_write_bbm);
EXTERN(nand_plugin_read_flash);
EXTERN(nand_plugin_write_flash_begin);
EXTERN(nand_plugin_write_flash_data);
EXTERN(nand_plugin_write_flash_end);
EXTERN(nand_plugin_erase_flash);
EXTERN(nand_plugin_erase_region);
EXTERN(nand_plugin_read_page_debug);

ASSERT(SIZEOF(.data) == 0, "Plugin must not have initialized .data — use BSS instead")
