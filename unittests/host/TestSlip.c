/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "unity.h"
#include "mock_rom_wrappers.h"
#include "slip.h"
#include <string.h>

/* Test setup and teardown */
void setUp(void)
{
    /* Initialize mocks */
    mock_rom_wrappers_Init();
}

void tearDown(void)
{
    /* Clean up mocks */
    mock_rom_wrappers_Verify();
    mock_rom_wrappers_Destroy();
}

/* Test cases */
void test_slip_send_frame_delimiter(void)
{
    /* Expect uart_tx_one_char to be called with SLIP_END (0xC0) */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xC0, 0);

    /* Call the function under test */
    slip_send_frame_delimiter();
}

void test_slip_send_frame_data_normal_byte(void)
{
    /* Expect uart_tx_one_char to be called with normal byte */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x55, 0);

    /* Call the function under test */
    slip_send_frame_data(0x55);
}

void test_slip_send_frame_data_end_escape(void)
{
    /* Expect uart_tx_one_char to be called twice: SLIP_ESC then SLIP_ESC_END */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* SLIP_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDC, 0); /* SLIP_ESC_END */

    /* Call the function under test with SLIP_END byte */
    slip_send_frame_data(0xC0);
}

void test_slip_send_frame_data_esc_escape(void)
{
    /* Expect uart_tx_one_char to be called twice: SLIP_ESC then SLIP_ESC_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* SLIP_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDD, 0); /* SLIP_ESC_ESC */

    /* Call the function under test with SLIP_ESC byte */
    slip_send_frame_data(0xDB);
}

void test_slip_send_frame_data_buf_normal(void)
{
    uint8_t test_data[] = {0x01, 0x02, 0x03};

    /* Expect uart_tx_one_char to be called for each byte */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x01, 0);
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x02, 0);
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x03, 0);

    /* Call the function under test */
    slip_send_frame_data_buf(test_data, sizeof(test_data));
}

void test_slip_send_frame_data_buf_with_escapes(void)
{
    uint8_t test_data[] = {0x01, 0xC0, 0xDB, 0x02};

    /* Expect uart_tx_one_char to be called for each byte with proper escaping */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x01, 0);
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* SLIP_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDC, 0); /* SLIP_ESC_END */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* SLIP_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDD, 0); /* SLIP_ESC_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x02, 0);

    /* Call the function under test */
    slip_send_frame_data_buf(test_data, sizeof(test_data));
}

void test_slip_send_frame_data_buf_null_pointer(void)
{
    /* Should not call uart_tx_one_char when data is NULL */

    /* Call the function under test with NULL data */
    slip_send_frame_data_buf(NULL, 10);
}

void test_slip_send_frame_complete(void)
{
    uint8_t test_data[] = {0x01, 0xC0, 0xDB, 0x02};

    /* Expect complete frame: SLIP_END + data + SLIP_END */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xC0, 0); /* SLIP_END */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x01, 0);
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* SLIP_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDC, 0); /* SLIP_ESC_END */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDB, 0); /* SLIP_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xDD, 0); /* SLIP_ESC_ESC */
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x02, 0);
    stub_lib_uart_tx_one_char_ExpectAndReturn(0xC0, 0); /* SLIP_END */

    /* Call the function under test */
    slip_send_frame(test_data, sizeof(test_data));
}

void test_slip_send_frame_null_data(void)
{
    /* Should not call uart_tx_one_char when data is NULL */

    /* Call the function under test with NULL data */
    slip_send_frame(NULL, 10);
}

void test_slip_recv_byte_no_frame_start(void)
{
    /* Test receiving non-SLIP_END byte when not in frame */
    /* This should not cause any state change */

    /* Call the function under test */
    slip_recv_byte(0x55);
}

void test_slip_recv_byte_frame_start(void)
{
    /* Test receiving SLIP_END to start frame */
    /* This should change state to STATE_IN_FRAME */

    /* Call the function under test */
    slip_recv_byte(0xC0);
}

void test_slip_recv_byte_frame_end(void)
{
    /* First start a frame */
    slip_recv_byte(0xC0);

    /* Then end it */
    slip_recv_byte(0xC0);
}

void test_slip_recv_byte_escape_sequence_end(void)
{
    /* Start frame */
    slip_recv_byte(0xC0);

    /* Send escape sequence for SLIP_END */
    slip_recv_byte(0xDB); /* SLIP_ESC */
    slip_recv_byte(0xDC); /* SLIP_ESC_END - should become SLIP_END */
}

void test_slip_recv_byte_escape_sequence_esc(void)
{
    /* Start frame */
    slip_recv_byte(0xC0);

    /* Send escape sequence for SLIP_ESC */
    slip_recv_byte(0xDB); /* SLIP_ESC */
    slip_recv_byte(0xDD); /* SLIP_ESC_ESC - should become SLIP_ESC */
}

void test_slip_recv_byte_invalid_escape(void)
{
    /* Start frame */
    slip_recv_byte(0xC0);

    /* Send invalid escape sequence */
    slip_recv_byte(0xDB); /* SLIP_ESC */
    slip_recv_byte(0xFF); /* Invalid escape - should be dropped */
}

int main(void)
{
    UnityBegin(__FILE__);

    RUN_TEST(test_slip_send_frame_delimiter);
    RUN_TEST(test_slip_send_frame_data_normal_byte);
    RUN_TEST(test_slip_send_frame_data_end_escape);
    RUN_TEST(test_slip_send_frame_data_esc_escape);
    RUN_TEST(test_slip_send_frame_data_buf_normal);
    RUN_TEST(test_slip_send_frame_data_buf_with_escapes);
    RUN_TEST(test_slip_send_frame_data_buf_null_pointer);
    RUN_TEST(test_slip_send_frame_complete);
    RUN_TEST(test_slip_send_frame_null_data);
    RUN_TEST(test_slip_recv_byte_no_frame_start);
    RUN_TEST(test_slip_recv_byte_frame_start);
    RUN_TEST(test_slip_recv_byte_frame_end);
    RUN_TEST(test_slip_recv_byte_escape_sequence_end);
    RUN_TEST(test_slip_recv_byte_escape_sequence_esc);
    RUN_TEST(test_slip_recv_byte_invalid_escape);

    return UnityEnd();
}
