/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stddef.h>
#include "soc.h"

/* ESP32P4 capabilities */
static const struct soc_capabilities esp32p4_capabilities = {
    .is_riscv = true,
};

/* ESP32P4 USB-OTG peripheral */
static const struct usb_otg esp32p4_usb_otg = {
    .usb_otg_intr_map_reg = 0,
    .hp_sys_usbotg20_ctrl_reg = 0,
    .usb_inum = 0,
    .clic_ext_intr_num_offset = 0,
};

/* ESP32P4 Watchdog peripheral */
static const struct watchdog esp32p4_watchdog = {
    .option1_reg = 0,
    .wdtconfig0_reg = 0,
    .wdtwprotect_reg = 0,
    .swd_conf_reg = 0,
    .swd_wprotect_reg = 0,
    .cpu_per_conf_reg = 0,
    .sysclk_conf_reg = 0,
    .swd_wkey = 0,
    .wdt_wkey = 0,
    .swd_auto_feed_en_bit = 0,
    .force_download_boot_bit = 0,
    .cpuperiod_sel_mask = 0,
    .cpuperiod_sel_shift = 0,
    .cpuperiod_max = 0,
    .soc_clk_sel_mask = 0,
    .soc_clk_sel_shift = 0,
    .soc_clk_max = 0,
    .wdt_disable = NULL,
    .wdt_enable = NULL,
};

/* ESP32P4 peripherals structure */
static const struct soc_peripherals esp32p4_peripherals = {
    .usb_otg = &esp32p4_usb_otg,
    .watchdog = &esp32p4_watchdog,
};

/* ESP32P4 SoC info */
const struct soc_info g_soc_info = {
    .capabilities = &esp32p4_capabilities,
    .peripherals = &esp32p4_peripherals,
    .security_info_bytes = 0,
};
