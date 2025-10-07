/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watchdog peripheral structure */
struct watchdog {
    /* Register offsets */
    uint32_t option1_reg;
    uint32_t wdtconfig0_reg;
    uint32_t wdtwprotect_reg;
    uint32_t swd_conf_reg;
    uint32_t swd_wprotect_reg;
    uint32_t cpu_per_conf_reg;
    uint32_t sysclk_conf_reg;

    /* Chip-specific values */
    uint32_t swd_wkey;
    uint32_t wdt_wkey;
    uint32_t swd_auto_feed_en_bit;
    uint32_t force_download_boot_bit;
    uint32_t cpuperiod_sel_mask;
    uint32_t cpuperiod_sel_shift;
    uint32_t cpuperiod_max;
    uint32_t soc_clk_sel_mask;
    uint32_t soc_clk_sel_shift;
    uint32_t soc_clk_max;

    /* Function pointers for RTC-specific operations */
    void (*wdt_disable)(void);
    void (*wdt_enable)(void);
};

#ifdef __cplusplus
}
#endif
