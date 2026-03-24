/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

/**
 * Host unit tests for the plugin Function Pointer Table (FPT) dispatch.
 *
 * Tests:
 *   1. Unpatched slot (default s_plugin_unsupported) returns
 *      RESPONSE_CMD_NOT_IMPLEMENTED.
 *   2. Patched slot calls the registered handler and propagates its return
 *      value and response payload.
 *
 * We define plugin_table here (like TestNandPlugin.c does) so we can control
 * its initial state and patch it in tests.  The default stub — filling every
 * slot with s_plugin_unsupported — is re-created via a small local helper that
 * mirrors command_handler.c's initialiser.
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "commands.h"
#include "command_handler.h"
#include "plugin_table.h"

/* ---- Local s_plugin_unsupported mirror ----------------------------------- */

static int s_plugin_unsupported_local(uint8_t command,
                                      const uint8_t *data,
                                      uint32_t len,
                                      struct command_response_data *resp)
{
    (void)command; (void)data; (void)len; (void)resp;
    return RESPONSE_CMD_NOT_IMPLEMENTED;
}

/* ---- Plugin table definition (required by plugin_table.h extern) --------- */

plugin_cmd_handler_t plugin_table[PLUGIN_TABLE_SIZE];

/* ---- Helper: reset every FPT slot to the unsupported default ------------- */

static void reset_plugin_table(void)
{
    for (int i = 0; i < PLUGIN_TABLE_SIZE; i++) {
        plugin_table[i] = s_plugin_unsupported_local;
    }
}

/* ---- Spy handler used in patching tests ---------------------------------- */

static int s_spy_called_with_opcode = -1;
static uint32_t s_spy_called_with_len = 0;
static int s_spy_return_value = RESPONSE_SUCCESS;
static uint32_t s_spy_response_value = 0xDEADBEEFU;

static int s_spy_handler(uint8_t command,
                         const uint8_t *data,
                         uint32_t len,
                         struct command_response_data *resp)
{
    (void)data;
    s_spy_called_with_opcode = (int)command;
    s_spy_called_with_len = len;
    resp->value = s_spy_response_value;
    return s_spy_return_value;
}

/* ---- Unity setUp / tearDown --------------------------------------------- */

void setUp(void)
{
    reset_plugin_table();
    s_spy_called_with_opcode = -1;
    s_spy_called_with_len = 0;
    s_spy_return_value = RESPONSE_SUCCESS;
    s_spy_response_value = 0xDEADBEEFU;
}

void tearDown(void)
{
}

/* ======================================================================== */
/* Tests                                                                      */
/* ======================================================================== */

/*
 * test_unpatched_slot_returns_not_implemented
 *
 * Every slot initialised by reset_plugin_table() must return
 * RESPONSE_CMD_NOT_IMPLEMENTED when called.
 */
void test_unpatched_slot_returns_not_implemented(void)
{
    struct command_response_data resp = {0};
    uint8_t dummy_data[4] = {0};

    /* Check first slot (opcode PLUGIN_FIRST_OPCODE) */
    int rc = plugin_table[0](PLUGIN_FIRST_OPCODE, dummy_data, sizeof(dummy_data), &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_CMD_NOT_IMPLEMENTED, rc);
}

/*
 * test_all_unpatched_slots_return_not_implemented
 *
 * Every unpatched slot returns RESPONSE_CMD_NOT_IMPLEMENTED, not zero or some
 * other code.
 */
void test_all_unpatched_slots_return_not_implemented(void)
{
    struct command_response_data resp;

    for (int i = 0; i < PLUGIN_TABLE_SIZE; i++) {
        memset(&resp, 0, sizeof(resp));
        int rc = plugin_table[i]((uint8_t)(PLUGIN_FIRST_OPCODE + i), NULL, 0, &resp);
        TEST_ASSERT_EQUAL_INT_MESSAGE(RESPONSE_CMD_NOT_IMPLEMENTED, rc,
                                      "Unpatched FPT slot must return RESPONSE_CMD_NOT_IMPLEMENTED");
    }
}

/*
 * test_patched_slot_calls_handler
 *
 * After patching slot 0 (opcode PLUGIN_FIRST_OPCODE), calling
 * plugin_table[0]() must invoke the spy handler with the correct opcode.
 */
void test_patched_slot_calls_handler(void)
{
    struct command_response_data resp = {0};
    uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};

    plugin_table[0] = s_spy_handler;

    plugin_table[0](PLUGIN_FIRST_OPCODE, payload, sizeof(payload), &resp);

    TEST_ASSERT_EQUAL_INT(PLUGIN_FIRST_OPCODE, s_spy_called_with_opcode);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), s_spy_called_with_len);
}

/*
 * test_patched_slot_propagates_return_value
 *
 * The return value from the registered handler must be propagated to the
 * caller (the dispatcher in command_handler.c checks this to decide whether
 * to send a success or error response).
 */
void test_patched_slot_propagates_return_value(void)
{
    struct command_response_data resp = {0};

    s_spy_return_value = RESPONSE_FAILED_SPI_OP;
    plugin_table[0] = s_spy_handler;

    int rc = plugin_table[0](PLUGIN_FIRST_OPCODE, NULL, 0, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_FAILED_SPI_OP, rc);
}

/*
 * test_patched_slot_propagates_response_payload
 *
 * The handler may fill *resp->value; the caller (dispatcher) forwards this
 * to esptool in the response header.
 */
void test_patched_slot_propagates_response_payload(void)
{
    struct command_response_data resp = {0};

    s_spy_response_value = 0x12345678U;
    plugin_table[0] = s_spy_handler;

    plugin_table[0](PLUGIN_FIRST_OPCODE, NULL, 0, &resp);

    TEST_ASSERT_EQUAL_UINT32(0x12345678U, resp.value);
}

/*
 * test_unpatched_slot_not_affected_by_neighbour_patch
 *
 * Patching slot N must not alter adjacent slots — each slot is independent.
 */
void test_unpatched_slot_not_affected_by_neighbour_patch(void)
{
    struct command_response_data resp = {0};

    /* Patch slot 0 only */
    plugin_table[0] = s_spy_handler;

    /* Slot 1 must still return RESPONSE_CMD_NOT_IMPLEMENTED */
    int rc = plugin_table[1]((uint8_t)(PLUGIN_FIRST_OPCODE + 1), NULL, 0, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_CMD_NOT_IMPLEMENTED, rc);
}

/*
 * test_patched_last_slot
 *
 * Patching the last slot (PLUGIN_TABLE_SIZE - 1) and calling it must reach
 * the spy handler.
 */
void test_patched_last_slot(void)
{
    struct command_response_data resp = {0};
    int last = PLUGIN_TABLE_SIZE - 1;
    uint8_t opcode = (uint8_t)(PLUGIN_FIRST_OPCODE + last);

    plugin_table[last] = s_spy_handler;

    int rc = plugin_table[last](opcode, NULL, 0, &resp);

    TEST_ASSERT_EQUAL_INT(PLUGIN_FIRST_OPCODE + last, s_spy_called_with_opcode);
    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

/* ======================================================================== */
/* main                                                                       */
/* ======================================================================== */

int main(void)
{
    UnityBegin(__FILE__);

    RUN_TEST(test_unpatched_slot_returns_not_implemented);
    RUN_TEST(test_all_unpatched_slots_return_not_implemented);
    RUN_TEST(test_patched_slot_calls_handler);
    RUN_TEST(test_patched_slot_propagates_return_value);
    RUN_TEST(test_patched_slot_propagates_response_payload);
    RUN_TEST(test_unpatched_slot_not_affected_by_neighbour_patch);
    RUN_TEST(test_patched_last_slot);

    return UnityEnd();
}
