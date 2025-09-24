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

/* USB OTG peripheral structure */
struct usb_otg {
    uint32_t usb_otg_intr_map_reg;            /* USB OTG interrupt mapping */
    uint32_t hp_sys_usbotg20_ctrl_reg;        /* USB OTG control register */
    uint32_t usb_inum;                        /* USB interrupt number */
    uint32_t clic_ext_intr_num_offset;        /* RISC-V CLIC interrupt offset */
};

#ifdef __cplusplus
}
#endif
