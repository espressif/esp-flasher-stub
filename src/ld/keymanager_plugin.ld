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
 *   BASE_SLIP_RECV_RESET         — address of slip_recv_reset in base stub
 *   BASE_SLIP_IS_FRAME_COMPLETE  — address of slip_is_frame_complete in base stub
 *   BASE_SLIP_GET_FRAME_DATA     — address of slip_get_frame_data in base stub
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x8000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x4000
}

/*
 * Forward SLIP calls to the base stub's implementations. The plugin
 * post_process streams the 552-byte key_recovery_info via slip_send_frame;
 * the recv-side helpers are unused today but kept symmetrical with NAND
 * for the eventual streaming-input handlers (e.g. large key_info on
 * recovery from flash).
 */
PROVIDE(slip_send_frame         = BASE_SLIP_SEND_FRAME);
PROVIDE(slip_recv_reset         = BASE_SLIP_RECV_RESET);
PROVIDE(slip_is_frame_complete  = BASE_SLIP_IS_FRAME_COMPLETE);
PROVIDE(slip_get_frame_data     = BASE_SLIP_GET_FRAME_DATA);

INCLUDE common.ld

/* Override common.ld's ENTRY(esp_main) — the plugin has no esp_main. */
ENTRY(km_plugin_key_deploy_random)

ASSERT(SIZEOF(.data) == 0, "Plugin must not have initialized .data — use BSS instead")
