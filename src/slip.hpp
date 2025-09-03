/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

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
 * @param byte Incoming byte
 */
void slip_recv_byte(uint8_t byte);

#ifdef __cplusplus
}
#endif
