/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 0x4000 plus 0xFF is the maximum data size sent by esptool (WRITE_FLASH command), so keeping for the compatibility
#define ESPTOOL_MAX_DATA_SIZE (0x4000U + 0xFFU)
#define HEADER_SIZE 8U
#define MAX_COMMAND_SIZE (HEADER_SIZE + ESPTOOL_MAX_DATA_SIZE)

/** Maximum number of extra data bytes a command response may carry. */
#define MAX_RESPONSE_DATA_SIZE 64U

/**
 * @brief Response payload populated by command handlers (and plugin handlers).
 *
 * Handlers fill this struct and return an esp_response_code status value.
 * The dispatcher owns SLIP framing and calls s_send_response() after the
 * handler returns.
 */
struct command_response_data {
    uint32_t value;                        /**< 4-byte value field in the response header */
    uint8_t  data[MAX_RESPONSE_DATA_SIZE]; /**< Optional inline payload */
    uint16_t data_size;                    /**< Number of valid bytes in data[] */
};

/**
 * @brief Main command handler for ESP flasher stub
 *
 * Function-based approach without classes for minimal flash footprint.
 *
 * @param buffer Buffer containing the command
 * @param len Length of the buffer
 */

void handle_command(const uint8_t *buffer, size_t len);

#ifdef __cplusplus
}
#endif
