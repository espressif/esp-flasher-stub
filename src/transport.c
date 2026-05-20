/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include <esp-stub-lib/uart.h>
#include <esp-stub-lib/usb_serial_jtag.h>
#include <esp-stub-lib/usb_otg.h>
#include <esp-stub-lib/clock.h>
#include <esp-stub-lib/rom_wrappers.h>
#include "transport.h"
#include "frame_buffer.h"
#include "slip.h"

#define USB_INTERRUPT_SOURCE  17
#define UART_INTERRUPT_SOURCE 5

/* ---- Interrupt handlers -------------------------------------------------- */

void uart_rx_interrupt_handler()
{
    uint32_t intr_flags = stub_lib_uart_clear_intr_flags(UART_NUM_0);

    if ((intr_flags & UART_INTR_RXFIFO_FULL) || (intr_flags & UART_INTR_RXFIFO_TOUT)) {
        uint32_t count = stub_lib_uart_get_rxfifo_count(UART_NUM_0);
        for (uint32_t i = 0; i < count; ++i) {
            slip_recv_byte(stub_lib_uart_read_rxfifo_byte(UART_NUM_0));
        }
    }
}

void usb_serial_jtag_rx_interrupt_handler()
{
    stub_lib_usb_serial_jtag_clear_intr_flags();

    while (stub_lib_usb_serial_jtag_is_data_available()) {
        slip_recv_byte(stub_lib_usb_serial_jtag_read_rxfifo_byte());
    }
}

/* ---- SLIP-based transport ops (UART / USB-OTG / USB-Serial-JTAG) --------- */

static void slip_do_rearm(void)
{
    slip_rearm(frame_buffer_acquire(), FRAME_BUFFER_SIZE);
}

static const uint8_t *slip_recv_poll(size_t *len, bool *error)
{
    *error = false;

    /* get_state() selects the proc buffer; acquire() (inside slip_do_rearm)
     * then switches the RX buffer to the spare; get_data() reads proc. */
    enum frame_buffer_state state = frame_buffer_get_state();
    if (state == FRAME_BUFFER_STATE_IDLE) {
        return NULL;
    }
    slip_do_rearm();
    if (state == FRAME_BUFFER_STATE_ERROR) {
        *error = true;
        return NULL;
    }
    return frame_buffer_get_data(len);
}

static const struct stub_transport_ops s_slip_ops = {
    .recv_poll    = slip_recv_poll,
    .recv_release = frame_buffer_reset,
    .send_frame   = slip_send_frame,
};

/* ---- Detection & initialisation ------------------------------------------ */

int stub_transport_detect(void)
{
    if (stub_lib_usb_otg_is_active()) {
        return TRANSPORT_USB_OTG;
    }
    if (stub_lib_usb_serial_jtag_is_active()) {
        return TRANSPORT_USB_SERIAL_JTAG;
    }
    return TRANSPORT_UART;
}

const struct stub_transport_ops *stub_transport_init(int transport)
{
    switch (transport) {
    case TRANSPORT_USB_OTG:
        stub_lib_usb_otg_rominit_intr_attach(USB_INTERRUPT_SOURCE, slip_recv_byte);
        slip_set_tx_fn(stub_lib_usb_otg_tx_one_char);
        slip_set_flush_fn(stub_lib_usb_otg_tx_flush);
        slip_do_rearm();
        return &s_slip_ops;

    case TRANSPORT_USB_SERIAL_JTAG:
        stub_lib_clock_disable_watchdogs();
        stub_lib_usb_serial_jtag_rominit_intr_attach(USB_INTERRUPT_SOURCE,
                                                     usb_serial_jtag_rx_interrupt_handler,
                                                     USB_SERIAL_JTAG_OUT_RECV_PKT_INT_ENA);
        slip_set_tx_fn(stub_lib_usb_serial_jtag_tx_one_char);
        slip_set_flush_fn(stub_lib_usb_serial_jtag_tx_flush);
        slip_do_rearm();
        return &s_slip_ops;

    case TRANSPORT_UART:
    default:
        // Wait for 10ms to ensure ROM has sent response to last command
        stub_lib_delay_us(10 * 1000);
        stub_lib_uart_wait_idle(UART_NUM_0);
        stub_lib_uart_rominit_intr_attach(UART_NUM_0, UART_INTERRUPT_SOURCE,
                                          uart_rx_interrupt_handler,
                                          UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
        slip_set_tx_fn(stub_lib_uart_tx_one_char);
        slip_set_flush_fn(NULL);
        slip_do_rearm();
        return &s_slip_ops;
    }
}
