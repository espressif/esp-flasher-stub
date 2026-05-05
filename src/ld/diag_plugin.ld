/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * Diag plugin linker script.
 *
 * The following values must be supplied via -Wl,--defsym by CMake:
 *   PLUGIN_TEXT_ADDR             — IRAM load address (base text_end, 16-byte aligned)
 *   PLUGIN_BSS_ADDR              — DRAM load address (base bss_end, 4-byte aligned)
 *   BASE_SLIP_SEND_FRAME         — address of slip_send_frame in base stub
 *   BASE_SLIP_RECV_RESET         — address of slip_recv_reset in base stub
 *   BASE_SLIP_IS_FRAME_COMPLETE  — address of slip_is_frame_complete in base stub
 *   BASE_SLIP_GET_FRAME_DATA     — address of slip_get_frame_data in base stub
 *   BASE_STUB_LOGF_PTR           — address of stub_logf fn-pointer in base stub BSS
 */

MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x8000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x4000
}

PROVIDE(slip_send_frame        = BASE_SLIP_SEND_FRAME);
PROVIDE(slip_recv_reset        = BASE_SLIP_RECV_RESET);
PROVIDE(slip_is_frame_complete = BASE_SLIP_IS_FRAME_COMPLETE);
PROVIDE(slip_get_frame_data    = BASE_SLIP_GET_FRAME_DATA);

/*
 * base_stub_logf_ptr resolves to the address of the stub_logf function pointer
 * in base stub BSS.  Assigning through it on first handler entry installs
 * s_logf_impl() into the base stub without any runtime symbol lookup.
 */
PROVIDE(base_stub_logf_ptr = BASE_STUB_LOGF_PTR);

ENTRY(diag_plugin_log_read)

INCLUDE common.ld

ASSERT(SIZEOF(.data) == 0, "Plugin must not have initialized .data — use BSS instead")
