/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stddef.h>
#include <esp-stub-lib/flash.h>
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

void esp_main(void)
{
    void *flash_state = NULL;
    stub_lib_flash_init(&flash_state);

    // Send OHAI greeting to signal stub is active
    const uint8_t greeting[4] = {'O', 'H', 'A', 'I'};
    slip_send_frame(&greeting, sizeof(greeting));

    command_handler_loop();

    // Cleanup
    if (flash_state) {
        stub_lib_flash_deinit(flash_state);
    }
}
