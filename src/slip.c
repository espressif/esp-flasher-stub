/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <esp-stub-lib/uart.h>
#include "frame_buffer.h"
#include "slip.h"

/* SLIP wire encoding constants — private to this file */
#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

/* RX state machine */
enum slip_rx_state {
    STATE_NO_FRAME,
    STATE_IN_FRAME,
    STATE_ESCAPING,
};

static volatile enum slip_rx_state s_state = STATE_NO_FRAME;
static uint8_t *volatile s_rx_buf;
static volatile size_t s_rx_len;
static volatile size_t s_rx_cap;

/* TX hooks — set once during transport init */
static uint8_t (*s_tx_one_char)(uint8_t) = stub_lib_uart_tx_one_char;
static void (*s_flush_fn)(void) = NULL;

/* Bytes written since the last flush, and the limit that forces an early one.
 * Zero disables the limit, leaving the end-of-frame flush as the only one. */
static size_t s_flush_interval;
static size_t s_unflushed;

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

void slip_set_flush_interval(size_t bytes)
{
    s_flush_interval = bytes;
    s_unflushed = 0;
}

static void slip_flush(void)
{
    s_unflushed = 0;
    if (s_flush_fn) {
        s_flush_fn();
    }
}

/*
 * Send one raw byte, flushing early once s_flush_interval bytes have piled up
 * since the last flush. See slip_set_flush_interval() for why that matters.
 */
static void send_byte(uint8_t byte)
{
    s_tx_one_char(byte);
    if (s_flush_interval != 0) {
        s_unflushed++;
        if (s_unflushed >= s_flush_interval) {
            slip_flush();
        }
    }
}

static void send_escaped_byte(uint8_t byte)
{
    switch (byte) {
    case SLIP_END:
        send_byte(SLIP_ESC);
        send_byte(SLIP_ESC_END);
        break;
    case SLIP_ESC:
        send_byte(SLIP_ESC);
        send_byte(SLIP_ESC_ESC);
        break;
    default:
        send_byte(byte);
        break;
    }
}

bool slip_send_frame(const void *data, size_t size)
{
    if (!data) {
        return false;
    }
    send_byte(SLIP_END);
    const uint8_t *buf = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++) {
        send_escaped_byte(buf[i]);
    }
    send_byte(SLIP_END);
    slip_flush();
    return true;
}

/*
 * Append one decoded byte to the active RX buffer. Overrunning the buffer
 * marks the frame as an error and aborts reception — without this guard a
 * host frame larger than the buffer would corrupt memory from inside the ISR.
 */
static void rx_put(uint8_t byte)
{
    if (s_rx_len < s_rx_cap) {
        s_rx_buf[s_rx_len++] = byte;
    } else {
        frame_buffer_mark_error();
        s_rx_buf = NULL;
        s_state = STATE_NO_FRAME;
    }
}

void slip_recv_byte(uint8_t byte)
{
    switch (s_state) {
    case STATE_NO_FRAME:
        if (byte == SLIP_END && s_rx_buf) {
            s_rx_len = 0;
            s_state = STATE_IN_FRAME;
        }
        break;

    case STATE_IN_FRAME:
        if (byte == SLIP_END) {
            if (s_rx_len > 0) {
                frame_buffer_mark_complete(s_rx_len);
                s_rx_buf = NULL;    /* main loop rearmed via slip_recv_poll */
            }
            /* else: zero-length frame (C0 C0 gap) — keep buffer armed so
             * the ISR can immediately start the next real frame. */
            s_rx_len = 0;
            s_state = STATE_NO_FRAME;
        } else if (byte == SLIP_ESC) {
            s_state = STATE_ESCAPING;
        } else {
            rx_put(byte);
        }
        break;

    case STATE_ESCAPING:
        s_state = STATE_IN_FRAME;
        if (byte == SLIP_ESC_END) {
            rx_put(SLIP_END);
        } else if (byte == SLIP_ESC_ESC) {
            rx_put(SLIP_ESC);
        } else {
            frame_buffer_mark_error();
            s_rx_buf = NULL;
            s_state = STATE_NO_FRAME;
        }
        break;
    }
}

void slip_rearm(uint8_t *buf, size_t cap)
{
    s_rx_buf = buf;
    s_rx_cap = cap;
}
