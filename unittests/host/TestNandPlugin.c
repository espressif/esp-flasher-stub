/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

/**
 * Host unit tests for nand_plugin.c handlers.
 *
 * Handlers have signature:
 *   int handler(uint8_t command, const uint8_t *data, uint32_t len,
 *               struct command_response_data *resp)
 *
 * Under the new ABI (Option A), handlers fill *resp and return a status code.
 * The dispatcher (command_handler.c:s_send_response) owns primary SLIP framing.
 * Tests assert on the handler return value and resp fields directly.
 *
 * For nand_plugin_read_flash the handler registers resp->post_process; calling
 * resp.post_process(&fake_ctx) exercises the streaming loop (data packets + MD5).
 * Streaming frames from the post-process are still captured via the
 * slip_send_frame stub. See command_handler.c:s_send_response for the primary
 * response framing path used for all other handlers.
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
int nand_plugin_attach(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp);
int nand_plugin_read_bbm(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp);
int nand_plugin_write_bbm(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp);
int nand_plugin_read_flash(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp);
int nand_plugin_write_flash_begin(uint8_t command, const uint8_t *data, uint32_t len,
                                  struct command_response_data *resp);
int nand_plugin_write_flash_data(uint8_t command, const uint8_t *data, uint32_t len,
                                 struct command_response_data *resp);
int nand_plugin_erase_flash(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp);
int nand_plugin_erase_region(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp);
int nand_plugin_read_page_debug(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp);
int nand_plugin_write_flash_end(uint8_t command, const uint8_t *data, uint32_t len,
                                struct command_response_data *resp);

/* ---- Frame capture helpers (used by read_flash post-process tests) -------- */
/*
 * The primary response for simple handlers is framed by the dispatcher
 * (command_handler.c:s_send_response) — not captured here.  This helper
 * captures frames emitted directly by the streaming post-process loop
 * (data packets and MD5 digest).
 */

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
    /* Register the frame capture callback for streaming-frame tests */
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
    struct command_response_data resp = {0};

    stub_target_nand_attach_ExpectAndReturn(0x04030201, 0);
    stub_target_nand_read_id_IgnoreAndReturn(0);
    stub_target_nand_read_register_IgnoreAndReturn(0);

    int rc = nand_plugin_attach(ESP_SPI_NAND_ATTACH, data, SPI_NAND_ATTACH_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
    TEST_ASSERT_EQUAL_UINT16(1, resp.data_size);
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

    stub_target_nand_get_page_size_ExpectAndReturn(2048);
    stub_target_nand_read_page_IgnoreAndReturn(0);

    int rc = nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                         SPI_NAND_READ_PAGE_DEBUG_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
    TEST_ASSERT_EQUAL_UINT32(2, resp.value);
    TEST_ASSERT_EQUAL_UINT16(16, resp.data_size);
}

void test_read_page_debug_target_failure(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE] = {0};
    struct command_response_data resp = {0};

    stub_target_nand_get_page_size_ExpectAndReturn(2048);
    stub_target_nand_read_page_IgnoreAndReturn(-1);

    int rc = nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                         SPI_NAND_READ_PAGE_DEBUG_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_FAILED_SPI_OP, rc);
}

void test_read_page_debug_bad_size(void)
{
    uint8_t data[SPI_NAND_READ_PAGE_DEBUG_SIZE + 1] = {0};
    struct command_response_data resp = {0};

    int rc = nand_plugin_read_page_debug(ESP_SPI_NAND_READ_PAGE_DEBUG, data,
                                         SPI_NAND_READ_PAGE_DEBUG_SIZE + 1, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
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
    struct command_response_data resp = {0};

    int rc = nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN, data,
                                           SPI_NAND_WRITE_FLASH_BEGIN_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

void test_write_flash_begin_bad_size(void)
{
    uint8_t data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE - 1] = {0};
    struct command_response_data resp = {0};

    int rc = nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN, data,
                                           SPI_NAND_WRITE_FLASH_BEGIN_SIZE - 1, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
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
    struct command_response_data resp = {0};

    int rc = nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN, data,
                                           SPI_NAND_WRITE_FLASH_BEGIN_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
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
    struct command_response_data resp = {0};

    int rc = nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                          small,  /* data */
                                          4, &resp); /* size < 16 */

    TEST_ASSERT_EQUAL_INT(RESPONSE_NOT_ENOUGH_DATA, rc);
}

void test_write_flash_data_not_in_progress(void)
{
    /* This test relies on in_progress being false (BSS zero-init at start).
     * It must execute before any successful write_flash_begin call.
     * The handler checks in_progress BEFORE the checksum, so we can use
     * a zeroed payload buffer safely. */
    uint8_t payload[SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE] = {0};
    struct command_response_data resp = {0};

    int rc = nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                          payload, /* data */
                                          SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_NOT_IN_FLASH_MODE, rc);
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
    struct command_response_data resp = {0};
    int rc = nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN,
                                           begin_data, SPI_NAND_WRITE_FLASH_BEGIN_SIZE, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);

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
    memset(&resp, 0, sizeof(resp));
    rc = nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                      &full_packet[WFD_PREAMBLE_SIZE],
                                      SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + WFD_DATA_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

/* ======================================================================== */
/* nand_plugin_erase_flash                                                   */
/* ======================================================================== */

void test_erase_flash_success(void)
{
    struct command_response_data resp = {0};

    /* Ignore all 1024 block erase calls */
    stub_target_nand_erase_block_IgnoreAndReturn(0);

    int rc = nand_plugin_erase_flash(ESP_SPI_NAND_ERASE_FLASH, NULL, 0, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

void test_erase_flash_spi_failure(void)
{
    struct command_response_data resp = {0};

    /* First erase call fails */
    stub_target_nand_erase_block_ExpectAndReturn(0, -1);

    int rc = nand_plugin_erase_flash(ESP_SPI_NAND_ERASE_FLASH, NULL, 0, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_FAILED_SPI_OP, rc);
}

/* ======================================================================== */
/* nand_plugin_erase_region                                                  */
/* ======================================================================== */

void test_erase_region_bad_size(void)
{
    uint8_t data[4] = {0};  /* too short */
    struct command_response_data resp = {0};

    int rc = nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, data, 4, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

void test_erase_region_unaligned(void)
{
    /* offset=1 (not block-aligned) */
    uint8_t data[ERASE_REGION_SIZE] = {
        0x01, 0x00, 0x00, 0x00,   /* offset = 1 */
        0x00, 0x00, 0x02, 0x00,   /* size = 131072 = one block */
    };
    struct command_response_data resp = {0};

    int rc = nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, data, ERASE_REGION_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

void test_erase_region_success(void)
{
    /* Erase one block starting at offset 0 (block 0) */
    uint8_t data[ERASE_REGION_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x00, 0x02, 0x00,   /* size = 0x20000 = 131072 = one block */
    };
    struct command_response_data resp = {0};

    /* One block = 1 erase_block call with page_number=0 */
    stub_target_nand_erase_block_ExpectAndReturn(0, 0);

    int rc = nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, data, ERASE_REGION_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

/* ======================================================================== */
/* nand_plugin_read_flash                                                    */
/* ======================================================================== */

void test_read_flash_bad_size(void)
{
    uint8_t data[4] = {0};  /* too short */
    struct command_response_data resp = {0};

    int rc = nand_plugin_read_flash(ESP_SPI_NAND_READ_FLASH, data, 4, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
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

    struct command_response_data resp = {0};

    /* Step (a): call handler — must return RESPONSE_SUCCESS and register post_process */
    int rc = nand_plugin_read_flash(ESP_SPI_NAND_READ_FLASH, data, SPI_NAND_READ_FLASH_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
    TEST_ASSERT_NOT_NULL(resp.post_process);

    /* Step (b): invoke post_process — exercises the streaming loop.
     *
     * ACK loop setup:
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

    stub_target_nand_read_page_IgnoreAndReturn(0);

    /* Fake cmd_ctx — post_process does not use ctx fields */
    struct cmd_ctx fake_ctx = {0};
    int post_rc = resp.post_process(&fake_ctx);

    /* Streaming frames:
     *   frame 0: data packet (2048 bytes)
     *   frame 1: MD5 digest (16 bytes)
     * post_process must return RESPONSE_SUCCESS. */
    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, post_rc);
    TEST_ASSERT_GREATER_OR_EQUAL(2, s_frame_count);
}

/* ======================================================================== */
/* nand_plugin_write_flash_end                                               */
/* ======================================================================== */

/* Must run before any begin_success to catch the not-in-progress path. */
void test_write_flash_end_not_in_flash_mode(void)
{
    uint8_t data[SPI_NAND_WRITE_FLASH_END_SIZE] = {0x00, 0x00, 0x00, 0x00};
    struct command_response_data resp = {0};

    int rc = nand_plugin_write_flash_end(ESP_SPI_NAND_WRITE_FLASH_END, data,
                                         SPI_NAND_WRITE_FLASH_END_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_NOT_IN_FLASH_MODE, rc);
}

void test_write_flash_end_bad_size(void)
{
    uint8_t data[3] = {0};
    struct command_response_data resp = {0};

    int rc = nand_plugin_write_flash_end(ESP_SPI_NAND_WRITE_FLASH_END, data, 3, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

void test_write_flash_end_bytes_remaining(void)
{
    /* Begin with total_size=0x1000; call end without any data — total_remaining != 0 */
    uint8_t begin_data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x10, 0x00, 0x00,   /* total_size = 0x1000 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    struct command_response_data resp = {0};
    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN,
                                  begin_data, SPI_NAND_WRITE_FLASH_BEGIN_SIZE, &resp);

    uint8_t end_data[SPI_NAND_WRITE_FLASH_END_SIZE] = {0x00, 0x00, 0x00, 0x00};
    memset(&resp, 0, sizeof(resp));
    int rc = nand_plugin_write_flash_end(ESP_SPI_NAND_WRITE_FLASH_END, end_data,
                                         SPI_NAND_WRITE_FLASH_END_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_BAD_DATA_LEN, rc);
}

void test_write_flash_end_success(void)
{
    /* Begin with total_size=NAND_PAGE_SIZE (2048) — one full page */
    uint8_t begin_data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x08, 0x00, 0x00,   /* total_size = 2048 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    struct command_response_data resp = {0};
    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN,
                                  begin_data, SPI_NAND_WRITE_FLASH_BEGIN_SIZE, &resp);

    /* Build a write_data packet with 2048 bytes (one full page) */
    static uint8_t flash_bytes[2048];
    for (int i = 0; i < 2048; i++) {
        flash_bytes[i] = (uint8_t)(i & 0xFF);
    }

    uint32_t checksum = 0xEF;
    for (int i = 0; i < 2048; i++) {
        checksum ^= flash_bytes[i];
    }

    uint8_t full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + 2048];
    memset(full_packet, 0, sizeof(full_packet));

    full_packet[4] = (uint8_t)(checksum & 0xFF);
    full_packet[5] = (uint8_t)((checksum >> 8) & 0xFF);
    full_packet[6] = (uint8_t)((checksum >> 16) & 0xFF);
    full_packet[7] = (uint8_t)((checksum >> 24) & 0xFF);

    /* data_len = 2048 */
    full_packet[WFD_PREAMBLE_SIZE + 0] = 0x00;
    full_packet[WFD_PREAMBLE_SIZE + 1] = 0x08;
    full_packet[WFD_PREAMBLE_SIZE + 2] = 0x00;
    full_packet[WFD_PREAMBLE_SIZE + 3] = 0x00;

    memcpy(&full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE],
           flash_bytes, 2048);

    /* Full page → triggers erase_block + write_page on flush */
    stub_target_nand_erase_block_ExpectAndReturn(0, 0);
    stub_target_nand_write_page_IgnoreAndReturn(0);

    memset(&resp, 0, sizeof(resp));
    int rc = nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                          &full_packet[WFD_PREAMBLE_SIZE],
                                          SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + 2048, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);

    /* END — page_buf_filled=0 so no flush needed */
    uint8_t end_data[SPI_NAND_WRITE_FLASH_END_SIZE] = {0x00, 0x00, 0x00, 0x00};
    memset(&resp, 0, sizeof(resp));
    rc = nand_plugin_write_flash_end(ESP_SPI_NAND_WRITE_FLASH_END, end_data,
                                     SPI_NAND_WRITE_FLASH_END_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

void test_write_flash_end_final_partial_flush(void)
{
    /* Begin with total_size=16 (less than one page) */
    uint8_t begin_data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x10, 0x00, 0x00, 0x00,   /* total_size = 16 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    struct command_response_data resp = {0};
    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN,
                                  begin_data, SPI_NAND_WRITE_FLASH_BEGIN_SIZE, &resp);

    /* Write 16 bytes — partial page, no mid-stream flush */
    uint8_t flash_bytes[WFD_DATA_SIZE];
    for (int i = 0; i < WFD_DATA_SIZE; i++) {
        flash_bytes[i] = (uint8_t)i;
    }

    uint32_t checksum = 0xEF;
    for (int i = 0; i < WFD_DATA_SIZE; i++) {
        checksum ^= flash_bytes[i];
    }

    uint8_t full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + WFD_DATA_SIZE];
    memset(full_packet, 0, sizeof(full_packet));

    full_packet[4] = (uint8_t)(checksum & 0xFF);
    full_packet[5] = (uint8_t)((checksum >> 8) & 0xFF);
    full_packet[6] = (uint8_t)((checksum >> 16) & 0xFF);
    full_packet[7] = (uint8_t)((checksum >> 24) & 0xFF);

    full_packet[WFD_PREAMBLE_SIZE + 0] = WFD_DATA_SIZE;

    memcpy(&full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE],
           flash_bytes, WFD_DATA_SIZE);

    memset(&resp, 0, sizeof(resp));
    int rc = nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                          &full_packet[WFD_PREAMBLE_SIZE],
                                          SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + WFD_DATA_SIZE, &resp);
    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);

    /* END — page_buf_filled=16 → must pad and flush */
    stub_target_nand_erase_block_ExpectAndReturn(0, 0);
    stub_target_nand_write_page_IgnoreAndReturn(0);

    uint8_t end_data[SPI_NAND_WRITE_FLASH_END_SIZE] = {0x00, 0x00, 0x00, 0x00};
    memset(&resp, 0, sizeof(resp));
    rc = nand_plugin_write_flash_end(ESP_SPI_NAND_WRITE_FLASH_END, end_data,
                                     SPI_NAND_WRITE_FLASH_END_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_SUCCESS, rc);
}

/* ======================================================================== */
/* nand_plugin_erase_flash — NAND_ERR_ERASE_FAILED                          */
/* ======================================================================== */

void test_erase_flash_erase_fail(void)
{
    struct command_response_data resp = {0};

    /* First block erase call returns NAND_ERR_ERASE_FAILED (-2) */
    stub_target_nand_erase_block_ExpectAndReturn(0, -2);  /* NAND_ERR_ERASE_FAILED */

    int rc = nand_plugin_erase_flash(ESP_SPI_NAND_ERASE_FLASH, NULL, 0, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_NAND_ERASE_FAILED, rc);
}

/* ======================================================================== */
/* nand_plugin_erase_region — NAND_ERR_ERASE_FAILED                         */
/* ======================================================================== */

void test_erase_region_erase_fail(void)
{
    /* Erase one block starting at offset 0 */
    uint8_t data[ERASE_REGION_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x00, 0x02, 0x00,   /* size = 0x20000 = one block */
    };
    struct command_response_data resp = {0};

    /* Erase call returns NAND_ERR_ERASE_FAILED (-2) */
    stub_target_nand_erase_block_ExpectAndReturn(0, -2);  /* NAND_ERR_ERASE_FAILED */

    int rc = nand_plugin_erase_region(ESP_SPI_NAND_ERASE_REGION, data, ERASE_REGION_SIZE, &resp);

    TEST_ASSERT_EQUAL_INT(RESPONSE_NAND_ERASE_FAILED, rc);
}

/* ======================================================================== */
/* s_nand_write_flush_page (via write_flash_data) — P_FAIL / E_FAIL         */
/* ======================================================================== */

/*
 * Helper: build a write_flash_begin + write_flash_data sequence that triggers
 * a full-page flush (erase + write).  Returns the rc of write_flash_data.
 *
 * page_buf_filled is carried over across tests because s_nand_write_state is
 * static; so we must call begin first to reset it.
 *
 * erase_ret / write_ret: mock return values for the two stub_target calls.
 * If erase_ret != 0 the write mock is NOT expected (erase fails first).
 */
static int _run_flush_test(int erase_ret, int write_ret)
{
    /* Begin: offset=0, total_size=2048 (one page) */
    uint8_t begin_data[SPI_NAND_WRITE_FLASH_BEGIN_SIZE] = {
        0x00, 0x00, 0x00, 0x00,   /* offset = 0 */
        0x00, 0x08, 0x00, 0x00,   /* total_size = 2048 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    struct command_response_data resp = {0};
    nand_plugin_write_flash_begin(ESP_SPI_NAND_WRITE_FLASH_BEGIN,
                                  begin_data, SPI_NAND_WRITE_FLASH_BEGIN_SIZE, &resp);

    /* Full 2048-byte page — triggers erase_block + write_page on flush */
    static uint8_t flash_bytes[2048];
    for (int i = 0; i < 2048; i++) {
        flash_bytes[i] = (uint8_t)(i & 0xFF);
    }

    uint32_t checksum = 0xEF;
    for (int i = 0; i < 2048; i++) {
        checksum ^= flash_bytes[i];
    }

    uint8_t full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + 2048];
    memset(full_packet, 0, sizeof(full_packet));

    full_packet[4] = (uint8_t)(checksum & 0xFF);
    full_packet[5] = (uint8_t)((checksum >> 8) & 0xFF);
    full_packet[6] = (uint8_t)((checksum >> 16) & 0xFF);
    full_packet[7] = (uint8_t)((checksum >> 24) & 0xFF);

    /* data_len = 2048 (LE) */
    full_packet[WFD_PREAMBLE_SIZE + 0] = 0x00;
    full_packet[WFD_PREAMBLE_SIZE + 1] = 0x08;
    full_packet[WFD_PREAMBLE_SIZE + 2] = 0x00;
    full_packet[WFD_PREAMBLE_SIZE + 3] = 0x00;

    memcpy(&full_packet[WFD_PREAMBLE_SIZE + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE],
           flash_bytes, 2048);

    /* Set up mocks */
    stub_target_nand_erase_block_ExpectAndReturn(0, erase_ret);
    if (erase_ret == 0) {
        /* IgnoreAndReturn skips per-arg checks (IgnoreArg_* is not generated for
         * this function by CMock). The test only cares about the return code. */
        stub_target_nand_write_page_IgnoreAndReturn(write_ret);
    }

    memset(&resp, 0, sizeof(resp));
    return nand_plugin_write_flash_data(ESP_SPI_NAND_WRITE_FLASH_DATA,
                                        &full_packet[WFD_PREAMBLE_SIZE],
                                        SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE + 2048, &resp);
}

void test_write_flush_erase_fail(void)
{
    int rc = _run_flush_test(-2, 0);  /* NAND_ERR_ERASE_FAILED, write not reached */
    TEST_ASSERT_EQUAL_INT(RESPONSE_NAND_ERASE_FAILED, rc);
}

void test_write_flush_program_fail(void)
{
    int rc = _run_flush_test(0, -3);  /* erase OK, NAND_ERR_PROGRAM_FAILED */
    TEST_ASSERT_EQUAL_INT(RESPONSE_NAND_PROGRAM_FAILED, rc);
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

    /* write_flash_data/end tests that require in_progress=false must run BEFORE
     * any test_write_flash_begin_success (which sets in_progress=true). */
    RUN_TEST(test_write_flash_data_bad_header_size);
    RUN_TEST(test_write_flash_data_not_in_progress);
    RUN_TEST(test_write_flash_end_not_in_flash_mode);
    RUN_TEST(test_write_flash_end_bad_size);

    RUN_TEST(test_write_flash_begin_success);
    RUN_TEST(test_write_flash_begin_bad_size);
    RUN_TEST(test_write_flash_begin_unaligned_offset);

    RUN_TEST(test_write_flash_data_success);

    RUN_TEST(test_write_flash_end_bytes_remaining);
    RUN_TEST(test_write_flash_end_success);
    RUN_TEST(test_write_flash_end_final_partial_flush);

    RUN_TEST(test_erase_flash_success);
    RUN_TEST(test_erase_flash_spi_failure);
    RUN_TEST(test_erase_flash_erase_fail);

    RUN_TEST(test_erase_region_bad_size);
    RUN_TEST(test_erase_region_unaligned);
    RUN_TEST(test_erase_region_success);
    RUN_TEST(test_erase_region_erase_fail);

    RUN_TEST(test_write_flush_erase_fail);
    RUN_TEST(test_write_flush_program_fail);

    RUN_TEST(test_read_flash_bad_size);
    RUN_TEST(test_read_flash_success);

    return UnityEnd();
}
