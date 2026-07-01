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

/* Maximum frame size: 8-byte SLIP header + esptool max data payload + SLIP slack.
 * Flash block enlarged to 32KB (fewer round-trips); run esptool with FLASH_WRITE_SIZE=0x8000
 * to match. The double buffer must fit the stub's DRAM window (see the target linker script).
 * Exception: esp8266's DRAM window is too small for a 2x32KB double buffer, so it stays at
 * 16KB (FLASH_WRITE_SIZE=0x4000). ESP8266 is a build-time define (-DESP8266, see cmake). */
#ifdef ESP8266
#define FRAME_BUFFER_SIZE (8U + 0x4000U + 0xFFU)   /* 16KB: esp8266 DRAM too small for a 2x32KB double buffer */
#else
#define FRAME_BUFFER_SIZE (8U + 0x8000U + 0xFFU)   /* 32KB: fewer round-trips */
#endif

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
