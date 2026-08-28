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
 * @brief Register flush function called at the end of every frame
 *
 * Optional — pass NULL for transports that do not need explicit flushing.
 * Called mid-frame as well, whenever slip_set_flush_interval() is in effect
 * and that many bytes have piled up since the last flush.
 *
 * @param flush_fn Function pointer with signature: void (*)(void)
 */
void slip_set_flush_fn(void (*flush_fn)(void));

/**
 * @brief Force a TX flush every @p bytes bytes, on top of the end-of-frame one
 *
 * The USB-Serial/JTAG IN endpoint sends a packet on its own as soon as its
 * 64-byte FIFO fills, so a frame whose wire length is an exact multiple of 64
 * ends on a full-size packet. USB treats a full-size packet as "more to come",
 * so the host driver holds those last 64 bytes waiting for a short packet that
 * never arrives, while the stub waits for the acknowledgement the host cannot
 * send.
 *
 * stub_lib_usb_serial_jtag_tx_flush() recovers from that after the fact: it
 * waits for the FIFO to become writable so that WR_DONE emits a zero-length
 * packet, which terminates the transfer. That wait gives up after 50 ms, and a
 * WR_DONE issued before the FIFO is writable is a no-op, so the recovery can
 * fail silently. Flushing before the FIFO can fill keeps every packet short,
 * so no transfer needs terminating and the frame no longer depends on the
 * zero-length packet.
 *
 * @param bytes Maximum bytes between flushes; 0 disables early flushing
 */
void slip_set_flush_interval(size_t bytes);

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
