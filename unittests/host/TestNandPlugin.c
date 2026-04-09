/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

/**
 * Host unit tests for nand_plugin.c handlers.
 *
 * Handlers have signature:
 *   void handler(uint8_t command, const uint8_t *data, uint16_t size)
 *
 * They send responses directly via slip_send_frame().  Tests capture the
 * frame using a StubWithCallback and inspect the response status code and
 * value field.
 *
 * Response wire format (produced by plugin_send_response() in nand_plugin.c):
 *   buf[0]       = 0x01 (DIRECTION_RESPONSE)
 *   buf[1]       = command opcode
 *   buf[2-3]     = resp_data_size  (LE uint16)  = extra_len + 2
 *   buf[4-7]     = value           (LE uint32)
 *   buf[8..8+extra_len-1]  = extra data (optional)
 *   buf[last-1..last]      = status code (BE uint16)
 *   Total frame size = HEADER_SIZE(8) + resp_data_size
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "mock_nand.h"
#include "mock_slip.h"
#include "mock_md5.h"
#include "commands.h"
#include "command_handler.h"
#include "plugin_table.h"

/* ---- Dummy plugin_table (required by nand_plugin.c's extern declaration) -- */
plugin_cmd_handler_t plugin_table[PLUGIN_TABLE_SIZE];

/* ---- Forward declarations for functions under test ----------------------- */
void nand_plugin_attach(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_read_bbm(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_write_bbm(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_read_flash(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_write_flash_begin(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_write_flash_data(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_erase_flash(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_erase_region(uint8_t command, const uint8_t *data, uint16_t size);
void nand_plugin_read_page_debug(uint8_t command, const uint8_t *data, uint16_t size);

/* ---- Frame capture helpers ----------------------------------------------- */

#define CAPTURED_FRAME_MAX 512

static uint8_t s_captured_frame[CAPTURED_FRAME_MAX];
static size_t  s_captured_size;
static int     s_frame_count;

static void reset_capture(void)
{
    memset(s_captured_frame, 0, sizeof(s_captured_frame));
    s_captured_size = 0;
    s_frame_count   = 0;
}

/* CMock callback registered via slip_send_frame_StubWithCallback().
 * Captures only the first frame. */
static void mock_slip_send_frame_cb(const void *data, size_t size, int num_calls)
{
    (void)num_calls;
    if (s_frame_count == 0 && size <= CAPTURED_FRAME_MAX) {
        memcpy(s_captured_frame, data, size);
        s_captured_size = size;
    }
    s_frame_count++;
}

/* Extract the 2-byte BE status from the last two bytes of the captured frame */
static uint16_t get_response_status(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(2, s_captured_size);
    return (uint16_t)(((uint16_t)s_captured_frame[s_captured_size - 2] << 8) |
                      (uint16_t)s_captured_frame[s_captured_size - 1]);
}

/* Extract the 4-byte LE value from frame bytes [4..7] */
static uint32_t get_response_value(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(8, s_captured_size);
    return (uint32_t)s_captured_frame[4]
           | ((uint32_t)s_captured_frame[5] << 8)
           | ((uint32_t)s_captured_frame[6] << 16)
           | ((uint32_t)s_captured_frame[7] << 24);
}

/* ---- Callbacks for nand_plugin_read_flash -------------------------------- */

/*
 * After the first data packet is sent, the handler checks for ACKs.
 * We simulate one ACK by returning true from slip_is_frame_complete on call 1+,
 * and returning a buffer with acked_data_size = 2048 from slip_get_frame_data.
 */
static uint32_t s_read_flash_ack_size;

static bool slip_is_frame_complete_read_flash_cb(int num_calls)
{
    /* Return true starting from the second check (after the first packet is sent) */
    return num_calls >= 1;
}

static const uint8_t *slip_get_frame_data_read_flash_cb(size_t *length, int num_calls)
{
    (void)num_calls;
    if (length) {
        *length = sizeof(s_read_flash_ack_size);
    }
    return (const uint8_t *)&s_read_flash_ack_size;
}

/* ---- Unity setUp / tearDown --------------------------------------------- */

void setUp(void)
{
    reset_capture();
    mock_nand_Init();
    mock_slip_Init();
    mock_md5_Init();
    /* Register the frame capture callback for every test */
    slip_send_frame_StubWithCallback(mock_slip_send_frame_cb);
}

void tearDown(void)
{
    mock_nand_Verify();
    mock_nand_Destroy();
    mock_slip_Verify();
    mock_slip_Destroy();
    mock_md5_Verify();
    mock_md5_Destroy();
}

/* ======================================================================== */
/* nand_plugin_attach                                                        */
/* ======================================================================== */

void test_attach_success(void)
{
    uint8_t data[SPI_NAND_ATTACH_SIZE] = {0x01, 0x02, 0x03, 0x04};

    stub_target_nand_attach_ExpectAndReturn(0x04030201, 0);
    stub_target_nand_read_id_IgnoreAndReturn(0);
    stub_target_nand_read_register_IgnoreAndReturn(0);

    nand_plugin_attach(ESP_SPI_NAND_ATTACH, data, SPI_NAND_ATTACH_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
    TEST_ASSERT_EQUAL_INT(1, s_frame_count);
}

void test_attach_target_failure(void)
{
    uint8_t data[SPI_NAND_ATTACH_SIZE] = {0};

    stub_target_nand_attach_ExpectAndReturn(0, -1);

    nand_plugin_attach(ESP_SPI_NAND_ATTACH, data, SPI_NAND_ATTACH_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_FAILED_SPI_OP, get_response_status());
    TEST_ASSERT_EQUAL_UINT32((uint32_t) -1, get_response_value());
}

void test_attach_bad_size(void)
{
    uint8_t data[SPI_NAND_ATTACH_SIZE + 1] = {0};

    nand_plugin_attach(ESP_SPI_NAND_ATTACH, data, SPI_NAND_ATTACH_SIZE + 1);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_read_bbm                                                      */
/* ======================================================================== */

void test_read_bbm_success(void)
{
    uint8_t data[SPI_NAND_READ_BBM_SIZE] = {0x05, 0x00, 0x00, 0x00};

    stub_target_nand_read_bbm_IgnoreAndReturn(0);

    nand_plugin_read_bbm(ESP_SPI_NAND_READ_BBM, data, SPI_NAND_READ_BBM_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
}

void test_read_bbm_target_failure(void)
{
    uint8_t data[SPI_NAND_READ_BBM_SIZE] = {0};

    stub_target_nand_read_bbm_IgnoreAndReturn(-1);

    nand_plugin_read_bbm(ESP_SPI_NAND_READ_BBM, data, SPI_NAND_READ_BBM_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_FAILED_SPI_OP, get_response_status());
}

void test_read_bbm_bad_size(void)
{
    uint8_t data[SPI_NAND_READ_BBM_SIZE - 1] = {0};

    nand_plugin_read_bbm(ESP_SPI_NAND_READ_BBM, data, SPI_NAND_READ_BBM_SIZE - 1);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_write_bbm                                                     */
/* ======================================================================== */

void test_write_bbm_success(void)
{
    uint8_t data[SPI_NAND_WRITE_BBM_SIZE] = {0x0A, 0x00, 0x00, 0x00, 0x01};

    stub_target_nand_write_bbm_ExpectAndReturn(10, 1, 0);

    nand_plugin_write_bbm(ESP_SPI_NAND_WRITE_BBM, data, SPI_NAND_WRITE_BBM_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
}

void test_write_bbm_target_failure(void)
{
    uint8_t data[SPI_NAND_WRITE_BBM_SIZE] = {0};

    stub_target_nand_write_bbm_ExpectAndReturn(0, 0, -1);

    nand_plugin_write_bbm(ESP_SPI_NAND_WRITE_BBM, data, SPI_NAND_WRITE_BBM_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_FAILED_SPI_OP, get_response_status());
}

void test_write_bbm_bad_size(void)
{
    uint8_t data[SPI_NAND_WRITE_BBM_SIZE + 1] = {0};

    nand_plugin_write_bbm(ESP_SPI_NAND_WRITE_BBM, data, SPI_NAND_WRITE_BBM_SIZE + 1);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_read_page_debug                                               */
/* ======================================================================== */

void test_read_page_debug_success(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE] = {0x02, 0x00, 0x00, 0x00};

    stub_target_nand_get_page_size_ExpectAndReturn(2048);
    stub_target_nand_read_page_IgnoreAndReturn(0);

    nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                SPI_NAND_READ_PAGE_DEBUG_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
    TEST_ASSERT_EQUAL_UINT32(2, get_response_value());
}

void test_read_page_debug_target_failure(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE] = {0};

    stub_target_nand_get_page_size_ExpectAndReturn(2048);
    stub_target_nand_read_page_IgnoreAndReturn(-1);

    nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                SPI_NAND_READ_PAGE_DEBUG_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_FAILED_SPI_OP, get_response_status());
}

void test_read_page_debug_bad_size(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE + 1] = {0};

    nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                SPI_NAND_READ_PAGE_DEBUG_SIZE + 1);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_write_flash_begin                                             */
/* ======================================================================== */

void test_write_flash_begin_success(void)
{
    /* offset=0, total_size=4096, block_size=131072, packet_size=4096 */
    uint8_t data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x10, 0x00, 0x00,   /* total_size = 4096 */
        0x00, 0x00, 0x00, 0x00,   /* block_size (unused) */
        0x00, 0x00, 0x00, 0x00,   /* packet_size (unused) */
    };

    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN, data,
                                  SPI_NAND_WRITE_FLASH_BEGIN_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
}

void test_write_flash_begin_bad_size(void)
{
    uint8_t data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE - 1] = {0};

    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN, data,
                                  SPI_NAND_WRITE_FLASH_BEGIN_SIZE - 1);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

void test_write_flash_begin_unaligned_offset(void)
{
    /* offset=1 — not page-aligned */
    uint8_t data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE] = {
        0x01, 0x00, 0x00, 0x00,   /* offset = 1 */
        0x00, 0x10, 0x00, 0x00,   /* total_size */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN, data,
                                  SPI_NAND_WRITE_FLASH_BEGIN_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_write_flash_data                                              */
/* ======================================================================== */

/*
 * The checksum field lives at data[-4] (4 bytes before the payload pointer).
 * In the real protocol the full command packet is:
 *   [0-3]   (unused, occupied by HEADER direction/cmd/size bytes)
 *   [4-7]   checksum field  ← data - 4 in the handler
 *   [8-15]  write_flash_data header (data_len at [8-11], padding at [12-15])
 *   [16+]   flash data     ← flash_data = data + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE
 *
 * We build a full buffer here and pass &buf[8] as the `data` pointer so that
 * buf[4-7] is accessible as data[-4].
 *
 * Test ordering dependency: test_write_flash_data_not_in_progress must run
 * BEFORE any test_write_flash_begin_success, while in_progress is still false
 * (BSS zero-initialised at program start).
 * test_write_flash_data_success must run AFTER begin sets up the state.
 */

/* Minimum data buffer:
 *   HEADER_SIZE(8) before the payload + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE(16) header
 *   + actual data bytes */
#define WFD_PREAMBLE_SIZE  8    /* bytes before payload pointer (checksum at [4-7]) */
#define WFD_DATA_SIZE      16   /* actual flash data bytes in this test */

void test_write_flash_data_bad_header_size(void)
{
    /* size < SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE → RESPONSE_NOT_ENOUGH_DATA.
     * This check fires before in_progress is examined, so test can run at any time. */
    uint8_t small[4] = {0};

    nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                 small,  /* data */
                                 4); /* size < 16 */

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_NOT_ENOUGH_DATA, get_response_status());
}

void test_write_flash_data_not_in_progress(void)
{
    /* This test relies on in_progress being false (BSS zero-init at start).
     * It must execute before any successful write_flash_begin call.
     * The handler checks in_progress BEFORE the checksum, so we can use
     * a zeroed payload buffer safely. */
    uint8_t payload[SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE] = {0};

    nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                 payload, /* data */
                                 SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_NOT_IN_FLASH_MODE, get_response_status());
}

void test_write_flash_data_success(void)
{
    /* 1. Call begin to set up write state: offset=0, total_size=0x1000 (>WFD_DATA_SIZE)
     *    so that writing WFD_DATA_SIZE bytes does NOT exhaust total_remaining,
     *    avoiding a page-flush (which would require additional mocks). */
    uint8_t begin_data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x10, 0x00, 0x00,   /* total_size = 0x1000 = 4096 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN,
                                  begin_data, SPI_NAND_WRITE_FLASH_BEGIN_SIZE);
    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
    reset_capture();
    slip_send_frame_StubWithCallback(mock_slip_send_frame_cb);

    /* 2. Build the write_data packet.
     *    Full buffer layout (all allocated on stack):
     *      [0-7]   preamble (checksum in bytes [4-7], LE)
     *      [8-23]  write_flash_data header (data_len at [8-11], rest zeros)
     *      [24-39] actual flash bytes
     *    The handler's `data` pointer = &full_packet[8] (== &full_packet[PREAMBLE_SIZE])
     *    so data[-4] == full_packet[4], which is the checksum. */
    uint8_t flash_bytes[WFD_DATA_SIZE];
    for (int i = 0; i < WFD_DATA_SIZE; i++) {
        flash_bytes[i] = (uint8_t)i;
    }

    /* Compute checksum = 0xEF XOR all flash_bytes */
    uint32_t checksum = 0xEF;
    for (int i = 0; i < WFD_DATA_SIZE; i++) {
        checksum ^= flash_bytes[i];
    }

    uint8_t full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + WFD_DATA_SIZE];
    memset(full_packet, 0, sizeof(full_packet));

    /* Checksum at preamble[4-7] (LE) */
    full_packet[4] = (uint8_t)(checksum & 0xFF);
    full_packet[5] = (uint8_t)((checksum >> 8) & 0xFF);
    full_packet[6] = (uint8_t)((checksum >> 16) & 0xFF);
    full_packet[7] = (uint8_t)((checksum >> 24) & 0xFF);

    /* data_len = WFD_DATA_SIZE at payload[0-3] (LE) */
    full_packet[WFD_PREAMBLE_SIZE + 0] = WFD_DATA_SIZE;

    /* Flash data */
    memcpy(&full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE],
           flash_bytes, WFD_DATA_SIZE);

    /* total_remaining = 4096, actual_data_size = 16 → total_remaining stays positive,
     * no page flush needed (page_buf_filled < NAND_PAGE_SIZE). */
    nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                 &full_packet[WFD_PREAMBLE_SIZE],
                                 SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + WFD_DATA_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_erase_flash                                                   */
/* ======================================================================== */

void test_erase_flash_success(void)
{
    /* Ignore all 1024 block erase calls */
    stub_target_nand_erase_block_IgnoreAndReturn(0);

    nand_plugin_erase_flash(ESP_SPI_NAND_ERASE_FLASH, NULL, 0);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
}

void test_erase_flash_spi_failure(void)
{
    /* First erase call fails */
    stub_target_nand_erase_block_ExpectAndReturn(0, -1);

    nand_plugin_erase_flash(ESP_SPI_NAND_ERASE_FLASH, NULL, 0);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_FAILED_SPI_OP, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_erase_region                                                  */
/* ======================================================================== */

void test_erase_region_bad_size(void)
{
    uint8_t data[4] = {0};  /* too short */

    nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, data, 4);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

void test_erase_region_unaligned(void)
{
    /* offset=1 (not block-aligned) */
    uint8_t data[ERASE_REGION_SIZE] = {
        0x01, 0x00, 0x00, 0x00,   /* offset = 1 */
        0x00, 0x00, 0x02, 0x00,   /* size = 131072 = one block */
    };

    nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, data, ERASE_REGION_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

void test_erase_region_success(void)
{
    /* Erase one block starting at offset 0 (block 0) */
    uint8_t data[ERASE_REGION_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x00, 0x02, 0x00,   /* size = 0x20000 = 131072 = one block */
    };

    /* One block = 1 erase_block call with page_number=0 */
    stub_target_nand_erase_block_ExpectAndReturn(0, 0);

    nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, data, ERASE_REGION_SIZE);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
}

/* ======================================================================== */
/* nand_plugin_read_flash                                                    */
/* ======================================================================== */

void test_read_flash_bad_size(void)
{
    uint8_t data[4] = {0};  /* too short */

    nand_plugin_read_flash(ESP_SPI_NAND_READ_FLASH, data, 4);

    TEST_ASSERT_EQUAL_UINT16(RESPONSE_BAD_DATA_LEN, get_response_status());
}

void test_read_flash_success(void)
{
    /* offset=0, read_size=2048 (one page), packet_size=2048 */
    uint8_t data[SPI_NAND_READ_FLASH_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x08, 0x00, 0x00,   /* read_size = 2048 */
        0x00, 0x08, 0x00, 0x00,   /* packet_size = 2048 */
        0x00, 0x00, 0x00, 0x00,   /* max_inflight (unused) */
    };

    stub_target_nand_get_page_size_ExpectAndReturn(2048);
    stub_target_nand_read_page_IgnoreAndReturn(0);

    /* ACK loop setup:
     *   - slip_recv_reset: ignore all calls
     *   - slip_is_frame_complete: return false on call 0 (before packet sent),
     *       true on call 1+ (after packet sent) — triggers ACK processing
     *   - slip_get_frame_data: return a 4-byte buffer with acked_data_size = 2048
     *       so the loop exits (acked_data_size >= read_size) */
    s_read_flash_ack_size = 2048;
    slip_recv_reset_Ignore();
    slip_is_frame_complete_StubWithCallback(slip_is_frame_complete_read_flash_cb);
    slip_get_frame_data_StubWithCallback(slip_get_frame_data_read_flash_cb);

    /* MD5 calls */
    stub_lib_md5_init_Ignore();
    stub_lib_md5_update_Ignore();
    stub_lib_md5_final_Ignore();

    nand_plugin_read_flash(ESP_SPI_NAND_READ_FLASH, data, SPI_NAND_READ_FLASH_SIZE);

    /* Frames sent:
     *   frame 0: initial SUCCESS response
     *   frame 1: data packet (2048 bytes)
     *   frame 2: MD5 digest (16 bytes)
     * First captured frame (frame 0) has RESPONSE_SUCCESS status. */
    TEST_ASSERT_GREATER_OR_EQUAL(2, s_frame_count);
    TEST_ASSERT_EQUAL_UINT16(RESPONSE_SUCCESS, get_response_status());
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

    /* write_flash_data tests that require in_progress=false must run BEFORE
     * any test_write_flash_begin_success (which sets in_progress=true). */
    RUN_TEST(test_write_flash_data_bad_header_size);
    RUN_TEST(test_write_flash_data_not_in_progress);

    RUN_TEST(test_write_flash_begin_success);
    RUN_TEST(test_write_flash_begin_bad_size);
    RUN_TEST(test_write_flash_begin_unaligned_offset);

    RUN_TEST(test_write_flash_data_success);

    RUN_TEST(test_erase_flash_success);
    RUN_TEST(test_erase_flash_spi_failure);

    RUN_TEST(test_erase_region_bad_size);
    RUN_TEST(test_erase_region_unaligned);
    RUN_TEST(test_erase_region_success);

    RUN_TEST(test_read_flash_bad_size);
    RUN_TEST(test_read_flash_success);

    return UnityEnd();
}
