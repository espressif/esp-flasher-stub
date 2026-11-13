/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * SDIO transport plugin — compiled as a separate binary that the host
 * loads only when it talks to the stub over SDIO.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <esp-stub-lib/err.h>
#include <esp-stub-lib/sdio.h>
#include "frame_buffer.h"
#include "plugin_table.h"
#include "transport.h"

/* ---- SDIO transport ops -------------------------------------------------- */
/*
 * SDIO bypasses SLIP entirely; the DMA writes a whole frame into the armed
 * buffer and the ISR latches its length for stub_lib_sdio_take_rx_frame().
 *
 * The driver disarms its receive DMA not only after delivering a frame but
 * also on no-frame paths (empty descriptor chain / TX_DSCR_EMPTY in
 * sdio_slc_isr). Those disarms are invisible to frame_buffer, so recv_poll
 * MUST re-arm whenever it finds no pending frame — otherwise the link
 * silently dies. stub_lib_sdio_rearm() is a cheap no-op while still armed.
 *
 * Ordering constraint: stub_lib_sdio_rearm() fails while a received frame is
 * still pending, so take_rx_frame() (which clears that state) must run first.
 * On the no-frame path take_rx_frame() returned false, so nothing is pending
 * and the re-arm is safe.
 */

static void sdio_arm(void)
{
    /* A NULL buffer means both frame buffers are occupied, which cannot
     * happen under the lock-step + single early-ack protocol; if it ever
     * did, RX would stall on the next frame (no silent corruption). */
    stub_lib_sdio_rearm(frame_buffer_acquire(), FRAME_BUFFER_SIZE);
}

static const uint8_t *sdio_recv_poll(size_t *len, bool *error)
{
    *error = false;

    enum frame_buffer_state state = frame_buffer_get_state();
    if (state == FRAME_BUFFER_STATE_IDLE) {
        size_t n;
        if (!stub_lib_sdio_take_rx_frame(&n)) {
            sdio_arm();
            return NULL;
        }
        frame_buffer_mark_complete(n);   /* frame already DMA'd into proc buf */
        state = frame_buffer_get_state();
        sdio_arm();                      /* take done → switch to spare + arm  */
    }
    /* state != IDLE: a prior frame awaits release; its spare is already armed
     * and may already hold an un-taken frame, so do NOT re-arm here. */
    if (state == FRAME_BUFFER_STATE_ERROR) {
        *error = true;
        return NULL;
    }
    return frame_buffer_get_data(len);
}

static bool sdio_send_frame(const void *data, size_t len)
{
    return stub_lib_sdio_tx_frame(data, len) == STUB_LIB_OK;
}

/* ---- Transport plugin hook ------------------------------------------------ */

static const struct stub_transport_ops s_ops = {
    .recv_poll = sdio_recv_poll,
    .recv_release = frame_buffer_reset,
    .send_frame = sdio_send_frame,
    .max_tx_frame_size = SDIO_DMA_DESC_MAX_LEN,
};

static bool sdio_transport_is_active(void)
{
    return stub_lib_sdio_is_active();
}

static const struct stub_transport_ops *sdio_transport_init(void)
{
    stub_lib_sdio_init();
    /* Arm the first DMA receive buffer immediately after init. */
    sdio_arm();
    return &s_ops;
}

void sdio_transport_plugin_start(void)
{
    plugin_zero_bss();

    transport_plugin.is_active = sdio_transport_is_active;
    transport_plugin.init = sdio_transport_init;
}
