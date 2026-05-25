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
#include <esp-stub-lib/err.h>
#include <esp-stub-lib/sdio.h>
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

/* ---- SDIO transport ops -------------------------------------------------- */
/*
 * SDIO bypasses SLIP entirely; the DMA writes a whole frame into the armed
 * buffer and the ISR latches its length for stub_lib_sdio_take_rx_frame().
 *
 * The driver disarms its receive DMA not only after delivering a frame but
 * also on no-frame paths (empty descriptor chain / TX_DSCR_EMPTY in
 * sdio_slc_isr). Those disarms are invisible to frame_buffer, so recv_poll
 * MUST re-arm whenever it finds no pending frame — otherwise the link
 * silently dies. stub_lib_sdio_rearm() is a cheap no-op while still armed.
 *
 * Ordering constraint: stub_lib_sdio_rearm() fails while a received frame is
 * still pending, so take_rx_frame() (which clears that state) must run first.
 * On the no-frame path take_rx_frame() returned false, so nothing is pending
 * and the re-arm is safe.
 */

static void sdio_arm(void)
{
    /* A NULL buffer means both frame buffers are occupied, which cannot
     * happen under the lock-step + single early-ack protocol; if it ever
     * did, RX would stall on the next frame (no silent corruption). */
    stub_lib_sdio_rearm(frame_buffer_acquire(), FRAME_BUFFER_SIZE);
}

static const uint8_t *sdio_recv_poll(size_t *len, bool *error)
{
    *error = false;

    enum frame_buffer_state state = frame_buffer_get_state();
    if (state == FRAME_BUFFER_STATE_IDLE) {
        size_t n;
        if (!stub_lib_sdio_take_rx_frame(&n)) {
            sdio_arm();
            return NULL;
        }
        frame_buffer_mark_complete(n);   /* frame already DMA'd into proc buf */
        state = frame_buffer_get_state();
        sdio_arm();                      /* take done → switch to spare + arm  */
    }
    /* state != IDLE: a prior frame awaits release; its spare is already armed
     * and may already hold an un-taken frame, so do NOT re-arm here. */
    if (state == FRAME_BUFFER_STATE_ERROR) {
        *error = true;
        return NULL;
    }
    return frame_buffer_get_data(len);
}

static bool sdio_send_frame(const void *data, size_t len)
{
    return stub_lib_sdio_tx_frame(data, len) == STUB_LIB_OK;
}

static const struct stub_transport_ops s_sdio_ops = {
    .recv_poll    = sdio_recv_poll,
    .recv_release = frame_buffer_reset,
    .send_frame   = sdio_send_frame,
};

/* ---- Detection & initialisation ------------------------------------------ */

int stub_transport_detect(void)
{
    /* SDIO must be checked before USB: ROM may leave USB registers in an
     * ambiguous state after an SDIO boot. */
    if (stub_lib_sdio_is_active()) {
        return TRANSPORT_SDIO;
    }
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
    case TRANSPORT_SDIO:
        stub_lib_sdio_init();
        /* Arm the first DMA receive buffer immediately after init. */
        sdio_arm();
        return &s_sdio_ops;

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
