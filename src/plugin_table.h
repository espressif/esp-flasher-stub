/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once
#include <stdint.h>
#include "command_handler.h"

/*
 * Extensible opcode range covered by the Function Pointer Table (FPT).
 * 0xD5-0xDD: NAND flash commands
 * 0xDE:      Key Manager (future)
 * 0xDF-0xEF: reserved
 */
#define PLUGIN_FIRST_OPCODE  0xD5
#define PLUGIN_LAST_OPCODE   0xEF
/* 27 — must match PLUGIN_TABLE_ENTRIES in tools/elf2json.py */
#define PLUGIN_TABLE_SIZE    (PLUGIN_LAST_OPCODE - PLUGIN_FIRST_OPCODE + 1)

/*
 * Plugin command handler ABI.
 *
 * The handler fills *resp with the response payload and returns a status code
 * from enum esp_response_code (RESPONSE_SUCCESS == 0 on success).
 * The base dispatcher calls s_send_response() and runs any post-process step
 * after the handler returns.
 *
 * Parameters:
 *   command — opcode byte (passed for convenience; dispatcher already knows it)
 *   data    — pointer to the command payload
 *   len     — payload length in bytes
 *   resp    — output: response data to send back; guaranteed non-NULL (dispatcher
 *             zeroes the struct and passes its address before calling the handler)
 *
 * Return value: an esp_response_code value (0 = RESPONSE_SUCCESS).
 */
typedef int (*plugin_cmd_handler_t)(uint8_t command,
                                    const uint8_t *data,
                                    uint32_t len,
                                    struct command_response_data *resp);

/*
 * Global Function Pointer Table — populated with s_plugin_unsupported
 * defaults by the base stub, patched by esptool at upload time when a
 * plugin is loaded.
 */
extern plugin_cmd_handler_t plugin_table[PLUGIN_TABLE_SIZE];
