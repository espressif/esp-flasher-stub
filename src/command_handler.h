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

// 0x4000 plus 0xFF is the maximum data size sent by esptool (WRITE_FLASH command), so keeping for the compatibility
#define ESPTOOL_MAX_DATA_SIZE (0x4000U + 0xFFU)
#define HEADER_SIZE 8U
#define MAX_COMMAND_SIZE (HEADER_SIZE + ESPTOOL_MAX_DATA_SIZE)

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
