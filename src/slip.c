/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stdint.h>
#include <esp-stub-lib/uart.h>
#include <esp-stub-lib/usb_serial_jtag.h>
#include "command_handler.h"
#include "slip.h"

/*
 * Number of buffers for zero-copy operation. Two are taken from old implementation and has around
 * 30 % performance improvement for flashing over USB-Serial/JTAG (depends on the target and flash chip)
 * more buffers might be beneficial for higher transfer speeds (not tested yet).
 */
#define SLIP_NUM_BUFFERS 2

/* SLIP State Machine */
enum slip_state {
    STATE_NO_FRAME,          /* Not in a frame */
    STATE_IN_FRAME,          /* Processing frame data */
    STATE_ESCAPING           /* Processing escape sequence */
};

struct slip_buffer {
    uint8_t buffer[MAX_COMMAND_SIZE] __attribute__((aligned(4)));
    volatile size_t frame_length;
    volatile bool frame_complete;
    volatile bool frame_error;
};

static struct slip_buffer s_buffers[SLIP_NUM_BUFFERS];
static volatile uint8_t s_receiving_buffer = 0;
static volatile uint8_t s_processing_buffer = 0;
static volatile enum slip_state s_state = STATE_NO_FRAME;

/* TX function pointer set by transport init (defaults to UART) */
static uint8_t (*s_tx_one_char)(uint8_t) = stub_lib_uart_tx_one_char;
/* Flush function pointer set by transport init (optional) */
static void (*s_flush_fn)(void) = NULL;

void slip_set_tx_fn(uint8_t (*tx_fn)(uint8_t))
{
    if (tx_fn) {
        s_tx_one_char = tx_fn;
    }
}

void slip_set_flush_fn(void (*flush_fn)(void))
{
    s_flush_fn = flush_fn;
}

void slip_send_frame_delimiter(void)
{
    s_tx_one_char(SLIP_END);
}

void slip_send_frame_data(uint8_t byte)
{
    switch (byte) {
    case SLIP_END:
        s_tx_one_char(SLIP_ESC);
        s_tx_one_char(SLIP_ESC_END);
        break;
    case SLIP_ESC:
        s_tx_one_char(SLIP_ESC);
        s_tx_one_char(SLIP_ESC_ESC);
        break;
    default:
        s_tx_one_char(byte);
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

void slip_flush(void)
{
    if (s_flush_fn) {
        s_flush_fn();
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
    slip_flush();
}

void slip_recv_byte(uint8_t byte)
{
    struct slip_buffer *rx_buf = &s_buffers[s_receiving_buffer];

    /* If current buffer is full, switch to next available buffer */
    if (rx_buf->frame_complete || rx_buf->frame_error) {
        bool found = false;
        for (uint8_t i = 1; i < SLIP_NUM_BUFFERS; i++) {
            uint8_t next_buf = (s_receiving_buffer + i) % SLIP_NUM_BUFFERS;
            if (!s_buffers[next_buf].frame_complete && !s_buffers[next_buf].frame_error) {
                s_receiving_buffer = next_buf;
                rx_buf = &s_buffers[s_receiving_buffer];
                found = true;
                break;
            }
        }

        /* If no available buffer, drop the byte */
        if (!found) {
            return;
        }
    }

    switch (s_state) {
    case STATE_NO_FRAME:
        if (byte == SLIP_END) {
            s_state = STATE_IN_FRAME;
            rx_buf->frame_length = 0;
        }
        break;

    case STATE_IN_FRAME:
        if (byte == SLIP_END) {
            if (rx_buf->frame_length > 0) {
                rx_buf->frame_complete = true;
            }
            s_state = STATE_NO_FRAME;
        } else if (byte == SLIP_ESC) {
            s_state = STATE_ESCAPING;
        } else {
            if (rx_buf->frame_length < MAX_COMMAND_SIZE) {
                rx_buf->buffer[rx_buf->frame_length] = byte;
                ++rx_buf->frame_length;
            } else {
                rx_buf->frame_error = true;
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
            rx_buf->frame_error = true;
            s_state = STATE_NO_FRAME;
            return;
        }

        if (rx_buf->frame_length < MAX_COMMAND_SIZE) {
            rx_buf->buffer[rx_buf->frame_length] = byte;
            ++rx_buf->frame_length;
        } else {
            rx_buf->frame_error = true;
            s_state = STATE_NO_FRAME;
        }
        break;
    }
}

bool slip_is_frame_complete(void)
{
    for (uint8_t i = 0; i < SLIP_NUM_BUFFERS; i++) {
        if (s_buffers[i].frame_complete) {
            return true;
        }
    }
    return false;
}

bool slip_is_frame_error(void)
{
    return s_buffers[s_processing_buffer].frame_error;
}

int slip_get_frame_state(void)
{
    /* Check all buffers for complete or error frames */
    for (uint8_t i = 0; i < SLIP_NUM_BUFFERS; i++) {
        if (s_buffers[i].frame_error) {
            s_processing_buffer = i;
            return SLIP_STATE_ERROR;
        }

        if (s_buffers[i].frame_complete) {
            s_processing_buffer = i;
            return SLIP_STATE_COMPLETE;
        }
    }

    return SLIP_STATE_IDLE;
}

const uint8_t *slip_get_frame_data(size_t *length)
{
    if (length) {
        *length = s_buffers[s_processing_buffer].frame_length;
    }
    return s_buffers[s_processing_buffer].buffer;
}

void slip_recv_reset(void)
{
    s_buffers[s_processing_buffer].frame_length = 0;
    s_buffers[s_processing_buffer].frame_complete = false;
    s_buffers[s_processing_buffer].frame_error = false;
}
