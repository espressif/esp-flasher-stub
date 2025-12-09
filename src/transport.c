/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stdint.h>
#include <esp-stub-lib/uart.h>
#include <esp-stub-lib/usb_serial_jtag.h>
#include "transport.h"
#include "slip.h"

void uart_rx_interrupt_handler()
{
    // This also resets the interrupt flags
    uint32_t intr_flags = stub_lib_uart_clear_intr_flags(UART_NUM_0);

    if ((intr_flags & UART_INTR_RXFIFO_FULL) || (intr_flags & UART_INTR_RXFIFO_TOUT)) {
        uint32_t count = stub_lib_uart_get_rxfifo_count(UART_NUM_0);

        for (uint32_t i = 0; i < count; ++i) {
            uint8_t byte = stub_lib_uart_read_rxfifo_byte(UART_NUM_0);
            slip_recv_byte(byte);

            // Cannot process more bytes until frame is processed
            if (slip_is_frame_complete() || slip_is_frame_error()) {
                break;
            }
        }
    }
}

void usb_jtag_serial_interrupt_handler()
{
    stub_lib_usb_serial_jtag_clear_intr_flags();

    while (stub_lib_usb_serial_jtag_is_data_available()) {
        slip_recv_byte(stub_lib_usb_serial_jtag_read_rxfifo_byte());
    }
}

uint8_t usb_serial_jtag_tx_one_char(uint8_t c)
{
    // Flush every 63 bytes (some Windows drivers have issues with >= 64 bytes)
    // or when a frame delimiter is sent
    // TODO: Proper fix with zero-length packets
    static unsigned short transferred_without_flush = 0;
    stub_lib_usb_serial_jtag_tx_one_char(c);
    ++transferred_without_flush;
    if (c == SLIP_END || transferred_without_flush >= 63) {
        stub_lib_usb_serial_jtag_tx_flush();
        transferred_without_flush = 0;
    }
    return 0;
}

void stub_transport_init(void)
{
    if (stub_lib_usb_serial_jtag_is_active()) {
        stub_lib_usb_serial_jtag_rominit_intr_attach(17, usb_jtag_serial_interrupt_handler, USB_SERIAL_JTAG_OUT_RECV_PKT_INT_ENA);
        slip_set_tx_fn(usb_serial_jtag_tx_one_char);
    } else {
        stub_lib_uart_wait_idle(UART_NUM_0); // Wait until ROM sends response to last command
        stub_lib_uart_rominit_intr_attach(UART_NUM_0, 5, uart_rx_interrupt_handler, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
        slip_set_tx_fn(stub_lib_uart_tx_one_char);
    }
}
