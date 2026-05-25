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
#include <esp-stub-lib/usb_otg.h>
#include "command_handler.h"
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

    const int transport = stub_transport_detect();

    // stub_lib_clock_init() increases CPU frequency which benefits both USB and UART transfers.
    // Currently only enabled for USB transfers due to concerns about instability (observed on ESP32-S3),
    // because DBIAS voltage not being set. This needs investigation and potentially enabling for all transport types.
    if (transport == TRANSPORT_USB_OTG || transport == TRANSPORT_USB_SERIAL_JTAG || transport == TRANSPORT_SDIO) {
        stub_lib_clock_init();
    }

    stub_lib_flash_init(NULL);
    stub_lib_flash_attach(0, false);
    const struct stub_transport_ops *ops = stub_transport_init(transport);

    // Send OHAI greeting to signal stub is active
    const uint8_t greeting[4] __attribute__((aligned(4))) = {'O', 'H', 'A', 'I'};
    ops->send_frame(&greeting, sizeof(greeting));

    for (;;) {
        size_t frame_length;
        bool frame_error;
        const uint8_t *frame_data = ops->recv_poll(&frame_length, &frame_error);

        if (frame_error) {
            ops->recv_release();
            continue;
        }

        if (frame_data != NULL) {
            handle_command(frame_data, frame_length, ops);
            // Redundant for post-process commands (e.g. READ_FLASH) that release
            // the command frame themselves; recv_release is idempotent and never
            // touches the buffer being received into, so this is safe.
            ops->recv_release();
            continue;
        }

        // Handle chip reset requests via CDC-ACM RTS line toggling in USB-OTG mode
        if (transport == TRANSPORT_USB_OTG && stub_lib_usb_otg_is_reset_requested()) {
            stub_lib_usb_otg_handle_reset();
        }
    }

    // Cleanup
    stub_lib_flash_deinit(NULL);
}
