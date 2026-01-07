/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <esp-stub-lib/flash.h>
#include <esp-stub-lib/clock.h>
#include <esp-stub-lib/usb_serial_jtag.h>
#include "command_handler.h"
#include "slip.h"
#include "transport.h"

#ifdef ESP8266
__asm__(
    ".global esp_main_esp8266\n"
    ".literal_position\n"
    ".align 4\n"
    "esp_main_esp8266:\n"
    "movi a0, 0x400010a8;"
    "j esp_main;");
#endif //ESP8266

void esp_main(void)
{
    extern uint32_t _bss_start;
    extern uint32_t _bss_end;

    /* Zero BSS section */
    for (uint32_t *p = &_bss_start; p < &_bss_end; p++) {
        *p = 0;
    }

    // stub_lib_clock_init() increases CPU frequency which benefits both USB and UART transfers.
    // Currently only enabled for USB-Serial/JTAG due to concerns about instability (observed on ESP32-S3),
    // because DBIAS voltage not being set. This needs investigation and potentially enabling for all transport types.
    if (stub_lib_usb_serial_jtag_is_active()) {
        stub_lib_clock_init();
    }

    void *flash_state = NULL;
    stub_lib_flash_init(&flash_state);
    stub_lib_flash_attach(0, false);

    stub_transport_init();

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
