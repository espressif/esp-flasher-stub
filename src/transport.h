/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
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
 * @brief Supported transport types
 */
enum stub_transport_type {
    STUB_TRANSPORT_UART = 0,
    STUB_TRANSPORT_USB_OTG = 1,
    STUB_TRANSPORT_USB_SERIAL_JTAG = 2,
};

/**
 * @brief Detect which transport is active (selected by ROM)
 *
 * This should be called once at startup and the result reused, to avoid repeated
 * USB transport probing.
 */
int stub_transport_detect(void);

/**
 * @brief Initialize the transport layer
 */
void stub_transport_init(int transport);

/**
 * @brief UART interrupt handler
 */
void uart_rx_interrupt_handler();

/**
 * @brief USB-Serial/JTAG interrupt handler
 */
void usb_serial_jtag_rx_interrupt_handler();

/**
 * @brief USB-Serial/JTAG transmit one character
 */
uint8_t usb_serial_jtag_tx_one_char(uint8_t c);

/**
 * @brief USB-OTG transmit one character
 */
uint8_t usb_otg_tx_one_char(uint8_t c);

#ifdef __cplusplus
}
#endif
