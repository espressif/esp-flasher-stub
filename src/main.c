/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <esp-stub-lib/flash.h>
#include <esp-stub-lib/uart.h>
#include "slip.h"
#include "command_handler.h"

#ifdef ESP8266
__asm__(
    ".global esp_main_esp8266\n"
    ".literal_position\n"
    ".align 4\n"
    "esp_main_esp8266:\n"
    "movi a0, 0x400010a8;"
    "j esp_main;");
#endif //ESP8266

static void uart_rx_interrupt_handler()
{
    // This also resets the interrupt flags
    uint32_t intr_flags = stub_lib_uart_get_intr_flags(UART_NUM_0);

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

void esp_main(void)
{
    extern uint32_t _bss_start;
    extern uint32_t _bss_end;

    /* Zero BSS section */
    for (uint32_t *p = &_bss_start; p < &_bss_end; p++) {
        *p = 0;
    }

    void *flash_state = NULL;
    stub_lib_flash_init(&flash_state);
    stub_lib_flash_attach(0, false);

    stub_lib_uart_wait_idle(UART_NUM_0); // Wait until ROM sends response to last command
    stub_lib_uart_rominit_intr_attach(UART_NUM_0, 5, uart_rx_interrupt_handler, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);

    // Send OHAI greeting to signal stub is active
    const uint8_t greeting[4] = {'O', 'H', 'A', 'I'};
    slip_send_frame(&greeting, sizeof(greeting));

    for (;;) {
        if (slip_is_frame_complete()) {
            size_t frame_length;
            const uint8_t *frame_data = slip_get_frame_data(&frame_length);
            handle_command(frame_data, frame_length);
            slip_recv_reset();
        }

        if (slip_is_frame_error()) {
            slip_recv_reset();
        }
    }

    // Cleanup
    if (flash_state) {
        stub_lib_flash_deinit(flash_state);
    }
}
