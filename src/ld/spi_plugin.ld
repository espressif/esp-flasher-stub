/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * SPI transport plugin linker script.
 *
 * The following values are supplied by a generated fragment
 * (<name>_plugin_addrs.ld) that CMake pulls in via -T before this script:
 *   PLUGIN_TEXT_ADDR          — IRAM load address (base text_end, 16-byte aligned)
 *   PLUGIN_BSS_ADDR           — DRAM load address (base data_end, 4-byte aligned)
 *   frame_buffer_*            — base stub functions marked STUB_EXPORT (PROVIDE)
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x2000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x1000
}

INCLUDE common.ld

/* Override the ENTRY(esp_main) from common.ld — the plugin has no esp_main */
ENTRY(spi_transport_init)

/*
 * Root both transport hook functions exported to the host via tools/elf2json.py
 * PLUGIN_TRANSPORT_SYMBOLS['spi'].  The base stub calls them only after the host
 * patches transport_plugin, so the plugin link has no in-image references to
 * them.  Keep this list in sync with elf2json.py.
 */
EXTERN(spi_transport_is_active);
EXTERN(spi_transport_init);

ASSERT(SIZEOF(.data) == 0, "Plugin must not have initialized .data — use BSS instead")
