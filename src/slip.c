/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stdint.h>
#include <esp-stub-lib/uart.h>
#include "command_handler.h"
#include "slip.h"

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

/* Internal SLIP receive context */
typedef struct {
    uint8_t *buffer;        /* Buffer to store decoded frame */
    size_t buffer_size;     /* Maximum buffer size */
    size_t frame_length;    /* Current frame length */
    bool frame_complete;    /* True when complete frame received */
    bool frame_error;       /* True if frame had errors */
} slip_recv_ctx_t;

/* Internal receive buffer and context */
static uint8_t s_recv_buffer[MAX_COMMAND_SIZE] __attribute__((aligned(4)));
static slip_recv_ctx_t s_recv_ctx = {
    .buffer = s_recv_buffer,
    .buffer_size = sizeof(s_recv_buffer),
    .frame_length = 0,
    .frame_complete = false,
    .frame_error = false
};

static slip_state_t s_state = STATE_NO_FRAME;

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
    // If frame is already complete or has error, ignore new bytes until reset
    if (s_recv_ctx.frame_complete || s_recv_ctx.frame_error) {
        return;
    }
    switch (s_state) {
    case STATE_NO_FRAME:
        if (byte == SLIP_END) {
            // Start new frame
            s_state = STATE_IN_FRAME;
            s_recv_ctx.frame_length = 0;
        }
        break;

    case STATE_IN_FRAME:
        if (byte == SLIP_END) {
            // End of frame (only if we have data)
            if (s_recv_ctx.frame_length > 0) {
                s_recv_ctx.frame_complete = true;
            }
            s_state = STATE_NO_FRAME;
        } else if (byte == SLIP_ESC) {
            s_state = STATE_ESCAPING;
        } else {
            // Add byte to buffer
            if (s_recv_ctx.frame_length < s_recv_ctx.buffer_size) {
                s_recv_ctx.buffer[s_recv_ctx.frame_length] = byte;
                ++s_recv_ctx.frame_length;
            } else {
                // Buffer overflow
                s_recv_ctx.frame_error = true;
                s_state = STATE_NO_FRAME;
            }
        }
        break;

    case STATE_ESCAPING:
        s_state = STATE_IN_FRAME;
        if (byte == SLIP_ESC_END) {
            byte = SLIP_END;
        } else if (byte == SLIP_ESC_ESC) {
            byte = SLIP_ESC;
        } else {
            // Invalid escape sequence, mark error and reset
            s_recv_ctx.frame_error = true;
            s_state = STATE_NO_FRAME;
            return;
        }

        // Add escaped byte to buffer
        if (s_recv_ctx.frame_length < s_recv_ctx.buffer_size) {
            s_recv_ctx.buffer[s_recv_ctx.frame_length] = byte;
            ++s_recv_ctx.frame_length;
        } else {
            // Buffer overflow
            s_recv_ctx.frame_error = true;
            s_state = STATE_NO_FRAME;
        }
        break;
    }
}

bool slip_is_frame_complete(void)
{
    return s_recv_ctx.frame_complete;
}

bool slip_is_frame_error(void)
{
    return s_recv_ctx.frame_error;
}

const uint8_t* slip_get_frame_data(size_t *length)
{
    if (length) {
        *length = s_recv_ctx.frame_length;
    }
    return s_recv_ctx.buffer;
}

void slip_recv_reset(void)
{
    s_recv_ctx.frame_length = 0;
    s_recv_ctx.frame_complete = false;
    s_recv_ctx.frame_error = false;
    s_state = STATE_NO_FRAME;
}
