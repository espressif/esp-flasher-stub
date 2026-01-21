/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SPI2 peripheral for NAND flash communication
 * @return 0 on success, negative on error
 */
int spi2_init(void);

/**
 * @brief Send command and optionally address/data over SPI2
 * @param cmd Command byte
 * @param addr Address bytes (can be NULL if addr_bits is 0)
 * @param addr_bits Number of address bits (0, 8, 16, or 24)
 * @param tx_data Data to transmit (can be NULL if tx_bits is 0)
 * @param tx_bits Number of data bits to transmit
 * @param rx_data Buffer to receive data (can be NULL if rx_bits is 0)
 * @param rx_bits Number of data bits to receive
 * @return 0 on success, negative on error
 */
int spi2_transaction(uint8_t cmd,
                     const uint8_t *addr, uint8_t addr_bits,
                     const uint8_t *tx_data, uint16_t tx_bits,
                     uint8_t *rx_data, uint16_t rx_bits);

#ifdef __cplusplus
}
#endif
