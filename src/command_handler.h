/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Main command handler for ESP flasher stub
 *
 * Function-based approach without classes for minimal flash footprint.
 */

void run_command_loop();

#ifdef __cplusplus
}
#endif
