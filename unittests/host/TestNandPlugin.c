/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

/**
 * Host unit tests for nand_plugin.c handlers.
 *
 * Each handler is tested for:
 *   - correct size  + target success  → expected response code
 *   - correct size  + target failure  → RESPONSE_FAILED_SPI_OP
 *   - wrong size                      → RESPONSE_BAD_DATA_LEN (no target call)
 *
 * Handlers fill a command_response_data struct and return a status code.
 * The dispatcher owns SLIP framing — handlers do not call slip_send_frame().
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "mock_nand.h"
#include "commands.h"
#include "command_handler.h"
#include "plugin_table.h"

/* ---- Dummy plugin_table (required by nand_plugin.c's extern declaration) -- */
plugin_cmd_handler_t plugin_table[PLUGIN_TABLE_SIZE];

/* ---- Forward declarations for functions under test ----------------------- */
int nand_plugin_attach(uint8_t command, const uint8_t *data, uint32_t len,
                       struct command_response_data *resp);
int nand_plugin_read_bbm(uint8_t command, const uint8_t *data, uint32_t len,
                         struct command_response_data *resp);
int nand_plugin_write_bbm(uint8_t command, const uint8_t *data, uint32_t len,
                          struct command_response_data *resp);
int nand_plugin_read_flash(uint8_t command, const uint8_t *data, uint32_t len,
                           struct command_response_data *resp);
int nand_plugin_write_flash_begin(uint8_t command, const uint8_t *data, uint32_t len,
                                  struct command_response_data *resp);
int nand_plugin_write_flash_data(uint8_t command, const uint8_t *data, uint32_t len,
                                 struct command_response_data *resp);
int nand_plugin_erase_flash(uint8_t command, const uint8_t *data, uint32_t len,
                            struct command_response_data *resp);
int nand_plugin_erase_region(uint8_t command, const uint8_t *data, uint32_t len,
                             struct command_response_data *resp);
int nand_plugin_read_page_debug(uint8_t command, const uint8_t *data, uint32_t len,
                                struct command_response_data *resp);

/* ---- Unity setUp / tearDown --------------------------------------------- */
void setUp(void)
{
    mock_nand_Init();
}

void tearDown(void)
{
    mock_nand_Verify();
    mock_nand_Destroy();
}

/* ======================================================================== */
/* nand_plugin_attach                                                        */
/* ======================================================================== */

void test_attach_success(void)
{
    uint8_t data[SPI_NAND_ATTACH_SIZE] = {0x01, 0x02, 0x03, 0x04};
    struct command_response_data resp = {0};
    stub_target_nand_attach_ExpectAndReturn(0x04030201, 0);

    int rc = nand_plugin_attach(ESP_SPI_NAND_ATTACH, data, SPI_NAND_ATTACH_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

void test_attach_target_failure(void)
{
    uint8_t data[SPI_NAND_ATTACH_SIZE] = {0};
    struct command_response_data resp = {0};
    stub_target_nand_attach_ExpectAndReturn(0, -1);

    int rc = nand_plugin_attach(ESP_SPI_NAND_ATTACH, data, SPI_NAND_ATTACH_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_FAILED_SPI_OP, rc);
    TEST_ASSERT_EQUAL_UINT32((uint32_t) -1, resp.value);
}

void test_attach_bad_size(void)
{
    uint8_t data[SPI_NAND_ATTACH_SIZE + 1] = {0};
    struct command_response_data resp = {0};
    /* No target call expected */
    int rc = nand_plugin_attach(ESP_SPI_NAND_ATTACH, data, SPI_NAND_ATTACH_SIZE + 1, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

/* ======================================================================== */
/* nand_plugin_read_bbm                                                      */
/* ======================================================================== */

void test_read_bbm_success(void)
{
    uint8_t data[SPI_NAND_READ_BBM_SIZE] = {0x05, 0x00, 0x00, 0x00};
    struct command_response_data resp = {0};
    stub_target_nand_read_bbm_IgnoreAndReturn(0);

    int rc = nand_plugin_read_bbm(ESP_SPI_NAND_READ_BBM, data, SPI_NAND_READ_BBM_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
    TEST_ASSERT_EQUAL_UINT32(0, resp.value);
}

void test_read_bbm_target_failure(void)
{
    uint8_t data[SPI_NAND_READ_BBM_SIZE] = {0};
    struct command_response_data resp = {0};
    stub_target_nand_read_bbm_IgnoreAndReturn(-1);

    int rc = nand_plugin_read_bbm(ESP_SPI_NAND_READ_BBM, data, SPI_NAND_READ_BBM_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_FAILED_SPI_OP, rc);
}

void test_read_bbm_bad_size(void)
{
    uint8_t data[SPI_NAND_READ_BBM_SIZE - 1] = {0};
    struct command_response_data resp = {0};
    /* No target call expected */
    int rc = nand_plugin_read_bbm(ESP_SPI_NAND_READ_BBM, data, SPI_NAND_READ_BBM_SIZE - 1, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

/* ======================================================================== */
/* nand_plugin_write_bbm                                                     */
/* ======================================================================== */

void test_write_bbm_success(void)
{
    uint8_t data[SPI_NAND_WRITE_BBM_SIZE] = {0x0A, 0x00, 0x00, 0x00, 0x01};
    struct command_response_data resp = {0};
    stub_target_nand_write_bbm_ExpectAndReturn(10, 1, 0);

    int rc = nand_plugin_write_bbm(ESP_SPI_NAND_WRITE_BBM, data, SPI_NAND_WRITE_BBM_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

void test_write_bbm_target_failure(void)
{
    uint8_t data[SPI_NAND_WRITE_BBM_SIZE] = {0};
    struct command_response_data resp = {0};
    stub_target_nand_write_bbm_ExpectAndReturn(0, 0, -1);

    int rc = nand_plugin_write_bbm(ESP_SPI_NAND_WRITE_BBM, data, SPI_NAND_WRITE_BBM_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_FAILED_SPI_OP, rc);
}

void test_write_bbm_bad_size(void)
{
    uint8_t data[SPI_NAND_WRITE_BBM_SIZE + 1] = {0};
    struct command_response_data resp = {0};
    /* No target call expected */
    int rc = nand_plugin_write_bbm(ESP_SPI_NAND_WRITE_BBM, data, SPI_NAND_WRITE_BBM_SIZE + 1, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

/* ======================================================================== */
/* nand_plugin_read_page_debug                                               */
/* ======================================================================== */

void test_read_page_debug_success(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE] = {0x02, 0x00, 0x00, 0x00};
    struct command_response_data resp = {0};
    stub_target_nand_read_page_IgnoreAndReturn(0);

    int rc = nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                         SPI_NAND_READ_PAGE_DEBUG_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
    TEST_ASSERT_EQUAL_UINT32(2, resp.value);
    TEST_ASSERT_EQUAL_UINT32(16, resp.data_size);
}

void test_read_page_debug_target_failure(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE] = {0};
    struct command_response_data resp = {0};
    stub_target_nand_read_page_IgnoreAndReturn(-1);

    int rc = nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                         SPI_NAND_READ_PAGE_DEBUG_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_FAILED_SPI_OP, rc);
}

void test_read_page_debug_bad_size(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE + 1] = {0};
    struct command_response_data resp = {0};
    /* No target call expected */
    int rc = nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                         SPI_NAND_READ_PAGE_DEBUG_SIZE + 1, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

/* ======================================================================== */
/* Not-implemented and trivial handlers                                      */
/* ======================================================================== */

void test_write_flash_begin_returns_success(void)
{
    struct command_response_data resp = {0};
    int rc = nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN, NULL, 0, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

void test_read_flash_not_implemented(void)
{
    struct command_response_data resp = {0};
    int rc = nand_plugin_read_flash(ESP_SPI_NAND_READ_FLASH, NULL, 0, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_CMD_NOT_IMPLEMENTED, rc);
}

void test_write_flash_data_not_implemented(void)
{
    struct command_response_data resp = {0};
    int rc = nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA, NULL, 0, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_CMD_NOT_IMPLEMENTED, rc);
}

void test_erase_flash_not_implemented(void)
{
    struct command_response_data resp = {0};
    int rc = nand_plugin_erase_flash(ESP_SPI_NAND_ERASE_FLASH, NULL, 0, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_CMD_NOT_IMPLEMENTED, rc);
}

void test_erase_region_not_implemented(void)
{
    struct command_response_data resp = {0};
    int rc = nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, NULL, 0, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_CMD_NOT_IMPLEMENTED, rc);
}

/* ======================================================================== */
/* main                                                                      */
/* ======================================================================== */

int main(void)
{
    UnityBegin(__FILE__);

    RUN_TEST(test_attach_success);
    RUN_TEST(test_attach_target_failure);
    RUN_TEST(test_attach_bad_size);

    RUN_TEST(test_read_bbm_success);
    RUN_TEST(test_read_bbm_target_failure);
    RUN_TEST(test_read_bbm_bad_size);

    RUN_TEST(test_write_bbm_success);
    RUN_TEST(test_write_bbm_target_failure);
    RUN_TEST(test_write_bbm_bad_size);

    RUN_TEST(test_read_page_debug_success);
    RUN_TEST(test_read_page_debug_target_failure);
    RUN_TEST(test_read_page_debug_bad_size);

    RUN_TEST(test_write_flash_begin_returns_success);
    RUN_TEST(test_read_flash_not_implemented);
    RUN_TEST(test_write_flash_data_not_implemented);
    RUN_TEST(test_erase_flash_not_implemented);
    RUN_TEST(test_erase_region_not_implemented);

    return UnityEnd();
}
