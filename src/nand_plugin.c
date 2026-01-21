/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * NAND flash plugin — compiled as a separate binary loaded at runtime by esptool.
 *
 * SLIP functions (slip_send_frame, slip_recv_reset, slip_is_frame_complete,
 * slip_get_frame_data) are forwarded to the base stub via PROVIDE in the linker
 * script. They access the same SLIP state that the UART ISR writes to.
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <esp-stub-lib/bit_utils.h>
#include <esp-stub-lib/md5.h>
#include <target/nand.h>
#include "commands.h"
#include "command_handler.h"
#include "endian_utils.h"
#include "plugin_table.h"

/* ---- SLIP forward declarations (resolved to base stub via PROVIDE) --------- */
extern void slip_send_frame(const void *data, size_t size);
extern void slip_recv_reset(void);
extern bool slip_is_frame_complete(void);
extern const uint8_t *slip_get_frame_data(size_t *length);

/* ---- NAND geometry constants ----------------------------------------------- */
#define NAND_PAGE_SIZE 2048
#define NAND_PAGES_PER_BLOCK 64
#define NAND_BLOCK_SIZE (NAND_PAGE_SIZE * NAND_PAGES_PER_BLOCK)
#define NAND_BLOCK_COUNT 1024

/* MD5 digest size in bytes */
#define MD5_DIGEST_SIZE 16

/* Byte offset of the is_bad flag within the write_bbm request payload:
 * [0..3] page_number (LE uint32) | [4] is_bad (uint8). */
#define WRITE_BBM_IS_BAD_OFFSET sizeof(uint32_t)

/* Number of bytes from the first byte of a data packet header that the
 * checksum field occupies.  The full SLIP frame is:
 *   [0]    direction byte
 *   [1]    command opcode
 *   [2-3]  payload size (LE uint16)
 *   [4-7]  checksum (LE uint32)   ← HEADER_SIZE=8, checksum at offset +4
 * The handler receives a pointer to byte [8] (start of payload), so
 * data - sizeof(uint32_t) reaches the checksum field. */

/* XOR checksum initial value — same seed used by the base stub and the
 * ROM loader (CHKSUM_INIT = 0xEF). */
#define NAND_CHECKSUM_INIT 0xEFU

/* Number of bytes returned as the diagnostic data preview in read_page_debug.
 * Chosen to fit within MAX_RESPONSE_DATA_SIZE (64) minus status/value overhead.
 * This is a diagnostic sample only — the full page is read but only this many
 * bytes are forwarded to the host. */
#define READ_PAGE_DEBUG_PREVIEW_SIZE 16

/* Host flow control: one in-flight packet at a time.
 * The read_flash loop will not send the next packet until the host ACKs the
 * current one.  Raising this value would increase throughput at the cost of
 * requiring a larger host-side reassembly buffer. */
static const uint32_t MAX_UNACKED_PACKETS = 1;

/* ---- Plugin-local state ---------------------------------------------------- */

/* NAND write operation state — placed in plugin BSS (zeroed by esptool).
 * in_progress stays set on flush failure so the host can detect incomplete
 * writes; page_buf_filled is cleared on failure to avoid replaying stale data. */
static struct {
    uint32_t offset;
    uint32_t total_remaining;
    uint8_t page_buf[NAND_PAGE_SIZE] __attribute__((aligned(4)));
    uint32_t page_buf_filled;
    bool in_progress;
} s_nand_write_state;

/*
 * NAND read operation state — stashed in plugin BSS so the post-process
 * callback can pick up parameters parsed by nand_plugin_read_flash.
 * Using BSS avoids data-layout assumptions about the command payload pointer
 * (whose lifetime ends when slip_recv_reset() is called at the top of the
 * streaming loop).  This mirrors the base stub's s_flash_state pattern.
 */
static struct {
    uint32_t offset;
    uint32_t read_size;
    uint32_t packet_size;
} s_nand_read_state;

/* ---- Internal helpers ------------------------------------------------------ */

/*
 * nand_err_to_response — map a stub_target_nand_* return code to a response code.
 *
 * NAND_ERR_PROGRAM_FAILED / NAND_ERR_ERASE_FAILED are chip-level signals: the
 * W25N01GV app-note recommends marking the block bad and moving on. We deliberately
 * do NOT retry inside the stub — retrying would mask genuine media failures and
 * cause the host to silently write bad data to a dying block.
 *
 * Any other non-zero code (SPI bus error, timeout, …) is a transport failure and
 * maps to RESPONSE_FAILED_SPI_OP so the host can distinguish it from a chip fault.
 */
static int nand_err_to_response(int err)
{
    if (err == 0) {
        return RESPONSE_SUCCESS;
    }
    if (err == NAND_ERR_PROGRAM_FAILED) {
        return RESPONSE_NAND_PROGRAM_FAILED;
    }
    if (err == NAND_ERR_ERASE_FAILED) {
        return RESPONSE_NAND_ERASE_FAILED;
    }
    return RESPONSE_FAILED_SPI_OP;
}

static int s_nand_write_flush_page(void)
{
    uint32_t page_number = s_nand_write_state.offset / NAND_PAGE_SIZE;
    if (page_number % NAND_PAGES_PER_BLOCK == 0) {
        int ret = stub_target_nand_erase_block(page_number);
        if (ret != 0) {
            return nand_err_to_response(ret);
        }
    }
    int ret = stub_target_nand_write_page(page_number, s_nand_write_state.page_buf, NAND_PAGE_SIZE);
    if (ret != 0) {
        return nand_err_to_response(ret);
    }
    s_nand_write_state.offset += NAND_PAGE_SIZE;
    s_nand_write_state.page_buf_filled = 0;
    return RESPONSE_SUCCESS;
}

static int nand_fill_send_buf(uint8_t *send_buf, uint8_t *page_buf,
                              uint32_t page_size,
                              uint32_t *current_offset,
                              uint32_t actual_read_size)
{
    uint32_t bytes_filled = 0;
    while (bytes_filled < actual_read_size) {
        uint32_t page_number = *current_offset / page_size;
        uint32_t page_offset = *current_offset % page_size;
        uint32_t avail = page_size - page_offset;
        uint32_t needed = actual_read_size - bytes_filled;
        uint32_t chunk = MIN(avail, needed);

        if (stub_target_nand_read_page(page_number, page_buf, page_size) != 0) {
            return -1;
        }
        memcpy(send_buf + bytes_filled, page_buf + page_offset, chunk);
        bytes_filled += chunk;
        *current_offset += chunk;
    }
    return 0;
}

/* ---- read_flash post-process ---------------------------------------------- */

/*
 * s_nand_read_flash_post_process — streaming read loop, runs after the
 * initial RESPONSE_SUCCESS frame has been sent by the dispatcher.
 *
 * Parameters are taken from s_nand_read_state (stashed by nand_plugin_read_flash
 * before returning).  If a NAND read error occurs mid-stream the handler
 * continues sending zero-filled packets so the host's expected byte count is
 * satisfied, then deliberately corrupts the final MD5 frame (XOR every byte
 * with 0xFF).  This surfaces as a verification failure on the host side rather
 * than a silent hang or truncated read.
 */
static int s_nand_read_flash_post_process(const struct cmd_ctx *ctx)
{
    (void)ctx;

    uint32_t offset = s_nand_read_state.offset;
    uint32_t read_size = s_nand_read_state.read_size;
    uint32_t packet_size = s_nand_read_state.packet_size;

    /* Reuse static buffers in plugin BSS — saves stack */
    static uint8_t page_buf[NAND_PAGE_SIZE] __attribute__((aligned(4)));
    /* send_buf is sized for worst-case packet_size the host may request
     * (NAND_PAGE_SIZE * 2).  The guard in nand_plugin_read_flash enforces
     * this bound before we are called. */
    static uint8_t send_buf[NAND_PAGE_SIZE * 2] __attribute__((aligned(4)));

    /* Reset receive state to accept ACK frames from the host */
    slip_recv_reset();

    uint32_t read_size_remaining = read_size;
    uint32_t sent_packets = 0;
    uint32_t acked_data_size = 0;
    uint32_t acked_packets = 0;
    uint32_t current_offset = offset;
    bool read_failed = false;

    struct stub_lib_md5_ctx md5_ctx;
    stub_lib_md5_init(&md5_ctx);

    while (read_size_remaining > 0 || acked_data_size < read_size) {
        /* Check for ACK from host */
        if (slip_is_frame_complete()) {
            size_t slip_size;
            const uint8_t *slip_data = slip_get_frame_data(&slip_size);
            if (slip_size == sizeof(acked_data_size)) {
                memcpy(&acked_data_size, slip_data, sizeof(acked_data_size));
                /* Only count ACKs for validated frames, and never exceed sent_packets */
                if (acked_packets < sent_packets) {
                    acked_packets++;
                }
            }
            slip_recv_reset();
        }

        /* Avoid unsigned underflow when computing unacked packet count */
        uint32_t unacked_packets = (sent_packets >= acked_packets) ?
                                   (sent_packets - acked_packets) : 0;

        if (read_size_remaining > 0 &&
                unacked_packets < MAX_UNACKED_PACKETS) {
            uint32_t actual_read_size = read_size_remaining;
            if (actual_read_size > packet_size) {
                actual_read_size = packet_size;
            }

            if (!read_failed) {
                if (nand_fill_send_buf(send_buf, page_buf, NAND_PAGE_SIZE,
                                       &current_offset, actual_read_size) != 0) {
                    read_failed = true;
                }
            }
            if (read_failed) {
                /* After a NAND read error we still send exactly read_size bytes so
                 * the host does not block waiting for data; the MD5 frame below is
                 * poisoned, which will surface as a verification failure. */
                memset(send_buf, 0, actual_read_size);
            }

            stub_lib_md5_update(&md5_ctx, send_buf, actual_read_size);
            slip_send_frame(send_buf, actual_read_size);
            read_size_remaining -= actual_read_size;
            sent_packets++;
        }
    }

    uint8_t md5[MD5_DIGEST_SIZE];
    stub_lib_md5_final(&md5_ctx, md5);
    if (read_failed) {
        for (size_t i = 0; i < sizeof(md5); i++) {
            md5[i] ^= 0xFF;  /* intentionally corrupt — see comment above */
        }
    }
    slip_send_frame(md5, sizeof(md5));
    return RESPONSE_SUCCESS;
}

/* ---- Plugin handler implementations --------------------------------------- */

int nand_plugin_attach(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
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

    uint8_t mfr_id = 0, status_reg = 0, prot_reg = 0;
    uint16_t dev_id = 0;
    stub_target_nand_read_id(&mfr_id, &dev_id);
    stub_target_nand_read_register(NAND_REG_STATUS, &status_reg);
    stub_target_nand_read_register(NAND_REG_PROTECT, &prot_reg);

    /* Pack: status_reg[31:24] | mfr_id[23:16] | dev_id[15:0] */
    resp->value = ((uint32_t)status_reg << 24)
                  | ((uint32_t)mfr_id << 16)
                  | (uint32_t)dev_id;
    resp->data[0] = prot_reg;
    resp->data_size = 1;
    return RESPONSE_SUCCESS;
}

int nand_plugin_read_bbm(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
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

    resp->value = get_le_to_u32(spare);
    return RESPONSE_SUCCESS;
}

int nand_plugin_write_bbm(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len != SPI_NAND_WRITE_BBM_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t page_number = get_le_to_u32(data);
    uint8_t is_bad = data[WRITE_BBM_IS_BAD_OFFSET];
    if (stub_target_nand_write_bbm(page_number, is_bad) != 0) {
        return RESPONSE_FAILED_SPI_OP;
    }
    return RESPONSE_SUCCESS;
}

/*
 * nand_plugin_read_flash — validates parameters, stashes them in s_nand_read_state,
 * registers s_nand_read_flash_post_process, and returns RESPONSE_SUCCESS.
 *
 * The dispatcher sends the initial success frame, then invokes the post-process
 * callback which runs the streaming loop (page fills, ACK handling, MD5,
 * poison-on-failure).
 */
int nand_plugin_read_flash(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != SPI_NAND_READ_FLASH_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const uint8_t *ptr = data;
    uint32_t offset = get_le_to_u32(ptr);
    ptr += sizeof(uint32_t);
    uint32_t read_size = get_le_to_u32(ptr);
    ptr += sizeof(uint32_t);
    uint32_t packet_size = get_le_to_u32(ptr);

    uint32_t page_size = stub_target_nand_get_page_size();
    if (page_size == 0 || page_size != NAND_PAGE_SIZE) {
        return RESPONSE_FAILED_SPI_OP;
    }

    /* send_buf in the post-process is sized for NAND_PAGE_SIZE * 2, and
     * packet_size must be non-zero so the post-process loop always makes
     * forward progress. */
    if (packet_size == 0 || packet_size > NAND_PAGE_SIZE * 2) {
        return RESPONSE_BAD_DATA_LEN;
    }

    /* Stash parameters for the post-process callback */
    s_nand_read_state.offset = offset;
    s_nand_read_state.read_size = read_size;
    s_nand_read_state.packet_size = packet_size;

    resp->post_process = s_nand_read_flash_post_process;
    return RESPONSE_SUCCESS;
}

int nand_plugin_write_flash_begin(uint8_t command, const uint8_t *data, uint32_t len,
                                  struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len != SPI_NAND_WRITE_FLASH_BEGIN_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const uint8_t *ptr = data;
    uint32_t offset = get_le_to_u32(ptr);
    ptr += sizeof(uint32_t);
    uint32_t total_size = get_le_to_u32(ptr); /* ptr += sizeof(uint32_t); block_size and packet_size unused */

    /* Write path assumes page-aligned offset: page_number = offset / NAND_PAGE_SIZE */
    if ((offset % NAND_PAGE_SIZE) != 0) {
        return RESPONSE_BAD_DATA_LEN;
    }

    s_nand_write_state.offset = offset;
    s_nand_write_state.total_remaining = total_size;
    s_nand_write_state.page_buf_filled = 0;
    s_nand_write_state.in_progress = true;

    return RESPONSE_SUCCESS;
}

/*
 * Mid-stream full-page flush errors are reported inline here.
 * Final partial-page flush and state-clear are handled by nand_plugin_write_flash_end.
 */
int nand_plugin_write_flash_data(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len < SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE) {
        return RESPONSE_NOT_ENOUGH_DATA;
    }

    if (!s_nand_write_state.in_progress) {
        return RESPONSE_NOT_IN_FLASH_MODE;
    }

    uint32_t data_len = get_le_to_u32(data);
    const uint8_t *flash_data = data + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE;
    uint32_t actual_data_size = len - SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE;

    if (data_len != actual_data_size) {
        return RESPONSE_TOO_MUCH_DATA;
    }

    if (actual_data_size > s_nand_write_state.total_remaining) {
        return RESPONSE_TOO_MUCH_DATA;
    }

    /* Validate checksum (same XOR algorithm as base stub).
     * data - sizeof(uint32_t) is the checksum field in the packet header
     * (HEADER_SIZE=8, checksum at header offset +4; see NAND_CHECKSUM_INIT comment). */
    uint32_t checksum = NAND_CHECKSUM_INIT;
    for (uint32_t i = 0; i < actual_data_size; i++) {
        checksum ^= flash_data[i];
    }
    uint32_t expected_checksum = get_le_to_u32(data - sizeof(uint32_t));
    if (checksum != expected_checksum) {
        return RESPONSE_BAD_DATA_CHECKSUM;
    }

    /* Execute write before sending the response */
    uint32_t to_process = actual_data_size;
    if (to_process > s_nand_write_state.total_remaining) {
        to_process = s_nand_write_state.total_remaining;
    }
    const uint8_t *src = flash_data;

    while (to_process > 0) {
        uint32_t space = NAND_PAGE_SIZE - s_nand_write_state.page_buf_filled;
        uint32_t chunk = (to_process < space) ? to_process : space;
        memcpy(s_nand_write_state.page_buf + s_nand_write_state.page_buf_filled,
               src, chunk);
        s_nand_write_state.page_buf_filled += chunk;
        s_nand_write_state.total_remaining -= chunk;
        src += chunk;
        to_process -= chunk;

        if (s_nand_write_state.page_buf_filled >= NAND_PAGE_SIZE) {
            int ret = s_nand_write_flush_page();
            if (ret != RESPONSE_SUCCESS) {
                s_nand_write_state.page_buf_filled = 0;
                return ret;
            }
        }
    }

    return RESPONSE_SUCCESS;
}

int nand_plugin_write_flash_end(uint8_t command, const uint8_t *data, uint32_t len,
                                struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len != SPI_NAND_WRITE_FLASH_END_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    if (!s_nand_write_state.in_progress) {
        return RESPONSE_NOT_IN_FLASH_MODE;
    }

    if (s_nand_write_state.total_remaining != 0) {
        return RESPONSE_BAD_DATA_LEN;
    }

    if (s_nand_write_state.page_buf_filled > 0) {
        memset(s_nand_write_state.page_buf + s_nand_write_state.page_buf_filled,
               0xFF, NAND_PAGE_SIZE - s_nand_write_state.page_buf_filled);
        s_nand_write_state.page_buf_filled = NAND_PAGE_SIZE;
        int ret = s_nand_write_flush_page();
        if (ret != RESPONSE_SUCCESS) {
            s_nand_write_state.page_buf_filled = 0;
            return ret;
        }
    }

    /* reboot_flag field exists for parity with ESP_FLASH_END; no chip-side action for NAND */
    (void)get_le_to_u32(data);

    memset(&s_nand_write_state, 0, sizeof(s_nand_write_state));
    return RESPONSE_SUCCESS;
}

int nand_plugin_erase_flash(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    (void)data;
    (void)len;
    (void)resp;
    for (uint32_t block = 0; block < NAND_BLOCK_COUNT; block++) {
        int ret = stub_target_nand_erase_block(block * NAND_PAGES_PER_BLOCK);
        if (ret != 0) {
            return nand_err_to_response(ret);
        }
    }
    return RESPONSE_SUCCESS;
}

int nand_plugin_erase_region(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len != ERASE_REGION_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t offset = get_le_to_u32(data);
    uint32_t erase_size = get_le_to_u32(data + sizeof(uint32_t));

    if (offset % NAND_BLOCK_SIZE != 0 || erase_size % NAND_BLOCK_SIZE != 0) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t start_page = offset / NAND_PAGE_SIZE;
    uint32_t num_blocks = erase_size / NAND_BLOCK_SIZE;
    for (uint32_t i = 0; i < num_blocks; i++) {
        int ret = stub_target_nand_erase_block(start_page + i * NAND_PAGES_PER_BLOCK);
        if (ret != 0) {
            return nand_err_to_response(ret);
        }
    }
    return RESPONSE_SUCCESS;
}

int nand_plugin_read_page_debug(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != SPI_NAND_READ_PAGE_DEBUG_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t page_number = get_le_to_u32(data);
    uint32_t page_size = stub_target_nand_get_page_size();
    if (page_size == 0 || page_size != NAND_PAGE_SIZE) {
        return RESPONSE_FAILED_SPI_OP;
    }

    static uint8_t page_buf[NAND_PAGE_SIZE] __attribute__((aligned(4)));
    if (stub_target_nand_read_page(page_number, page_buf, page_size) != 0) {
        return RESPONSE_FAILED_SPI_OP;
    }

    resp->value = page_number;
    memcpy(resp->data, page_buf, READ_PAGE_DEBUG_PREVIEW_SIZE);
    resp->data_size = READ_PAGE_DEBUG_PREVIEW_SIZE;
    return RESPONSE_SUCCESS;
}
