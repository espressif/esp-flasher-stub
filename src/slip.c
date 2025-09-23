/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "slip.h"
#include <esp-stub-lib/rom_wrappers.h>

/* SLIP Protocol Constants */
#define SLIP_END            0xC0    /* Frame delimiter */
#define SLIP_ESC            0xDB    /* Escape character */
#define SLIP_ESC_END        0xDC    /* Escaped frame delimiter */
#define SLIP_ESC_ESC        0xDD    /* Escaped escape character */

/* SLIP State Machine */
typedef enum {
    STATE_NO_FRAME,          /* Not in a frame */
    STATE_IN_FRAME,          /* Processing frame data */
    STATE_ESCAPING           /* Processing escape sequence */
} slip_state_t;

void slip_send_frame_delimiter(void)
{
    stub_lib_uart_tx_one_char(SLIP_END);
}

void slip_send_frame_data(uint8_t byte)
{
    switch (byte) {
    case SLIP_END:
        stub_lib_uart_tx_one_char(SLIP_ESC);
        stub_lib_uart_tx_one_char(SLIP_ESC_END);
        break;
    case SLIP_ESC:
        stub_lib_uart_tx_one_char(SLIP_ESC);
        stub_lib_uart_tx_one_char(SLIP_ESC_ESC);
        break;
    default:
        stub_lib_uart_tx_one_char(byte);
        break;
    }
}

void slip_send_frame_data_buf(const void *data, size_t size)
{
    if (!data) {
        return;
    }

    const uint8_t *buf = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++) {
        slip_send_frame_data(buf[i]);
    }
}

void slip_send_frame(const void *data, size_t size)
{
    if (!data) {
        return;
    }

    slip_send_frame_delimiter();
    slip_send_frame_data_buf(data, size);
    slip_send_frame_delimiter();
}

void slip_recv_byte(uint8_t byte)
{
    static slip_state_t state = STATE_NO_FRAME;

    switch (state) {
    case STATE_NO_FRAME:
        if (byte == SLIP_END) {
            // Start new frame
            state = STATE_IN_FRAME;
        }
        break;

    case STATE_IN_FRAME:
        if (byte == SLIP_END) {
            // End of frame
            state = STATE_NO_FRAME;
        } else if (byte == SLIP_ESC) {
            state = STATE_ESCAPING;
        } else {
            // TODO: Fill buffer
        }
        break;

    case STATE_ESCAPING:
        state = STATE_IN_FRAME;
        if (byte == SLIP_ESC_END) {
            byte = SLIP_END;
        } else if (byte == SLIP_ESC_ESC) {
            byte = SLIP_ESC;
        } else {
            // Invalid escape, drop byte
            return;
        }
        // TODO: Fill buffer
        break;
    }
}
