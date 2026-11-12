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
 *
 * The plugin's .data follows its .bss in DRAM; the host uploads .data and the
 * plugin zeroes its own .bss.
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x2000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x1000
}

INCLUDE common.ld

/* Override the ENTRY(esp_main) from common.ld — the plugin has no esp_main */
ENTRY(spi_transport_plugin_start)
