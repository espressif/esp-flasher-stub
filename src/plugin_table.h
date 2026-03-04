/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once
#include <stdint.h>

/*
 * Extensible opcode range covered by the Function Pointer Table (FPT).
 * 0xD5-0xDD: NAND flash commands
 * 0xDE:      Key Manager (future)
 * 0xDF-0xEF: reserved
 */
#define PLUGIN_FIRST_OPCODE  0xD5
#define PLUGIN_LAST_OPCODE   0xEF
#define PLUGIN_TABLE_SIZE    (PLUGIN_LAST_OPCODE - PLUGIN_FIRST_OPCODE + 1)  /* 27 */

/*
 * Plugin command handler ABI.
 * Each handler is responsible for sending its own SLIP response frame(s).
 * The command byte is passed so the handler can embed the correct opcode
 * in its response.
 */
typedef void (*plugin_cmd_handler_t)(uint8_t command,
                                     const uint8_t *data,
                                     uint16_t size);

/*
 * Global Function Pointer Table — populated with s_plugin_unsupported
 * defaults by the base stub, patched by esptool at upload time when a
 * plugin is loaded.
 */
extern plugin_cmd_handler_t plugin_table[PLUGIN_TABLE_SIZE];
