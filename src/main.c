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
#include <esp-stub-lib/uart.h>
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
    // Upstream enables it only for USB transports: the ESP32-S3 UART path was unstable because
    // DBIAS was not set. ESP32's stub_target_clock_init() DOES set DBIAS (RTC_CNTL_DBIAS_1V25),
    // so enabling the boost for UART is verified safe ON ESP32 ONLY. ESP32 is defined
    // by the build only for TARGET_CHIP=esp32; every other target keeps the upstream USB-only gate.
#ifdef ESP32
    stub_lib_clock_init();

    // The boost raises APB to 80 MHz, changing the ROM-set UART baud divider. Reprogram it for the
    // current ROM link baud (115200) right after the boost, else the OHAI/handshake is corrupted;
    // esptool then issues its own CHANGE_BAUDRATE (recomputed from the new 80 MHz APB).
    if (transport == TRANSPORT_UART) {
        stub_lib_uart_rominit_set_baudrate(UART_NUM_0, 115200);
    }
#else
    if (transport == TRANSPORT_USB_OTG || transport == TRANSPORT_USB_SERIAL_JTAG ||
            transport == TRANSPORT_SDIO) {
        stub_lib_clock_init();
    }
#endif

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
