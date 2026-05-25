/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
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

/* Maximum frame size: 8-byte SLIP header + esptool max data payload (WRITE_FLASH uses 0x4000+0xFF). */
#define FRAME_BUFFER_SIZE (8U + 0x4000U + 0xFFU)

enum frame_buffer_state {
    FRAME_BUFFER_STATE_IDLE,
    FRAME_BUFFER_STATE_COMPLETE,
    FRAME_BUFFER_STATE_ERROR,
};

void frame_buffer_mark_error(void);

uint8_t *frame_buffer_acquire(void);
void frame_buffer_mark_complete(size_t len);

enum frame_buffer_state frame_buffer_get_state(void);
const uint8_t *frame_buffer_get_data(size_t *length);
void frame_buffer_reset(void);

#ifdef __cplusplus
}
#endif
