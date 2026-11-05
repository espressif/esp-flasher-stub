/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

/*
 * Base stub symbols that plugins link against.
 *
 * LTO makes every other base symbol local, so tools/compute_plugin_addrs.py
 * passes the address of every global base function and variable to the plugin
 * link. Exported functions are noinline: the base must keep their out-of-line
 * copy for the plugins anyway, and inlining it into callers only adds code.
 */
#define STUB_EXPORT      __attribute__((used, externally_visible, noinline))
#define STUB_EXPORT_DATA __attribute__((used, externally_visible))
