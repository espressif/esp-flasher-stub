/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SLIP Protocol Constants */
#define SLIP_END            0xC0    /* Frame delimiter */
#define SLIP_ESC            0xDB    /* Escape character */
#define SLIP_ESC_END        0xDC    /* Escaped frame delimiter */
#define SLIP_ESC_ESC        0xDD    /* Escaped escape character */

enum slip_frame_state {
    SLIP_STATE_IDLE,         /* No frame processing */
    SLIP_STATE_COMPLETE,     /* Frame complete and ready */
    SLIP_STATE_ERROR,        /* Frame error occurred */
};

/**
 * @brief Register TX function used by SLIP to send bytes
 *
 * The function must transmit a single byte and return 0 on success.
 *
 * @param tx_fn Function pointer with signature: uint8_t (*)(uint8_t)
 */
void slip_set_tx_fn(uint8_t (*tx_fn)(uint8_t));

/**
 * @brief Register flush function used by SLIP to flush TX buffer
 *
 * The function is called after a complete SLIP frame has been sent.
 * Optional - set to NULL for transports that don't require flushing.
 *
 * @param flush_fn Function pointer with signature: void (*)(void)
 */
void slip_set_flush_fn(void (*flush_fn)(void));

/**
 * @brief Flush TX buffer
 *
 * Calls the registered flush function. This is automatically invoked by
 * slip_send_frame() after sending a complete frame.
 */
void slip_flush(void);

/**
 * @brief Send SLIP frame delimiter
 */
void slip_send_frame_delimiter(void);

/**
 * @brief Send single byte with SLIP escaping
 *
 * @param byte Byte to send
 */
void slip_send_frame_data(uint8_t byte);

/**
 * @brief Send buffer with SLIP escaping
 *
 * @param data Data buffer to send
 * @param size Size of data in bytes
 */
void slip_send_frame_data_buf(const void *data, size_t size);

/**
 * @brief Send complete SLIP frame
 *
 * @param data Data to send
 * @param size Size of data in bytes
 */
void slip_send_frame(const void *data, size_t size);

/**
 * @brief Process incoming byte through SLIP decoder
 *
 * This function should be called for each received byte.
 * Uses multi-buffering (configurable via SLIP_NUM_BUFFERS) for zero-copy operation.
 * Automatically switches to next available buffer when current frame completes.
 *
 * @param byte Incoming byte
 */
void slip_recv_byte(uint8_t byte);

/**
 * @brief Check if a complete frame has been received
 *
 * @return true if a complete frame is ready to process
 */
bool slip_is_frame_complete(void);

/**
 * @brief Check if a frame error occurred
 *
 * @return true if an error occurred (buffer overflow, invalid escape sequence)
 */
bool slip_is_frame_error(void);

/**
 * @brief Query frame state and get data/error info
 *
 * @return SLIP_STATE_COMPLETE, SLIP_STATE_ERROR, or SLIP_STATE_IDLE
 */
int slip_get_frame_state(void);

/**
 * @brief Get pointer to frame data (ZERO-COPY)
 *
 * Returns direct pointer to frame buffer - no memcpy needed!
 * Pointer remains valid until slip_recv_reset() is called.
 *
 * @param length Pointer to store the frame length (can be NULL)
 * @return Pointer to frame data buffer, or NULL if no frame available
 */
const uint8_t *slip_get_frame_data(size_t *length);

/**
 * @brief Reset receive state for next frame
 *
 * Switches to the other buffer and allows reception to continue.
 * Call this after processing a complete frame or to clear an error.
 */
void slip_recv_reset(void);

#ifdef __cplusplus
}
#endif
