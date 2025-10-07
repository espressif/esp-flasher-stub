/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "peripherals/usb_otg.h"
#include "peripherals/watchdog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Essential SoC Capabilities for flasher stub */
struct soc_capabilities {
    bool is_riscv;              /* RISC-V architecture (affects interrupt handling) */
};

/* Essential peripherals for flasher stub */
struct soc_peripherals {
    const struct usb_otg *usb_otg;        /* USB-OTG communication */
    const struct watchdog *watchdog;  /* Watchdog and power management */
};

/* Main SoC structure */
struct soc_info {
    const struct soc_capabilities *capabilities;
    const struct soc_peripherals *peripherals;
    uint8_t security_info_bytes;
};

const struct soc_info* soc_get_info(void);

/* Convenience accessors */
static inline const struct soc_capabilities* soc_get_capabilities(void)
{
    return soc_get_info()->capabilities;
}

static inline const struct soc_peripherals* soc_get_peripherals(void)
{
    return soc_get_info()->peripherals;
}

#ifdef __cplusplus
}
#endif
