/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <esp-stub-lib/flash.h>

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
    void *flash_state = nullptr;
    stub_lib_flash_init(&flash_state);

    if (flash_state) {
        stub_lib_flash_deinit(flash_state);
    }
}
