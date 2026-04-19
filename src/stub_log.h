/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * Stub diagnostic logging hook.
 *
 * stub_logf is a BSS function pointer installed by a logging plugin at runtime.
 * It is NULL until a plugin (e.g. diag) installs its implementation.
 *
 * Usage:
 *   #include "stub_log.h"
 *   STUB_LOGF("flash: begin off=0x%08x size=%u\n", offset, size);
 */

#pragma once

extern void (*stub_logf)(const char *fmt, ...);

#define STUB_LOGF(fmt, ...) do { \
    if (stub_logf) stub_logf(fmt, ##__VA_ARGS__); \
} while (0)
