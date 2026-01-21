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
 * @brief Command context — parsed SLIP frame header, passed to handlers and post-process functions.
 *
 * The dispatcher fills this struct before calling a handler; the same pointer is
 * forwarded to the post-process callback (if registered) immediately after the
 * response frame is sent.
 */
struct cmd_ctx {
    uint8_t command;
    uint8_t direction;
    uint16_t packet_size;
    uint32_t checksum;
    const uint8_t *data;
};

/**
 * @brief Response payload populated by command handlers (and plugin handlers).
 *
 * Handler contract:
 *   1. Fill @p value / @p data / @p data_size for the response payload.
 *   2. Optionally set @p post_process to a callback that runs after the
 *      dispatcher sends the response frame (e.g. for streaming reads).
 *      Set to NULL when no post-processing is needed.
 *   3. Return a status code (RESPONSE_SUCCESS or an error code).
 *
 * Handlers MUST NOT call SLIP functions directly for the primary response frame —
 * the dispatcher owns framing via s_send_response(). Streaming frames emitted
 * inside a post_process callback may still use slip_send_frame() directly.
 */
struct command_response_data {
    uint32_t value;                        /**< 4-byte value field in the response header */
    uint8_t  data[MAX_RESPONSE_DATA_SIZE]; /**< Optional inline payload */
    uint16_t data_size;                    /**< Number of valid bytes in data[] */
    int (*post_process)(const struct cmd_ctx *ctx); /**< Post-process callback, or NULL */
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
