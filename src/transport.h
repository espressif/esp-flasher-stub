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
 * @brief Initialize the transport layer
 */
void stub_transport_init(void);

/**
 * @brief UART interrupt handler
 */
void uart_rx_interrupt_handler();

/**
 * @brief USB-Serial/JTAG interrupt handler
 */
void usb_jtag_serial_interrupt_handler();

/**
 * @brief USB-Serial/JTAG transmit one character
 */
uint8_t usb_serial_jtag_tx_one_char(uint8_t c);

#ifdef __cplusplus
}
#endif
