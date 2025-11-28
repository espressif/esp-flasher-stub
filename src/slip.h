/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
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
 * This function should be called for each received byte from the interrupt.
 * It will decode the SLIP protocol and fill an internal buffer.
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
 * @brief Get pointer to received frame data
 *
 * Only call this when slip_is_frame_complete() returns true.
 *
 * @param length Pointer to store the frame length
 * @return Pointer to frame data buffer
 */
const uint8_t* slip_get_frame_data(size_t *length);

/**
 * @brief Reset receive state for next frame
 *
 * Call this after processing a complete frame or to clear an error.
 */
void slip_recv_reset(void);

#ifdef __cplusplus
}
#endif
