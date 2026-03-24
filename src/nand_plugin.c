/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * NAND flash plugin — compiled as a separate binary loaded at runtime by esptool.
 *
 * Handlers fill a command_response_data struct and return a status code.
 * The base stub dispatcher owns SLIP response framing and post-process execution.
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <target/nand.h>
#include "commands.h"
#include "command_handler.h"
#include "endian_utils.h"
#include "plugin_table.h"

/* ---- Plugin handler implementations --------------------------------------- */

int nand_plugin_attach(uint8_t command, const uint8_t *data, uint32_t len,
                       struct command_response_data *resp)
{
    (void)command;
    if (len != SPI_NAND_ATTACH_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t hspi_arg = get_le_to_u32(data);
    int result = stub_target_nand_attach(hspi_arg);
    if (result != 0) {
        resp->value = (uint32_t)result;
        return RESPONSE_FAILED_SPI_OP;
    }

    return RESPONSE_SUCCESS;
}

int nand_plugin_read_bbm(uint8_t command, const uint8_t *data, uint32_t len,
                         struct command_response_data *resp)
{
    (void)command;
    if (len != SPI_NAND_READ_BBM_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t page_number = get_le_to_u32(data);
    uint8_t spare[4] = {0};
    if (stub_target_nand_read_bbm(page_number, spare) != 0) {
        return RESPONSE_FAILED_SPI_OP;
    }

    /* Return spare[0] in the value field so check_command(resp_data_len=0) returns it
     * directly without confusing it with the status bytes. */
    resp->value = spare[0];
    return RESPONSE_SUCCESS;
}

int nand_plugin_write_bbm(uint8_t command, const uint8_t *data, uint32_t len,
                          struct command_response_data *resp)
{
    (void)command; (void)resp;
    if (len != SPI_NAND_WRITE_BBM_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t page_number = get_le_to_u32(data);
    uint8_t is_bad = data[4];
    if (stub_target_nand_write_bbm(page_number, is_bad) != 0) {
        return RESPONSE_FAILED_SPI_OP;
    }
    return RESPONSE_SUCCESS;
}

int nand_plugin_read_flash(uint8_t command, const uint8_t *data, uint32_t len,
                           struct command_response_data *resp)
{
    (void)command; (void)data; (void)len; (void)resp;
    return RESPONSE_CMD_NOT_IMPLEMENTED;
}

int nand_plugin_write_flash_begin(uint8_t command, const uint8_t *data, uint32_t len,
                                  struct command_response_data *resp)
{
    (void)command; (void)data; (void)len; (void)resp;
    return RESPONSE_SUCCESS;
}

int nand_plugin_write_flash_data(uint8_t command, const uint8_t *data, uint32_t len,
                                 struct command_response_data *resp)
{
    (void)command; (void)data; (void)len; (void)resp;
    return RESPONSE_CMD_NOT_IMPLEMENTED;
}

int nand_plugin_erase_flash(uint8_t command, const uint8_t *data, uint32_t len,
                            struct command_response_data *resp)
{
    (void)command; (void)data; (void)len; (void)resp;
    return RESPONSE_CMD_NOT_IMPLEMENTED;
}

int nand_plugin_erase_region(uint8_t command, const uint8_t *data, uint32_t len,
                             struct command_response_data *resp)
{
    (void)command; (void)data; (void)len; (void)resp;
    return RESPONSE_CMD_NOT_IMPLEMENTED;
}

int nand_plugin_read_page_debug(uint8_t command, const uint8_t *data, uint32_t len,
                                struct command_response_data *resp)
{
    (void)command;
    if (len != SPI_NAND_READ_PAGE_DEBUG_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t page_number = get_le_to_u32(data);
    static uint8_t page_buf[16] __attribute__((aligned(4)));
    if (stub_target_nand_read_page(page_number, page_buf, 16) != 0) {
        return RESPONSE_FAILED_SPI_OP;
    }

    resp->value = page_number;
    memcpy(resp->data, page_buf, 16);
    resp->data_size = 16;
    return RESPONSE_SUCCESS;
}
