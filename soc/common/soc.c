/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stddef.h>
#include "soc.h"

/*
 * Single SoC info structure - chip-specific file provides this
 * Chip selected at compile time
 */
extern const struct soc_info g_soc_info;

const struct soc_info* soc_get_info(void)
{
    return &g_soc_info;
}
