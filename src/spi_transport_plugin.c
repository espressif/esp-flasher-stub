/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * SPI slave transport plugin — compiled as a separate binary that the host
 * loads only when it talks to the stub over SPI.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <esp-stub-lib/err.h>
#include <esp-stub-lib/slave_spi.h>
#include "frame_buffer.h"
#include "plugin_table.h"
#include "transport.h"

// SPI TX sends at most 4092 bytes per frame.
#define SPI_MAX_TX_FRAME_SIZE 4092U

/* ---- SPI transport ops ---------------------------------------------------- */
/* Like SDIO, SPI bypasses SLIP: DMA writes a whole frame into the armed buffer
 * and stub_lib_slave_spi_take_rx_frame() (polled) latches its length. */

static void spi_arm(void)
{
    stub_lib_slave_spi_rearm(frame_buffer_acquire(), FRAME_BUFFER_SIZE);
}

static const uint8_t *spi_recv_poll(size_t *len, bool *error)
{
    *error = false;

    enum frame_buffer_state state = frame_buffer_get_state();
    if (state == FRAME_BUFFER_STATE_IDLE) {
        size_t n;
        if (!stub_lib_slave_spi_take_rx_frame(&n)) {
            spi_arm();
            return NULL;
        }
        frame_buffer_mark_complete(n);   /* frame already DMA'd into proc buf */
        state = frame_buffer_get_state();
        spi_arm();                       /* take done → switch to spare + arm */
    }
    if (state == FRAME_BUFFER_STATE_ERROR) {
        *error = true;
        return NULL;
    }
    return frame_buffer_get_data(len);
}

static bool spi_send_frame(const void *data, size_t len)
{
    /* SPI TX uses one 12-bit DMA descriptor and is capped at the largest
     * 4-byte-aligned descriptor size, 4092 bytes. */
    return stub_lib_slave_spi_tx_frame(data, len) == STUB_LIB_OK;
}

/* ---- Transport plugin hook ------------------------------------------------ */

static const struct stub_transport_ops s_ops = {
    .recv_poll = spi_recv_poll,
    .recv_release = frame_buffer_reset,
    .send_frame = spi_send_frame,
    .max_tx_frame_size = SPI_MAX_TX_FRAME_SIZE,
};

static bool spi_transport_is_active(void)
{
    return stub_lib_slave_spi_is_active();
}

static const struct stub_transport_ops *spi_transport_init(void)
{
    stub_lib_slave_spi_init();
    /* Arm the first DMA receive buffer immediately after init. */
    spi_arm();
    return &s_ops;
}

void spi_transport_plugin_start(void)
{
    plugin_zero_bss();

    transport_plugin.is_active = spi_transport_is_active;
    transport_plugin.init = spi_transport_init;
}
