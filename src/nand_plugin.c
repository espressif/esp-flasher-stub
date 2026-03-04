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
#include <esp-stub-lib/md5.h>
#include "nand.h"
#include "commands.h"
#include "command_handler.h"
#include "endian_utils.h"
#include "plugin_table.h"

/* ---- SLIP forward declarations (resolved to base stub via PROVIDE) --------- */
extern void         slip_send_frame(const void *data, size_t size);
extern void         slip_recv_reset(void);
extern bool         slip_is_frame_complete(void);
extern const uint8_t *slip_get_frame_data(size_t *length);

/* ---- Response wire format constants ---------------------------------------- */
#define DIRECTION_RESPONSE         0x01
#define RESPONSE_STATUS_SIZE       2
#define PLUGIN_MAX_RESP_DATA_SIZE  64
#define PLUGIN_MAX_RESP_SIZE       (HEADER_SIZE + PLUGIN_MAX_RESP_DATA_SIZE + RESPONSE_STATUS_SIZE)

/* ---- NAND geometry constants ----------------------------------------------- */
#define NAND_PAGE_SIZE       2048
#define NAND_PAGES_PER_BLOCK 64
#define NAND_BLOCK_SIZE      (NAND_PAGE_SIZE * NAND_PAGES_PER_BLOCK)
#define NAND_BLOCK_COUNT     1024

/* ---- Plugin-local state ---------------------------------------------------- */

/* NAND write operation state — placed in plugin BSS (zeroed by esptool) */
static struct {
    uint32_t offset;
    uint32_t total_remaining;
    uint8_t  page_buf[NAND_PAGE_SIZE] __attribute__((aligned(4)));
    uint32_t page_buf_filled;
    bool     in_progress;
} s_nand_write_state;

/*
 * Accumulated result from the previous write_flash_data post-process.
 * Mirrors the base stub's accumulated_result mechanism.
 */
static int s_nand_accumulated_result;  /* zero-initialised = RESPONSE_SUCCESS */

/* ---- Response helper ------------------------------------------------------- */

static void plugin_send_response(uint8_t command, int code,
                                 uint32_t value,
                                 const uint8_t *extra, uint16_t extra_len)
{
    uint8_t buf[PLUGIN_MAX_RESP_SIZE];
    memset(buf, 0, sizeof(buf));

    uint16_t data_size = extra_len;
    if (data_size > PLUGIN_MAX_RESP_DATA_SIZE) {
        data_size = PLUGIN_MAX_RESP_DATA_SIZE;
    }
    uint16_t resp_data_size  = (uint16_t)(data_size + RESPONSE_STATUS_SIZE);
    uint16_t total_frame_size = (uint16_t)(HEADER_SIZE + resp_data_size);

    uint8_t *p = buf;
    *p++ = DIRECTION_RESPONSE;
    *p++ = command;
    set_u16_to_le(p, resp_data_size);  p += 2;
    set_u32_to_le(p, value);           p += 4;
    if (extra && data_size > 0) {
        memcpy(p, extra, data_size);
        p += data_size;
    }
    set_u16_to_be(p, (uint16_t)code);

    slip_send_frame(buf, total_frame_size);
}

/* ---- Internal helpers ------------------------------------------------------ */

static int s_nand_write_flush_page(void)
{
    uint32_t page_number = s_nand_write_state.offset / NAND_PAGE_SIZE;
    if (page_number % NAND_PAGES_PER_BLOCK == 0) {
        if (nand_erase_block(page_number) != 0) {
            return RESPONSE_FAILED_SPI_OP;
        }
    }
    if (nand_write_page(page_number, s_nand_write_state.page_buf) != 0) {
        return RESPONSE_FAILED_SPI_OP;
    }
    s_nand_write_state.offset       += NAND_PAGE_SIZE;
    s_nand_write_state.page_buf_filled = 0;
    return RESPONSE_SUCCESS;
}

/* ---- Plugin handler implementations --------------------------------------- */

void nand_plugin_attach(uint8_t command, const uint8_t *data, uint16_t size)
{
    if (size != SPI_NAND_ATTACH_SIZE) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    uint32_t hspi_arg = get_le_to_u32(data);
    int result = nand_attach(hspi_arg);
    if (result != 0) {
        plugin_send_response(command, RESPONSE_FAILED_SPI_OP,
                             (uint32_t)result, NULL, 0);
        return;
    }

    const uint8_t *dbg_id    = nand_get_debug_id();
    const uint8_t *dbg_extra = nand_get_debug_extra();
    uint32_t val = ((uint32_t)dbg_extra[0] << 24) | ((uint32_t)dbg_id[0] << 16) |
                   ((uint32_t)dbg_id[1] << 8)    | (uint32_t)dbg_id[2];
    uint8_t extra_byte = dbg_extra[1];
    plugin_send_response(command, RESPONSE_SUCCESS, val, &extra_byte, 1);
}

void nand_plugin_read_spare(uint8_t command, const uint8_t *data, uint16_t size)
{
    if (size != SPI_NAND_READ_SPARE_SIZE) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    uint32_t page_number = get_le_to_u32(data);
    uint8_t spare[4] = {0};
    if (nand_read_spare(page_number, spare) != 0) {
        plugin_send_response(command, RESPONSE_FAILED_SPI_OP, 0, NULL, 0);
        return;
    }

    uint32_t val = (uint32_t)spare[0]        | ((uint32_t)spare[1] << 8) |
                   ((uint32_t)spare[2] << 16) | ((uint32_t)spare[3] << 24);
    plugin_send_response(command, RESPONSE_SUCCESS, val, NULL, 0);
}

void nand_plugin_write_spare(uint8_t command, const uint8_t *data, uint16_t size)
{
    if (size != SPI_NAND_WRITE_SPARE_SIZE) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    uint32_t page_number = (uint32_t)data[0]       | ((uint32_t)data[1] << 8) |
                           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    uint8_t is_bad = data[4];
    if (nand_write_spare(page_number, is_bad) != 0) {
        plugin_send_response(command, RESPONSE_FAILED_SPI_OP, 0, NULL, 0);
        return;
    }
    plugin_send_response(command, RESPONSE_SUCCESS, 0, NULL, 0);
}

void nand_plugin_read_flash(uint8_t command, const uint8_t *data, uint16_t size)
{
    if (size != SPI_NAND_READ_FLASH_SIZE) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    const uint8_t *ptr = data;
    uint32_t offset      = get_le_to_u32(ptr); ptr += 4;
    uint32_t read_size   = get_le_to_u32(ptr); ptr += 4;
    uint32_t packet_size = get_le_to_u32(ptr);
    uint32_t max_unacked_packets = 1;

    uint32_t page_size = nand_get_page_size();
    if (page_size == 0) {
        plugin_send_response(command, RESPONSE_FAILED_SPI_OP, 0, NULL, 0);
        return;
    }

    /* Reuse static buffers in plugin BSS — saves stack */
    static uint8_t page_buf[2048] __attribute__((aligned(4)));
    static uint8_t send_buf[4096] __attribute__((aligned(4)));

    if (packet_size > sizeof(send_buf)) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    /* Send SUCCESS first — data stream follows */
    plugin_send_response(command, RESPONSE_SUCCESS, 0, NULL, 0);

    /* Reset receive state to accept ACK frames from the host */
    slip_recv_reset();

    uint32_t read_size_remaining = read_size;
    uint32_t sent_packets   = 0;
    uint32_t acked_data_size = 0;
    uint32_t acked_packets  = 0;
    uint32_t current_offset = offset;

    struct stub_lib_md5_ctx md5_ctx;
    stub_lib_md5_init(&md5_ctx);

    while (read_size_remaining > 0 || acked_data_size < read_size) {
        /* Check for ACK from host */
        if (slip_is_frame_complete()) {
            size_t slip_size;
            const uint8_t *slip_data = slip_get_frame_data(&slip_size);
            if (slip_size == sizeof(acked_data_size)) {
                memcpy(&acked_data_size, slip_data, sizeof(acked_data_size));
            }
            acked_packets++;
            slip_recv_reset();
        }

        if (read_size_remaining > 0 &&
                sent_packets - acked_packets < max_unacked_packets) {
            uint32_t actual_read_size = read_size_remaining;
            if (actual_read_size > packet_size) {
                actual_read_size = packet_size;
            }

            uint32_t bytes_filled = 0;
            while (bytes_filled < actual_read_size) {
                uint32_t page_number  = current_offset / page_size;
                uint32_t page_offset  = current_offset % page_size;
                uint32_t avail        = page_size - page_offset;
                uint32_t needed       = actual_read_size - bytes_filled;
                uint32_t chunk        = (avail < needed) ? avail : needed;

                if (nand_read_page(page_number, page_buf, page_size) != 0) {
                    /* Can't easily error-out mid-stream; best effort */
                    goto done;
                }
                memcpy(send_buf + bytes_filled, page_buf + page_offset, chunk);
                bytes_filled   += chunk;
                current_offset += chunk;
            }

            stub_lib_md5_update(&md5_ctx, send_buf, actual_read_size);
            slip_send_frame(send_buf, actual_read_size);
            read_size_remaining -= actual_read_size;
            sent_packets++;
        }
    }
done:;
    uint8_t md5[16];
    stub_lib_md5_final(&md5_ctx, md5);
    slip_send_frame(md5, sizeof(md5));
}

void nand_plugin_write_flash_begin(uint8_t command, const uint8_t *data, uint16_t size)
{
    if (size != SPI_NAND_WRITE_FLASH_BEGIN_SIZE) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    const uint8_t *ptr = data;
    uint32_t offset     = get_le_to_u32(ptr); ptr += 4;
    uint32_t total_size = get_le_to_u32(ptr); /* ptr += 4; block_size and packet_size unused */

    s_nand_write_state.offset         = offset;
    s_nand_write_state.total_remaining = total_size;
    s_nand_write_state.page_buf_filled  = 0;
    s_nand_write_state.in_progress      = true;
    s_nand_accumulated_result           = RESPONSE_SUCCESS;

    plugin_send_response(command, RESPONSE_SUCCESS, 0, NULL, 0);
}

void nand_plugin_write_flash_data(uint8_t command, const uint8_t *data, uint16_t size)
{
    /* Carry over error from previous chunk's write */
    if (s_nand_accumulated_result != RESPONSE_SUCCESS) {
        int err = s_nand_accumulated_result;
        s_nand_accumulated_result = RESPONSE_SUCCESS;
        plugin_send_response(command, err, 0, NULL, 0);
        return;
    }

    if (size < SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE) {
        plugin_send_response(command, RESPONSE_NOT_ENOUGH_DATA, 0, NULL, 0);
        return;
    }

    if (!s_nand_write_state.in_progress) {
        plugin_send_response(command, RESPONSE_NOT_IN_FLASH_MODE, 0, NULL, 0);
        return;
    }

    uint32_t data_len            = get_le_to_u32(data);
    const uint8_t *flash_data    = data + SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE;
    uint16_t actual_data_size    = (uint16_t)(size - SPI_NAND_WRITE_FLASH_DATA_HEADER_SIZE);

    if (data_len != actual_data_size) {
        plugin_send_response(command, RESPONSE_TOO_MUCH_DATA, 0, NULL, 0);
        return;
    }

    if (actual_data_size > s_nand_write_state.total_remaining) {
        plugin_send_response(command, RESPONSE_TOO_MUCH_DATA, 0, NULL, 0);
        return;
    }

    /* Validate checksum (same XOR algorithm as base stub).
     * data[-4..-1] is the checksum field in the packet header (HEADER_SIZE=8,
     * checksum at header offset +4). */
    uint32_t checksum = 0xEF;
    for (uint32_t i = 0; i < actual_data_size; i++) {
        checksum ^= flash_data[i];
    }
    uint32_t expected_checksum = get_le_to_u32(data - 4);
    if (checksum != expected_checksum) {
        plugin_send_response(command, RESPONSE_BAD_DATA_CHECKSUM, 0, NULL, 0);
        return;
    }

    /* Send SUCCESS before the write — mirrors base stub post-process pattern */
    plugin_send_response(command, RESPONSE_SUCCESS, 0, NULL, 0);

    /* Execute write inline (was post-process in base stub) */
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
        s_nand_write_state.page_buf_filled  += chunk;
        s_nand_write_state.total_remaining  -= chunk;
        src         += chunk;
        to_process  -= chunk;

        if (s_nand_write_state.page_buf_filled >= NAND_PAGE_SIZE) {
            int ret = s_nand_write_flush_page();
            if (ret != RESPONSE_SUCCESS) {
                s_nand_accumulated_result = ret;
                return;
            }
        }
    }

    /* Pad and flush last partial page */
    if (s_nand_write_state.total_remaining == 0 &&
            s_nand_write_state.page_buf_filled > 0) {
        memset(s_nand_write_state.page_buf + s_nand_write_state.page_buf_filled,
               0xFF, NAND_PAGE_SIZE - s_nand_write_state.page_buf_filled);
        s_nand_write_state.page_buf_filled = NAND_PAGE_SIZE;
        int ret = s_nand_write_flush_page();
        if (ret != RESPONSE_SUCCESS) {
            s_nand_accumulated_result = ret;
            return;
        }
    }

    if (s_nand_write_state.total_remaining == 0) {
        s_nand_write_state.in_progress = false;
    }
}

void nand_plugin_erase_flash(uint8_t command, const uint8_t *data, uint16_t size)
{
    (void)data; (void)size;
    for (uint32_t block = 0; block < NAND_BLOCK_COUNT; block++) {
        if (nand_erase_block(block * NAND_PAGES_PER_BLOCK) != 0) {
            plugin_send_response(command, RESPONSE_FAILED_SPI_OP, 0, NULL, 0);
            return;
        }
    }
    plugin_send_response(command, RESPONSE_SUCCESS, 0, NULL, 0);
}

void nand_plugin_erase_region(uint8_t command, const uint8_t *data, uint16_t size)
{
    if (size != ERASE_REGION_SIZE) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    uint32_t offset     = get_le_to_u32(data);
    uint32_t erase_size = get_le_to_u32(data + 4);

    if (offset % NAND_BLOCK_SIZE != 0 || erase_size % NAND_BLOCK_SIZE != 0) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    uint32_t start_page = offset / NAND_PAGE_SIZE;
    uint32_t num_blocks = erase_size / NAND_BLOCK_SIZE;
    for (uint32_t i = 0; i < num_blocks; i++) {
        if (nand_erase_block(start_page + i * NAND_PAGES_PER_BLOCK) != 0) {
            plugin_send_response(command, RESPONSE_FAILED_SPI_OP, 0, NULL, 0);
            return;
        }
    }
    plugin_send_response(command, RESPONSE_SUCCESS, 0, NULL, 0);
}

void nand_plugin_read_page_debug(uint8_t command, const uint8_t *data, uint16_t size)
{
    if (size != SPI_NAND_READ_PAGE_DEBUG_SIZE) {
        plugin_send_response(command, RESPONSE_BAD_DATA_LEN, 0, NULL, 0);
        return;
    }

    uint32_t page_number = get_le_to_u32(data);
    uint32_t page_size   = nand_get_page_size();
    if (page_size == 0) {
        plugin_send_response(command, RESPONSE_FAILED_SPI_OP, 0, NULL, 0);
        return;
    }

    static uint8_t page_buf[2048] __attribute__((aligned(4)));
    if (nand_read_page(page_number, page_buf, page_size) != 0) {
        plugin_send_response(command, RESPONSE_FAILED_SPI_OP, 0, NULL, 0);
        return;
    }

    plugin_send_response(command, RESPONSE_SUCCESS, page_number, page_buf, 16);
}
