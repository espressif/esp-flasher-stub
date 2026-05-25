/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "unity.h"
#include "mock_rom_wrappers.h"
#include "mock_uart.h"
#include "slip.h"
#include "frame_buffer.h"
#include <string.h>

void setUp(void)
{
    mock_rom_wrappers_Init();
    mock_uart_Init();

    /* Hand the SLIP decoder a fresh receive buffer — the ISR refuses to
     * start a frame without one. frame_buffer state persists across tests
     * via static state, but every RX test that marks a frame resets it before
     * returning, so acquire() yields a clean buffer here. */
    slip_rearm(frame_buffer_acquire(), FRAME_BUFFER_SIZE);
}

void tearDown(void)
{
    mock_rom_wrappers_Verify();
    mock_rom_wrappers_Destroy();
    mock_uart_Verify();
    mock_uart_Destroy();
}

/* ---- TX: slip_send_frame ------------------------------------------------- */

void test_slip_send_frame_complete(void)
{
    uint8_t test_data[] = {0x01, 0xC0, 0xDB, 0x02};

    /* SLIP_END + escaped payload + SLIP_END */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xC0, 0); /* SLIP_END open */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x01, 0);
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDC, 0); /* ESC_END */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDD, 0); /* ESC_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x02, 0);
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xC0, 0); /* SLIP_END close */

    TEST_ASSERT_TRUE(slip_send_frame(test_data, sizeof(test_data)));
}

void test_slip_send_frame_null_data(void)
{
    /* No bytes should be sent; function must return false */
    TEST_ASSERT_FALSE(slip_send_frame(NULL, 10));
}

/* ---- RX: slip_recv_byte + frame_buffer ----------------------------------- */
/*
 * slip_recv_byte decodes SLIP framing and writes payload into frame_buffer.
 * Tests exercise the state machine and verify frame_buffer state afterwards.
 */

void test_slip_recv_byte_no_frame_start(void)
{
    /* Non-SLIP_END byte before any frame — no frame should start */
    slip_recv_byte(0x55);
    TEST_ASSERT_EQUAL_INT(FRAME_BUFFER_STATE_IDLE, frame_buffer_get_state());
}

void test_slip_recv_byte_empty_frame_discarded(void)
{
    /* Two consecutive SLIP_ENDs without payload — frame_buffer_mark_complete
     * ignores zero-length frames, so state stays IDLE */
    slip_recv_byte(0xC0);
    slip_recv_byte(0xC0);
    TEST_ASSERT_EQUAL_INT(FRAME_BUFFER_STATE_IDLE, frame_buffer_get_state());
    frame_buffer_reset();
}

void test_slip_recv_byte_complete_frame(void)
{
    uint8_t expected[] = {0x01, 0x02, 0x03};

    slip_recv_byte(0xC0);
    slip_recv_byte(0x01);
    slip_recv_byte(0x02);
    slip_recv_byte(0x03);
    slip_recv_byte(0xC0);

    TEST_ASSERT_EQUAL_INT(FRAME_BUFFER_STATE_COMPLETE, frame_buffer_get_state());

    size_t len;
    const uint8_t *data = frame_buffer_get_data(&len);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
    TEST_ASSERT_EQUAL_MEMORY(expected, data, len);
    frame_buffer_reset();
}

void test_slip_recv_byte_escape_end(void)
{
    slip_recv_byte(0xC0);
    slip_recv_byte(0xDB); /* ESC */
    slip_recv_byte(0xDC); /* ESC_END → 0xC0 */
    slip_recv_byte(0xC0); /* frame end */

    TEST_ASSERT_EQUAL_INT(FRAME_BUFFER_STATE_COMPLETE, frame_buffer_get_state());

    size_t len;
    const uint8_t *data = frame_buffer_get_data(&len);
    TEST_ASSERT_EQUAL_size_t(1, len);
    TEST_ASSERT_EQUAL_UINT8(0xC0, data[0]);
    frame_buffer_reset();
}

void test_slip_recv_byte_escape_esc(void)
{
    slip_recv_byte(0xC0);
    slip_recv_byte(0xDB); /* ESC */
    slip_recv_byte(0xDD); /* ESC_ESC → 0xDB */
    slip_recv_byte(0xC0); /* frame end */

    TEST_ASSERT_EQUAL_INT(FRAME_BUFFER_STATE_COMPLETE, frame_buffer_get_state());

    size_t len;
    const uint8_t *data = frame_buffer_get_data(&len);
    TEST_ASSERT_EQUAL_size_t(1, len);
    TEST_ASSERT_EQUAL_UINT8(0xDB, data[0]);
    frame_buffer_reset();
}

void test_slip_recv_byte_invalid_escape(void)
{
    slip_recv_byte(0xC0);
    slip_recv_byte(0xDB); /* ESC */
    slip_recv_byte(0xFF); /* invalid — frame becomes an error */

    TEST_ASSERT_EQUAL_INT(FRAME_BUFFER_STATE_ERROR, frame_buffer_get_state());
    frame_buffer_reset();
}

int main(void)
{
    UnityBegin(__FILE__);

    RUN_TEST(test_slip_send_frame_complete);
    RUN_TEST(test_slip_send_frame_null_data);
    RUN_TEST(test_slip_recv_byte_no_frame_start);
    RUN_TEST(test_slip_recv_byte_empty_frame_discarded);
    RUN_TEST(test_slip_recv_byte_complete_frame);
    RUN_TEST(test_slip_recv_byte_escape_end);
    RUN_TEST(test_slip_recv_byte_escape_esc);
    RUN_TEST(test_slip_recv_byte_invalid_escape);

    return UnityEnd();
}
