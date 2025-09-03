/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <esp-stub-lib/flash.h>
#include <esp-stub-lib/rom_wrappers.h>
#include "slip.hpp"

extern "C" void esp_main(void) __attribute__((used));

#ifdef ESP8266
__asm__(
    ".global esp_main_esp8266\n"
    ".literal_position\n"
    ".align 4\n"
    "esp_main_esp8266:\n"
    "movi a0, 0x400010a8;"
    "j esp_main;");
#endif //ESP8266

extern "C" void esp_main(void)
{
    // Initialize flash subsystem
    void *flash_state = nullptr;
    stub_lib_flash_init(&flash_state);

    // Send OHAI greeting to signal stub is active
    const uint8_t greeting[4] = {'O', 'H', 'A', 'I'};
    slip_send_frame(&greeting, sizeof(greeting));

    // TODO: Implement command loop

    // Cleanup
    if (flash_state) {
        stub_lib_flash_deinit(flash_state);
    }
}
