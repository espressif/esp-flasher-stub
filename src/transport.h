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
    TRANSPORT_UART = 0,
    TRANSPORT_USB_OTG = 1,
    TRANSPORT_USB_SERIAL_JTAG = 2,
    TRANSPORT_SDIO = 3,
};

/**
 * @brief Transport receive/transmit operations
 *
 * The receive buffering (single vs. double, DMA vs. byte ISR) is fully
 * private to each transport implementation — callers only poll and release.
 */
struct stub_transport_ops {
    /**
     * Poll for the next complete received frame.
     *
     * @param len   Receives the frame length when a frame is returned.
     * @param error Set true if the last reception failed (overflow / bad
     *              escape); the frame pointer is NULL in that case.
     * @return Pointer to the frame payload, or NULL when no frame is ready
     *         or an error occurred.
     *
     * When a frame (or error) is returned the transport has already armed
     * the spare receive buffer, so reception continues while the caller runs
     * handle_command — required because commands such as FLASH_DATA send
     * their response early and the host immediately streams the next frame.
     */
    const uint8_t *(*recv_poll)(size_t *len, bool *error);

    /**
     * Release the frame returned by the most recent recv_poll().
     * Idempotent; never affects the buffer currently being received into.
     */
    void (*recv_release)(void);

    bool (*send_frame)(const void *data, size_t len);
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
const struct stub_transport_ops *stub_transport_init(int transport);

/**
 * @brief UART interrupt handler
 */
void uart_rx_interrupt_handler();

/**
 * @brief USB-Serial/JTAG interrupt handler
 */
void usb_serial_jtag_rx_interrupt_handler();

#ifdef __cplusplus
}
#endif
