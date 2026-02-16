/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write a 16-bit value to buffer in little-endian format
 *
 * @param buf Buffer to write to
 * @param val Value to write
 */
static inline void set_u16_to_le(uint8_t *buf, uint16_t val)
{
    buf[1] = (uint8_t)((val >> 8) & 0xff);
    buf[0] = (uint8_t)((val >> 0) & 0xff);
}

/**
 * @brief Write a 16-bit value to buffer in big-endian format
 *
 * @param buf Buffer to write to
 * @param val Value to write
 */
static inline void set_u16_to_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)((val >> 8) & 0xff);
    buf[1] = (uint8_t)((val >> 0) & 0xff);
}

/**
 * @brief Write a 32-bit value to buffer in little-endian format
 *
 * @param buf Buffer to write to
 * @param val Value to write
 */
static inline void set_u32_to_le(uint8_t *buf, uint32_t val)
{
    buf[3] = (uint8_t)((val >> 24) & 0xff);
    buf[2] = (uint8_t)((val >> 16) & 0xff);
    buf[1] = (uint8_t)((val >> 8) & 0xff);
    buf[0] = (uint8_t)((val >> 0) & 0xff);
}

/**
 * @brief Read a 16-bit value from buffer in little-endian format
 *
 * @param buf Buffer to read from
 * @return uint16_t The value read
 */
static inline uint16_t get_le_to_u16(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/**
 * @brief Read a 32-bit value from buffer in little-endian format
 *
 * @param buf Buffer to read from
 * @return uint32_t The value read
 */
static inline uint32_t get_le_to_u32(const uint8_t *buf)
{
    return (uint32_t)((uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24));
}

#ifdef __cplusplus
}
#endif
