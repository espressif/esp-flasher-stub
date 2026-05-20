/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register TX function used by SLIP to send bytes
 *
 * @param tx_fn Function pointer with signature: uint8_t (*)(uint8_t)
 */
void slip_set_tx_fn(uint8_t (*tx_fn)(uint8_t));

/**
 * @brief Register flush function called after a complete frame is sent
 *
 * Optional — pass NULL for transports that do not need explicit flushing.
 *
 * @param flush_fn Function pointer with signature: void (*)(void)
 */
void slip_set_flush_fn(void (*flush_fn)(void));

/**
 * @brief Send a SLIP-encoded frame
 *
 * Wraps @p data with SLIP delimiters and byte-stuffing, then flushes.
 * Only used by UART/USB transports — SDIO bypasses this and sends raw frames.
 *
 * @param data Pointer to payload
 * @param size Payload length in bytes
 * @return true on success, false if data is NULL
 */
bool slip_send_frame(const void *data, size_t size);

/**
 * @brief Feed one received byte into the SLIP decoder
 *
 * Call this from the transport RX interrupt for each received byte.
 * The decoded frame is written into the buffer provided by slip_rearm().
 *
 * @param byte Incoming byte from the wire
 */
void slip_recv_byte(uint8_t byte);

/**
 * @brief Provide the next receive buffer to the SLIP decoder
 *
 * Call after frame_buffer_acquire() to hand the new buffer to the ISR.
 * Pass NULL if no buffer is available; the ISR will discard incoming frames
 * until slip_rearm() is called again with a valid buffer.
 *
 * @param buf Buffer pointer from frame_buffer_acquire(), or NULL
 * @param cap Capacity of @p buf in bytes; the decoder marks the frame as an
 *            error rather than overrunning this bound
 */
void slip_rearm(uint8_t *buf, size_t cap);

#ifdef __cplusplus
}
#endif
