/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "soc.h"

void disable_watchdogs(void)
{
    const struct watchdog *watchdog = soc_get_peripherals()->watchdog;
    if (watchdog->wdt_disable) {
        watchdog->wdt_disable();
    }
}

void enable_watchdogs(void)
{
    const struct watchdog *watchdog = soc_get_peripherals()->watchdog;
    if (watchdog->wdt_enable) {
        watchdog->wdt_enable();
    }
}
