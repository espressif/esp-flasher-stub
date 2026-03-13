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
 *   BASE_SLIP_SEND_FRAME      — address of slip_send_frame in base stub
 *   BASE_SLIP_RECV_RESET      — address of slip_recv_reset in base stub
 *   BASE_SLIP_IS_FRAME_COMPLETE  — address of slip_is_frame_complete in base stub
 *   BASE_SLIP_GET_FRAME_DATA  — address of slip_get_frame_data in base stub
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x8000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x4000
}

/*
 * Forward SLIP calls to the base stub's implementations.  The ISR writes into
 * the base stub's SLIP receive buffers, so the plugin must use them for RX too.
 * PROVIDE is ignored if a compiled object already defines the symbol.
 */
PROVIDE(slip_send_frame         = BASE_SLIP_SEND_FRAME);
PROVIDE(slip_recv_reset         = BASE_SLIP_RECV_RESET);
PROVIDE(slip_is_frame_complete  = BASE_SLIP_IS_FRAME_COMPLETE);
PROVIDE(slip_get_frame_data     = BASE_SLIP_GET_FRAME_DATA);

INCLUDE common.ld
