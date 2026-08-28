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
    /* The flush-interval tests swap the global TX hooks; put them back so the
     * order of RUN_TEST() calls cannot change what the other tests exercise. */
    slip_set_tx_fn(stub_lib_uart_tx_one_char);
    slip_set_flush_fn(NULL);
    slip_set_flush_interval(0);

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

/* ---- TX: early flushing (slip_set_flush_interval) ------------------------ */
/*
 * On USB-Serial/JTAG a flush cannot terminate a transfer once the endpoint
 * FIFO has drained, so a frame must never end on a full-size packet. These
 * tests treat the byte counts between flushes as the packets that reach the
 * host and check that none of them is full-size.
 */

#define TEST_PACKET_SIZE  64
#define MAX_FLUSH_POINTS  512

static size_t s_tx_count;
static size_t s_flush_points[MAX_FLUSH_POINTS];
static size_t s_flush_count;

static uint8_t counting_tx_one_char(uint8_t c)
{
    (void)c;
    s_tx_count++;
    return 0;
}

static void recording_flush(void)
{
    if (s_flush_count < MAX_FLUSH_POINTS) {
        s_flush_points[s_flush_count] = s_tx_count;
    }
    s_flush_count++;
}

static void start_recording(size_t flush_interval)
{
    s_tx_count = 0;
    s_flush_count = 0;
    memset(s_flush_points, 0, sizeof(s_flush_points));
    slip_set_tx_fn(counting_tx_one_char);
    slip_set_flush_fn(recording_flush);
    slip_set_flush_interval(flush_interval);
}

/* Longest run of bytes handed to the transport between two flushes, including
 * any bytes left after the last one. This is the largest packet the host sees. */
static size_t longest_run(void)
{
    TEST_ASSERT_LESS_OR_EQUAL_size_t(MAX_FLUSH_POINTS, s_flush_count);

    size_t longest = 0;
    size_t prev = 0;
    for (size_t i = 0; i < s_flush_count; i++) {
        size_t run = s_flush_points[i] - prev;
        if (run > longest) {
            longest = run;
        }
        prev = s_flush_points[i];
    }
    size_t tail = s_tx_count - prev;
    return tail > longest ? tail : longest;
}

void test_slip_flush_interval_disabled_flushes_once_per_frame(void)
{
    uint8_t payload[200] = {0};

    start_recording(0);
    TEST_ASSERT_TRUE(slip_send_frame(payload, sizeof(payload)));

    /* One flush, at the end of the frame, and no early ones */
    TEST_ASSERT_EQUAL_size_t(1, s_flush_count);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload) + 2, s_flush_points[0]);
}

void test_slip_flush_interval_disabled_can_end_on_full_packet(void)
{
    /* 62 payload bytes with nothing to escape make a 64-byte frame — the shape
     * that hangs a USB-Serial/JTAG read. Recorded here as the behaviour the
     * interval exists to prevent. */
    uint8_t payload[62] = {0};

    start_recording(0);
    TEST_ASSERT_TRUE(slip_send_frame(payload, sizeof(payload)));

    TEST_ASSERT_EQUAL_size_t(TEST_PACKET_SIZE, s_tx_count);
    TEST_ASSERT_EQUAL_size_t(TEST_PACKET_SIZE, longest_run());
}

void test_slip_flush_interval_never_ends_frame_on_full_packet(void)
{
    /* Sweep every payload length, so every frame length modulo the packet size
     * is covered, including the multiples that trigger the hang. */
    for (size_t size = 1; size <= 300; size++) {
        uint8_t payload[300] = {0};

        start_recording(TEST_PACKET_SIZE - 1);
        TEST_ASSERT_TRUE(slip_send_frame(payload, size));

        TEST_ASSERT_EQUAL_size_t(size + 2, s_tx_count);
        TEST_ASSERT_LESS_THAN_size_t(TEST_PACKET_SIZE, longest_run());
    }
}

void test_slip_flush_interval_counts_escaped_bytes(void)
{
    /* Escaping doubles bytes on the wire; the interval must count what is sent,
     * not what the caller passed in. */
    uint8_t payload[100];
    memset(payload, 0xC0, sizeof(payload));

    start_recording(TEST_PACKET_SIZE - 1);
    TEST_ASSERT_TRUE(slip_send_frame(payload, sizeof(payload)));

    TEST_ASSERT_EQUAL_size_t(2 * sizeof(payload) + 2, s_tx_count);
    TEST_ASSERT_LESS_THAN_size_t(TEST_PACKET_SIZE, longest_run());
}

void test_slip_flush_interval_read_flash_block(void)
{
    /* The shape reported in espressif/esptool#1184: a 4096-byte read-flash
     * block holding 62 bytes that need escaping, so the frame is 4160 bytes on
     * the wire — 65 whole packets. Reading that block over USB-Serial/JTAG hung
     * every time until the frame stopped ending on a full-size packet. */
    static uint8_t payload[4096];
    const size_t escaped_bytes = 62;

    memset(payload, 0x00, sizeof(payload));
    for (size_t i = 0; i < escaped_bytes; i++) {
        payload[i] = 0xC0;
    }
    const size_t wire_size = sizeof(payload) + escaped_bytes + 2;
    TEST_ASSERT_EQUAL_size_t(0, wire_size % TEST_PACKET_SIZE);

    start_recording(TEST_PACKET_SIZE - 1);
    TEST_ASSERT_TRUE(slip_send_frame(payload, sizeof(payload)));

    TEST_ASSERT_EQUAL_size_t(wire_size, s_tx_count);
    TEST_ASSERT_LESS_THAN_size_t(TEST_PACKET_SIZE, longest_run());
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
    RUN_TEST(test_slip_flush_interval_disabled_flushes_once_per_frame);
    RUN_TEST(test_slip_flush_interval_disabled_can_end_on_full_packet);
    RUN_TEST(test_slip_flush_interval_never_ends_frame_on_full_packet);
    RUN_TEST(test_slip_flush_interval_counts_escaped_bytes);
    RUN_TEST(test_slip_flush_interval_read_flash_block);
    RUN_TEST(test_slip_recv_byte_no_frame_start);
    RUN_TEST(test_slip_recv_byte_empty_frame_discarded);
    RUN_TEST(test_slip_recv_byte_complete_frame);
    RUN_TEST(test_slip_recv_byte_escape_end);
    RUN_TEST(test_slip_recv_byte_escape_esc);
    RUN_TEST(test_slip_recv_byte_invalid_escape);

    return UnityEnd();
}
