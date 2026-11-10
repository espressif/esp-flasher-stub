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
 * 0xD5-0xDE: NAND flash commands
 * 0xDF-0xEF: reserved
 */
#define PLUGIN_FIRST_OPCODE  0xD5
#define PLUGIN_LAST_OPCODE   0xEF
/* 27 */
#define PLUGIN_TABLE_SIZE    (PLUGIN_LAST_OPCODE - PLUGIN_FIRST_OPCODE + 1)

/*
 * Plugin command handler ABI.
 *
 * Parameters:
 *   command — opcode byte (passed for convenience; dispatcher already knows it)
 *   data    — pointer to the command payload
 *   len     — payload length in bytes
 *   resp    — output: response data to send back; guaranteed non-NULL (dispatcher
 *             zeroes the struct and passes its address before calling the handler)
 *
 * Handler contract:
 *   1. Fill resp->value / resp->data / resp->data_size for the response payload.
 *   2. Optionally set resp->post_process to a callback that the dispatcher invokes
 *      immediately after sending the response frame (e.g. streaming reads).
 *      Leave resp->post_process as NULL when no post-processing is needed.
 *   3. Return RESPONSE_SUCCESS or an error code.
 *
 * The dispatcher (command_handler.c:s_send_response) owns primary response framing —
 * handlers MUST NOT send the response frame themselves.  Streaming frames inside
 * a post_process callback should use ctx->transport.
 *
 * Return value (esp_response_code):
 *   RESPONSE_SUCCESS (0): dispatcher sends a success frame, then calls
 *       resp->post_process (if non-NULL).
 *   Any non-success code: dispatcher sends an error frame with the returned code;
 *       resp->post_process is NOT called.  This is the path used by the default
 *       s_plugin_unsupported stub for unpatched opcodes.
 */
typedef int (*plugin_cmd_handler_t)(uint8_t command,
                                    const uint8_t *data,
                                    uint32_t len,
                                    struct command_response_data *resp);

/*
 * Global Function Pointer Table — populated with s_plugin_unsupported
 * defaults by the base stub. A command plugin's start function writes its
 * handlers into it.
 */
extern plugin_cmd_handler_t plugin_table[PLUGIN_TABLE_SIZE];

/* Plugins that the host can load at the same time. */
#define PLUGIN_START_SLOTS 2

/*
 * Start functions of the loaded plugins, in the base stub's .data. The host
 * writes each plugin's entry here before upload, and esp_main calls them
 * after it zeroes the base .bss.
 *
 * A start function runs before clock init and before any transport is up,
 * so it only zeroes the plugin's .bss and registers the plugin: a command
 * plugin writes its handlers into plugin_table, a transport plugin fills
 * transport_plugin (src/transport.h).
 */
typedef void (*plugin_start_t)(void);
extern plugin_start_t plugin_start[PLUGIN_START_SLOTS];

/*
 * Zeroes the calling plugin's .bss, which the host does not upload. Every
 * start function calls it first. Inline, so that _bss_start and _bss_end
 * resolve against the plugin's own link.
 */
static inline void plugin_zero_bss(void)
{
    extern uint32_t _bss_start;
    extern uint32_t _bss_end;

    for (uint32_t *p = &_bss_start; p < &_bss_end; p++) {
        *p = 0;
    }
}
