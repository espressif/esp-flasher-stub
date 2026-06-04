/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * Generic Key Manager plugin linker script.
 *
 * The following values must be supplied via -Wl,--defsym by CMake:
 *   PLUGIN_TEXT_ADDR             — IRAM load address (16-byte aligned)
 *   PLUGIN_BSS_ADDR              — DRAM load address (4-byte aligned)
 *   BASE_SLIP_SEND_FRAME         — address of slip_send_frame in base stub
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x8000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x4000
}

/*
 * Forward SLIP calls to the base stub's implementation. The plugin's
 * post_process streams its output frames (huk_info / key_info / k2*G) via
 * slip_send_frame; it receives its input through the command dispatcher's
 * (data, len) arguments, so no recv-side helpers are needed. (The earlier
 * slip_recv_reset / slip_is_frame_complete / slip_get_frame_data helpers
 * were removed when the transport layer's receive path was refactored.)
 */
PROVIDE(slip_send_frame         = BASE_SLIP_SEND_FRAME);

INCLUDE common.ld

/* Override common.ld's ENTRY(esp_main) — the plugin has no esp_main. */
ENTRY(km_plugin_key_deploy_random)

/* The base stub calls these handlers through the plugin function table, not
 * from within the plugin, so they are unreachable from ENTRY. Without these
 * EXTERN declarations --gc-sections would drop every handler but the entry,
 * and elf2json.py would fail to resolve their addresses. Must stay in sync
 * with PLUGIN_HANDLER_SYMBOLS in tools/elf2json.py. */
EXTERN(km_plugin_key_deploy_random);
EXTERN(km_plugin_key_deploy_aes);
EXTERN(km_plugin_key_deploy_ecdh0);
EXTERN(km_plugin_key_deploy_ecdh1);
EXTERN(km_plugin_key_recovery);
EXTERN(km_plugin_huk_deploy);
EXTERN(km_plugin_huk_recovery);

ASSERT(SIZEOF(.data) == 0, "Plugin must not have initialized .data — use BSS instead")
