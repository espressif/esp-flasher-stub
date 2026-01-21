/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <esp-stub-lib/soc_utils.h>
#include <esp-stub-lib/bit_utils.h>
#include <esp-stub-lib/err.h>
#include <esp-stub-lib/flash.h>
#include <esp-stub-lib/uart.h>
#include <esp-stub-lib/rom_wrappers.h>
#include <esp-stub-lib/security.h>
#include <esp-stub-lib/miniz.h>
#include <esp-stub-lib/md5.h>
#include "slip.h"
#include "commands.h"
#include "command_handler.h"
#include "endian_utils.h"
#include "nand.h"

#define DIRECTION_REQUEST 0x00
#define DIRECTION_RESPONSE 0x01

#define RESPONSE_STATUS_SIZE 2
#define MAX_RESPONSE_DATA_SIZE 64
#define MAX_RESPONSE_SIZE (HEADER_SIZE + MAX_RESPONSE_DATA_SIZE + RESPONSE_STATUS_SIZE)

/* Memory operation state */
struct memory_operation_state {
    uint32_t total_remaining;
    uint32_t num_blocks;
    uint32_t block_size;
    uint32_t offset;
    bool in_progress;
};

/* Flash operation state */
struct flash_operation_state {
    uint32_t total_remaining;
    uint32_t num_blocks;
    uint32_t block_size;
    uint32_t offset;
    uint32_t compressed_remaining;
    tinfl_decompressor decompressor;
    bool encrypt;
    bool in_progress;
    /* Async erase state */
    uint32_t next_erase_addr;
    uint32_t erase_remaining;
};

/* Command context passed to handlers and post-process functions */
struct cmd_ctx {
    uint8_t command;
    uint8_t direction;
    uint16_t packet_size;
    uint32_t checksum;
    const uint8_t *data;
};

/* Response data for commands that return data */
struct command_response_data {
    uint32_t value;
    uint8_t data[MAX_RESPONSE_DATA_SIZE];
    uint16_t data_size;
};

static struct flash_operation_state s_flash_state = {0};
static struct memory_operation_state s_memory_state = {0};
static int (*s_pending_post_process)(const struct cmd_ctx *ctx) = NULL;

static inline int s_validate_checksum(const uint8_t *data, uint32_t size, uint32_t expected)
{
    uint32_t checksum = 0xEF;
    for (uint32_t i = 0; i < size; i++) {
        checksum ^= data[i];
    }

    if (checksum != expected) {
        return RESPONSE_BAD_DATA_CHECKSUM;
    }
    return RESPONSE_SUCCESS;
}

static void s_send_response(uint8_t command, int response_code, const struct command_response_data *response_data)
{
    uint8_t response_buffer[MAX_RESPONSE_SIZE] = {0};

    uint16_t data_size = response_data ? response_data->data_size : 0U;
    if (data_size > MAX_RESPONSE_DATA_SIZE) {
        data_size = MAX_RESPONSE_DATA_SIZE;
    }

    const uint16_t resp_data_size = (uint16_t)(data_size + RESPONSE_STATUS_SIZE);
    const uint16_t total_frame_size = (uint16_t)(HEADER_SIZE + resp_data_size);

    uint8_t direction_byte = DIRECTION_RESPONSE;

    uint8_t *ptr = response_buffer;
    *ptr++ = direction_byte;
    *ptr++ = command;
    set_u16_to_le(ptr, resp_data_size);
    ptr += sizeof(resp_data_size);
    uint32_t value = response_data ? response_data->value : 0;
    set_u32_to_le(ptr, value);
    ptr += sizeof(value);

    if (response_data && response_data->data_size > 0) {
        memcpy(ptr, response_data->data, data_size);
        ptr += data_size;
    }
    // Write response code in big-endian format (remote reads as ">H")
    set_u16_to_be(ptr, (uint16_t)response_code);

    slip_send_frame(response_buffer, total_frame_size);
}

static inline int s_check_flash_in_progress(void)
{
    if (!s_flash_state.in_progress) {
        return RESPONSE_NOT_IN_FLASH_MODE;
    }
    return RESPONSE_SUCCESS;
}

static inline int s_check_memory_in_progress(void)
{
    if (!s_memory_state.in_progress) {
        return RESPONSE_NOT_IN_FLASH_MODE;
    }
    return RESPONSE_SUCCESS;
}

static inline int s_ensure_flash_erased_to(uint32_t target_addr)
{
    while (s_flash_state.next_erase_addr < target_addr) {
        int result = stub_lib_flash_start_next_erase(&s_flash_state.next_erase_addr,
                                                     &s_flash_state.erase_remaining);
        if (result != STUB_LIB_OK && result != STUB_LIB_ERR_FLASH_BUSY) {
            return RESPONSE_FAILED_SPI_OP;
        }
    }
    return RESPONSE_SUCCESS;
}

static int s_init_flash_operation(const uint8_t *buffer, uint16_t size, bool is_compressed)
{
    if (size != FLASH_BEGIN_SIZE && size != FLASH_BEGIN_ENC_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const uint8_t *ptr = buffer;
    s_flash_state.total_remaining = get_le_to_u32(ptr);
    ptr += sizeof(s_flash_state.total_remaining);
    s_flash_state.num_blocks = get_le_to_u32(ptr);
    ptr += sizeof(s_flash_state.num_blocks);
    s_flash_state.block_size = get_le_to_u32(ptr);
    ptr += sizeof(s_flash_state.block_size);
    s_flash_state.offset = get_le_to_u32(ptr);
    ptr += sizeof(s_flash_state.offset);
    s_flash_state.encrypt = (size == FLASH_BEGIN_ENC_SIZE) ? get_le_to_u32(ptr) : false;
    s_flash_state.in_progress = true;

    if (is_compressed) {
        s_flash_state.compressed_remaining = s_flash_state.num_blocks * s_flash_state.block_size;
        tinfl_init(&s_flash_state.decompressor);
    }

    // Calculate total erase size (round to nearest sector boundary)
    uint32_t erase_start = ALIGN_DOWN(s_flash_state.offset, STUB_FLASH_SECTOR_SIZE);
    uint32_t erase_end = ALIGN_UP(s_flash_state.offset + s_flash_state.total_remaining, STUB_FLASH_SECTOR_SIZE);
    s_flash_state.erase_remaining = erase_end - erase_start;
    s_flash_state.next_erase_addr = erase_start;

    // Start the next erase operation, but do not wait for it to complete
    int result = stub_lib_flash_start_next_erase(&s_flash_state.next_erase_addr,
                                                 &s_flash_state.erase_remaining);
    if (result != STUB_LIB_OK && result != STUB_LIB_ERR_FLASH_BUSY) {
        return RESPONSE_FAILED_SPI_OP;
    }

    return RESPONSE_SUCCESS;
}

static int s_handle_flash_end(const uint8_t *buffer, uint16_t size, uint32_t *reboot_flag)
{
    if (size != FLASH_END_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    int check = s_check_flash_in_progress();
    if (check != RESPONSE_SUCCESS) {
        return check;
    }

    if (s_flash_state.total_remaining != 0) {
        return RESPONSE_BAD_DATA_LEN;
    }

    *reboot_flag = get_le_to_u32(buffer);
    memset(&s_flash_state, 0, sizeof(s_flash_state));

    return RESPONSE_SUCCESS;
}

static int s_write_flash_data(const uint8_t *data, uint32_t size)
{
    int result = s_ensure_flash_erased_to(s_flash_state.offset + size);
    if (result != RESPONSE_SUCCESS) {
        return result;
    }

    int lib_result = stub_lib_flash_write_buff(s_flash_state.offset, data, size, s_flash_state.encrypt);
    if (lib_result != STUB_LIB_OK) {
        return RESPONSE_FAILED_SPI_OP;
    }

    s_flash_state.total_remaining -= size;
    s_flash_state.offset += size;

    return RESPONSE_SUCCESS;
}

static int s_flash_data_post_process(const struct cmd_ctx *ctx)
{
    const uint8_t *flash_data = ctx->data + FLASH_DATA_HEADER_SIZE;
    const uint16_t actual_data_size = (uint16_t)(ctx->packet_size - FLASH_DATA_HEADER_SIZE);
    uint32_t write_size = MIN(actual_data_size, s_flash_state.total_remaining);

    return s_write_flash_data(flash_data, write_size);
}

static int s_flash_defl_data_post_process(const struct cmd_ctx *ctx)
{
    const uint8_t *ptr = ctx->data;
    uint32_t data_size = get_le_to_u32(ptr);
    ptr += sizeof(data_size);
    uint32_t seq = get_le_to_u32(ptr);
    ptr += sizeof(seq);

    const uint8_t *compressed_data = ctx->data + FLASH_DEFL_DATA_HEADER_SIZE;

    static uint8_t decompressed_data[TINFL_LZ_DICT_SIZE];
    static uint8_t *decompressed_data_ptr = decompressed_data;

    /* Ensure decompression buffer state is reset at the start of a new stream */
    if (seq == 0) {
        decompressed_data_ptr = decompressed_data;
    }

    size_t compressed_remaining = data_size;
    mz_uint32 flags = (seq == 0) ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0;
    tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;

    while (status > TINFL_STATUS_DONE && compressed_remaining > 0) {
        size_t in_bytes = compressed_remaining;
        size_t out_bytes = sizeof(decompressed_data) - (size_t)(decompressed_data_ptr - decompressed_data);

        if (s_flash_state.compressed_remaining > compressed_remaining) {
            flags |= TINFL_FLAG_HAS_MORE_INPUT;
        }

        /* Start opportunistic erase during decompression */
        int result = stub_lib_flash_start_next_erase(&s_flash_state.next_erase_addr,
                                                     &s_flash_state.erase_remaining);
        if (result != STUB_LIB_OK && result != STUB_LIB_ERR_FLASH_BUSY) {
            return RESPONSE_FAILED_SPI_OP;
        }

        status = tinfl_decompress(&s_flash_state.decompressor,
                                  compressed_data + data_size - compressed_remaining,
                                  &in_bytes,
                                  decompressed_data,
                                  decompressed_data_ptr,
                                  &out_bytes,
                                  flags);

        compressed_remaining -= in_bytes;
        decompressed_data_ptr += out_bytes;
        s_flash_state.compressed_remaining -= in_bytes;
        flags = 0;

        if (status == TINFL_STATUS_DONE ||
                decompressed_data_ptr >= decompressed_data + sizeof(decompressed_data)) {

            uint32_t write_size = (uint32_t)(decompressed_data_ptr - decompressed_data);

            int write_result = s_write_flash_data(decompressed_data, write_size);
            if (write_result != RESPONSE_SUCCESS) {
                return write_result;
            }

            decompressed_data_ptr = decompressed_data;
        }
    }

    if (status < TINFL_STATUS_DONE) {
        return RESPONSE_INFLATE_ERROR;
    }

    return RESPONSE_SUCCESS;
}

static int s_sync(const struct cmd_ctx *ctx)
{
    /* Bootloader responds to the SYNC request with eight identical SYNC responses.
     * Stub flasher should react the same way so SYNC could be possible with the
     * flasher stub as well. This helps in cases when the chip cannot be reset and
     * the flasher stub keeps running. */

    if (ctx->packet_size != SYNC_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    /* Send 8 identical SYNC responses with value 0.
     * ROM bootloader also sends 8 responses (7 here, last one as return command response) but with non-zero values.
     * The value 0 is used by esptool to detect that it's talking to a flasher stub. */
    for (int i = 0; i < 7; ++i) {
        s_send_response(ESP_SYNC, RESPONSE_SUCCESS, NULL);
    }

    return RESPONSE_SUCCESS;
}

static int s_flash_begin(const struct cmd_ctx *ctx)
{
    return s_init_flash_operation(ctx->data, ctx->packet_size, false);
}

static int s_flash_data(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size < FLASH_DATA_HEADER_SIZE) {
        return RESPONSE_NOT_ENOUGH_DATA;
    }

    int check = s_check_flash_in_progress();
    if (check != RESPONSE_SUCCESS) {
        return check;
    }

    uint32_t data_len = get_le_to_u32(ctx->data);

    const uint8_t *flash_data = ctx->data + FLASH_DATA_HEADER_SIZE;
    const uint16_t actual_data_size = (uint16_t)(ctx->packet_size - FLASH_DATA_HEADER_SIZE);

    if (data_len != actual_data_size) {
        return RESPONSE_TOO_MUCH_DATA;
    }

    // Validate checksum of the flash data
    int checksum_result = s_validate_checksum(flash_data, actual_data_size, ctx->checksum);
    if (checksum_result != RESPONSE_SUCCESS) {
        return checksum_result;
    }

    // Set post-process function (write happens AFTER response sent for double-buffering)
    s_pending_post_process = s_flash_data_post_process;

    return RESPONSE_SUCCESS;
}

static int s_flash_end(const struct cmd_ctx *ctx)
{
    uint32_t reboot_flag = 0;
    int result = s_handle_flash_end(ctx->data, ctx->packet_size, &reboot_flag);

    // If reboot flag is set, reboot the device
    if (result == RESPONSE_SUCCESS && reboot_flag != 0) {
        // TODO: Implement reboot
    }

    return result;
}

static int s_flash_defl_begin(const struct cmd_ctx *ctx)
{
    return s_init_flash_operation(ctx->data, ctx->packet_size, true);
}

static int s_flash_defl_data(const struct cmd_ctx *ctx)
{
#define ADLER32_CHECKSUM_SIZE 4

    if (ctx->packet_size < FLASH_DEFL_DATA_HEADER_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    int check = s_check_flash_in_progress();
    if (check != RESPONSE_SUCCESS) {
        return check;
    }

    // If all expected data has already been decompressed and written,
    // only accept the checksum if it is part of the data.
    if (s_flash_state.total_remaining == 0 && ctx->packet_size > ADLER32_CHECKSUM_SIZE) {
        return RESPONSE_TOO_MUCH_DATA;
    }

    uint32_t data_size = get_le_to_u32(ctx->data);

    const uint8_t *compressed_data = ctx->data + FLASH_DEFL_DATA_HEADER_SIZE;

    // Validate checksum of the compressed data
    int checksum_result = s_validate_checksum(compressed_data, data_size, ctx->checksum);
    if (checksum_result != RESPONSE_SUCCESS) {
        return checksum_result;
    }

    // Set post-process function (decompress+write happens AFTER response sent for double-buffering)
    s_pending_post_process = s_flash_defl_data_post_process;

    return RESPONSE_SUCCESS;
#undef ADLER32_CHECKSUM_SIZE
}

static int s_flash_defl_end(const struct cmd_ctx *ctx)
{
    uint32_t reboot_flag = 0;
    int result = s_handle_flash_end(ctx->data, ctx->packet_size, &reboot_flag);

    if (result == RESPONSE_SUCCESS && reboot_flag != 0) {
        // TODO: Implement reboot
    }

    return result;
}

static int s_mem_begin(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size != MEM_BEGIN_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const uint8_t *ptr = ctx->data;
    s_memory_state.total_remaining = get_le_to_u32(ptr);
    ptr += sizeof(s_memory_state.total_remaining);
    s_memory_state.num_blocks = get_le_to_u32(ptr);
    ptr += sizeof(s_memory_state.num_blocks);
    s_memory_state.block_size = get_le_to_u32(ptr);
    ptr += sizeof(s_memory_state.block_size);
    s_memory_state.offset = get_le_to_u32(ptr);
    s_memory_state.in_progress = true;

    return RESPONSE_SUCCESS;
}

static int s_mem_data(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size < MEM_DATA_HEADER_SIZE) {
        return RESPONSE_NOT_ENOUGH_DATA;
    }

    int check = s_check_memory_in_progress();
    if (check != RESPONSE_SUCCESS) {
        return check;
    }

    uint32_t data_len = get_le_to_u32(ctx->data);

    if (s_memory_state.total_remaining < data_len) {
        return RESPONSE_TOO_MUCH_DATA;
    }

    const uint8_t *mem_data = ctx->data + MEM_DATA_HEADER_SIZE;
    const uint16_t actual_data_size = (uint16_t)(ctx->packet_size - MEM_DATA_HEADER_SIZE);

    if (data_len != actual_data_size) {
        return RESPONSE_TOO_MUCH_DATA;
    }

    memcpy((void *)s_memory_state.offset, mem_data, actual_data_size);
    s_memory_state.offset += actual_data_size;
    s_memory_state.total_remaining -= actual_data_size;

    return RESPONSE_SUCCESS;
}

static int s_mem_end_post_process(const struct cmd_ctx *ctx)
{
    const uint8_t *ptr = ctx->data;
    uint32_t flag = get_le_to_u32(ptr);
    ptr += sizeof(flag);
    uint32_t entrypoint = get_le_to_u32(ptr);

    if (flag == 0) {
        stub_lib_uart_tx_flush(UART_NUM_0);

        // ROM loader firstly exits the loader routine and then executes the entrypoint,
        // but for our purposes, keeping a bit of extra stuff on the stack doesn't really matter.
        void (*run_user_ram_code)(void) = (void(*)(void))entrypoint;
        run_user_ram_code();
    }

    return RESPONSE_SUCCESS;
}

static int s_mem_end(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size != MEM_END_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    int check = s_check_memory_in_progress();
    if (check != RESPONSE_SUCCESS) {
        return check;
    }

    memset(&s_memory_state, 0, sizeof(s_memory_state));

    s_pending_post_process = s_mem_end_post_process;

    return RESPONSE_SUCCESS;
}

static int s_write_reg(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size == 0 || ctx->packet_size % WRITE_REG_ENTRY_SIZE != 0) {
        return RESPONSE_NOT_ENOUGH_DATA;
    }

    const uint16_t command_count = (uint16_t)(ctx->packet_size / WRITE_REG_ENTRY_SIZE);

    for (uint16_t i = 0; i < command_count; i++) {
        const uint8_t *ptr = ctx->data + (i * WRITE_REG_ENTRY_SIZE);

        uint32_t addr = get_le_to_u32(ptr);
        ptr += sizeof(addr);
        uint32_t value = get_le_to_u32(ptr);
        ptr += sizeof(value);
        uint32_t mask = get_le_to_u32(ptr);
        ptr += sizeof(mask);
        uint32_t delay_us = get_le_to_u32(ptr);

        stub_lib_delay_us(delay_us);

        uint32_t write_value = value & mask;
        if (mask != 0xFFFFFFFF) {
            write_value |= REG_READ(addr) & ~mask;
        }
        REG_WRITE(addr, write_value);
    }

    return RESPONSE_SUCCESS;
}

static int s_read_reg(const struct cmd_ctx *ctx, uint32_t *reg_value)
{
    if (ctx->packet_size != READ_REG_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t addr = get_le_to_u32(ctx->data);
    *reg_value = REG_READ(addr);

    return RESPONSE_SUCCESS;
}

static int s_spi_attach(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size != SPI_ATTACH_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    uint32_t ishspi = get_le_to_u32(ctx->data);

    stub_lib_flash_attach(ishspi, 0);
    return RESPONSE_SUCCESS;
}

static int s_spi_set_params(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size != SPI_SET_PARAMS_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const uint8_t *ptr = ctx->data;
    stub_lib_flash_config_t config;
    config.flash_id = get_le_to_u32(ptr);
    ptr += sizeof(config.flash_id);
    config.flash_size = get_le_to_u32(ptr);
    ptr += sizeof(config.flash_size);
    config.block_size = get_le_to_u32(ptr);
    ptr += sizeof(config.block_size);
    config.sector_size = get_le_to_u32(ptr);
    ptr += sizeof(config.sector_size);
    config.page_size = get_le_to_u32(ptr);
    ptr += sizeof(config.page_size);
    config.status_mask = get_le_to_u32(ptr);

    int result = stub_lib_flash_update_config(&config);
    if (result != STUB_LIB_OK) {
        return RESPONSE_FAILED_SPI_OP;
    }

    return RESPONSE_SUCCESS;
}

static int s_change_baudrate_post_process(const struct cmd_ctx *ctx)
{
    uint32_t new_baudrate = get_le_to_u32(ctx->data);

    stub_lib_uart_rominit_set_baudrate(UART_NUM_0, new_baudrate);

    return RESPONSE_SUCCESS;
}

static int s_change_baudrate(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size != CHANGE_BAUDRATE_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    s_pending_post_process = s_change_baudrate_post_process;

    return RESPONSE_SUCCESS;
}

static int s_spi_flash_md5(const struct cmd_ctx *ctx, uint8_t *md5_hash)
{
    if (ctx->packet_size != SPI_FLASH_MD5_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const uint8_t *ptr = ctx->data;
    uint32_t addr = get_le_to_u32(ptr);
    ptr += sizeof(addr);
    uint32_t read_size = get_le_to_u32(ptr);

    // Flash data buffer needs to be aligned to 4 bytes for the flash read function.
    uint8_t data[4096] __attribute__((aligned(4)));

    // Flash address and size needs to be aligned to 4 bytes because of the flash read function.
    // Calculate alignment offset to skip the unaligned bytes.
    uint32_t aligned_addr = ALIGN_DOWN(addr, 4);
    uint32_t offset = addr - aligned_addr;
    uint32_t remaining = read_size;

    struct stub_lib_md5_ctx md5_ctx;
    stub_lib_md5_init(&md5_ctx);

    while (remaining > 0) {
        uint32_t chunk_size = MIN(remaining + offset, sizeof(data));
        uint32_t aligned_chunk_size = ALIGN_UP(chunk_size, 4);

        if (stub_lib_flash_read_buff(aligned_addr, data, aligned_chunk_size) != STUB_LIB_OK) {
            return RESPONSE_FAILED_SPI_OP;
        }

        // Update MD5 with only the requested bytes (skip offset on first iteration)
        uint32_t bytes_to_hash = MIN(remaining, aligned_chunk_size - offset);
        stub_lib_md5_update(&md5_ctx, data + offset, bytes_to_hash);

        aligned_addr += aligned_chunk_size;
        remaining -= bytes_to_hash;
        offset = 0;  // Only apply offset on first chunk
    }

    stub_lib_md5_final(&md5_ctx, md5_hash);

    return RESPONSE_SUCCESS;
}

static int s_get_security_info(const struct cmd_ctx *ctx, uint8_t *security_info, uint16_t *info_size)
{
    if (ctx->packet_size != GET_SECURITY_INFO_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t size = stub_lib_security_info_size();

    uint8_t security_info_buf[size];
    int ret = stub_lib_get_security_info(security_info_buf, sizeof(security_info_buf));

    switch (ret) {
    case STUB_LIB_OK:
        memcpy(security_info, security_info_buf, size);
        *info_size = (uint16_t)size;
        return RESPONSE_SUCCESS;

    case STUB_LIB_ERR_NOT_SUPPORTED:
        return RESPONSE_CMD_NOT_IMPLEMENTED;

    case STUB_LIB_ERR_INVALID_ARG:
        return RESPONSE_BAD_DATA_LEN;

    case STUB_LIB_FAIL:
    default:
        return RESPONSE_BAD_DATA_LEN;
    }
}

static int s_read_flash_post_process(const struct cmd_ctx *ctx)
{
    const uint8_t *ptr = ctx->data;
    uint32_t offset = get_le_to_u32(ptr);
    ptr += sizeof(offset);
    uint32_t read_size = get_le_to_u32(ptr);
    ptr += sizeof(read_size);
    uint32_t packet_size = get_le_to_u32(ptr);
    // Packet contains max in-flight packets, which esptool and other tools set to 64 or higher,
    // but old stub interpreted this always as 1 due to a bug. When interpreted correctly, esptool
    // cannot handle the data flow due to the implementation. Setting it to 1 to avoid possible issues.
    uint32_t max_unacked_packets = 1;

    // Flash data buffer needs to be aligned to 4 bytes for the flash read function.
    // 4096 plus 6 bytes for the alignment and the MD5 hash.
    uint8_t data[4102] __attribute__((aligned(4)));

    // Check if the buffer size (including potential alignment requirements - 6 bytes) is large enough to hold the packet size
    if (packet_size > sizeof(data) - 6) {
        return RESPONSE_BAD_DATA_LEN;
    }

    // Reset slip receive state to avoid reading READ_FLASH command again
    slip_recv_reset();

    uint32_t read_size_remaining = read_size;
    uint32_t sent_packets = 0;
    uint32_t acked_data_size = 0;
    uint32_t acked_packets = 0;

    struct stub_lib_md5_ctx md5_ctx;
    stub_lib_md5_init(&md5_ctx);

    while (read_size_remaining > 0 || acked_data_size < read_size) {
        // Check if packet acknowledgement from a host arrived
        if (slip_is_frame_complete()) {
            size_t slip_size;
            const uint8_t *slip_data = slip_get_frame_data(&slip_size);
            if (slip_size != sizeof(acked_data_size)) {
                break;
            }
            memcpy(&acked_data_size, slip_data, sizeof(acked_data_size));
            acked_packets++;
            slip_recv_reset();
        }

        // If not all data was read and max in-flight packets was not reached, read more data
        if (read_size_remaining > 0 && sent_packets - acked_packets < max_unacked_packets) {
            size_t actual_read_size = MIN(read_size_remaining, packet_size);

            // Align offset and size to 4-byte boundaries for flash read
            uint32_t aligned_offset = ALIGN_DOWN(offset, 4);
            uint32_t align_offset = offset - aligned_offset;
            uint32_t aligned_size = ALIGN_UP(actual_read_size + align_offset, 4);

            if (stub_lib_flash_read_buff(aligned_offset, data, aligned_size) != STUB_LIB_OK) {
                return RESPONSE_FAILED_SPI_OP;
            }

            // Use the data starting from the alignment offset
            uint8_t *actual_data = data + align_offset;
            stub_lib_md5_update(&md5_ctx, actual_data, actual_read_size);
            slip_send_frame(actual_data, actual_read_size);
            offset += actual_read_size;
            read_size_remaining -= actual_read_size;
            sent_packets++;
        }
    }
    uint8_t md5[16];
    stub_lib_md5_final(&md5_ctx, md5);
    slip_send_frame(md5, sizeof(md5));

    return RESPONSE_SUCCESS;
}

static int s_read_flash(const struct cmd_ctx *ctx)
{
    if (ctx->packet_size != READ_FLASH_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    s_pending_post_process = s_read_flash_post_process;

    return RESPONSE_SUCCESS;
}

static int s_erase_flash(const struct cmd_ctx *ctx)
{
    (void)ctx;  // Unused parameter
    int result = stub_lib_flash_erase_chip();
    if (result != STUB_LIB_OK) {
        return RESPONSE_FAILED_SPI_OP;
    }

    return RESPONSE_SUCCESS;
}

static int s_erase_region(const struct cmd_ctx *ctx)
{
    // Timeout values for flash operations, inspired by esptool
#define ERASE_PER_SECTOR_TIMEOUT_US 120000U

    if (ctx->packet_size != ERASE_REGION_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const uint8_t *ptr = ctx->data;
    uint32_t addr = get_le_to_u32(ptr);
    ptr += sizeof(addr);
    uint32_t erase_size = get_le_to_u32(ptr);

    uint64_t timeout_us = (erase_size + STUB_FLASH_SECTOR_SIZE - 1) / STUB_FLASH_SECTOR_SIZE * ERASE_PER_SECTOR_TIMEOUT_US;

    if (addr % STUB_FLASH_SECTOR_SIZE || erase_size % STUB_FLASH_SECTOR_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    while (erase_size > 0 && timeout_us > 0) {
        stub_lib_flash_start_next_erase(&addr, &erase_size);
        stub_lib_delay_us(1);
        --timeout_us;
    }
    if (stub_lib_flash_wait_ready(timeout_us) != STUB_LIB_OK) {
        return RESPONSE_FAILED_SPI_OP;
    }

    return RESPONSE_SUCCESS;
#undef ERASE_PER_SECTOR_TIMEOUT_US
}

static void s_spi_nand_attach(const uint8_t *buffer, uint16_t size)
{
    if (size != SPI_NAND_ATTACH_SIZE) {
        s_send_response(ESP_SPI_NAND_ATTACH, RESPONSE_BAD_DATA_LEN, NULL);
        return;
    }

    const uint32_t *params = (const uint32_t *)buffer;
    uint32_t hspi_arg = params[0];

    int result = nand_attach(hspi_arg);
    if (result != 0) {
        s_send_response(ESP_SPI_NAND_ATTACH, RESPONSE_FAILED_SPI_OP, NULL);
        return;
    }

    s_send_response(ESP_SPI_NAND_ATTACH, RESPONSE_SUCCESS, NULL);
}

static void s_spi_nand_read_spare(const uint8_t *buffer, uint16_t size)
{
    if (size != SPI_NAND_READ_SPARE_SIZE) {
        s_send_response(ESP_SPI_NAND_READ_SPARE, RESPONSE_BAD_DATA_LEN, NULL);
        return;
    }

    const uint32_t *params = (const uint32_t *)buffer;
    uint32_t page_number = params[0];

    uint8_t spare_data[4] = {0}; // Read up to 4 bytes
    int result = nand_read_spare(page_number, spare_data);
    if (result != 0) {
        s_send_response(ESP_SPI_NAND_READ_SPARE, RESPONSE_FAILED_SPI_OP, NULL);
        return;
    }

    // Return spare data as value (first 4 bytes as uint32, little-endian)
    uint32_t spare_value = spare_data[0] | (spare_data[1] << 8) |
                           (spare_data[2] << 16) | (spare_data[3] << 24);
    struct command_response_data response = {
        .value = spare_value,
        .data_size = 0
    };
    s_send_response(ESP_SPI_NAND_READ_SPARE, RESPONSE_SUCCESS, &response);
}

static void s_spi_nand_write_spare(const uint8_t *buffer, uint16_t size)
{
    // Expect 5 bytes: 4-byte page_number + 1-byte is_bad
    if (size != 5) {
        s_send_response(ESP_SPI_NAND_WRITE_SPARE, RESPONSE_BAD_DATA_LEN, NULL);
        return;
    }

    // Parse: first 4 bytes = page_number (little-endian), last byte = is_bad
    uint32_t page_number = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
    uint8_t is_bad = buffer[4];

    int result = nand_write_spare(page_number, is_bad);
    if (result != 0) {
        s_send_response(ESP_SPI_NAND_WRITE_SPARE, RESPONSE_FAILED_SPI_OP, NULL);
        return;
    }

    s_send_response(ESP_SPI_NAND_WRITE_SPARE, RESPONSE_SUCCESS, NULL);
}

void handle_command(const uint8_t *buffer, size_t size)
{
    // Accumulates errors from previous post-process and current command
    static int accumulated_result = RESPONSE_SUCCESS;

    const uint8_t *ptr = buffer;
    uint8_t direction = *ptr++;
    uint8_t command = *ptr++;
    uint16_t packet_size = get_le_to_u16(ptr);
    ptr += sizeof(packet_size);
    uint32_t checksum = get_le_to_u32(ptr);
    ptr += sizeof(checksum);

    const uint8_t *data = ptr;

    if (direction != DIRECTION_REQUEST) {
        s_send_response(command, RESPONSE_INVALID_COMMAND, NULL);
        return;
    }

    if (size != (size_t)(packet_size + HEADER_SIZE)) {
        s_send_response(command, RESPONSE_BAD_DATA_LEN, NULL);
        return;
    }

    // Create command context
    struct cmd_ctx ctx = {
        .command = command,
        .direction = direction,
        .packet_size = packet_size,
        .checksum = checksum,
        .data = data
    };

    // If previous post-process failed, send that error and skip command execution
    if (accumulated_result != RESPONSE_SUCCESS) {
        s_send_response(command, accumulated_result, NULL);
        accumulated_result = RESPONSE_SUCCESS;
        return;
    }

    // Initialize response data for commands that return data
    struct command_response_data response = {0};

    switch (command) {
    case ESP_SYNC:
        accumulated_result = s_sync(&ctx);
        break;

    case ESP_FLASH_BEGIN:
        accumulated_result = s_flash_begin(&ctx);
        break;

    case ESP_FLASH_DATA:
        accumulated_result = s_flash_data(&ctx);
        break;

    case ESP_FLASH_END:
        accumulated_result = s_flash_end(&ctx);
        break;

    case ESP_MEM_BEGIN:
        accumulated_result = s_mem_begin(&ctx);
        break;

    case ESP_MEM_DATA:
        accumulated_result = s_mem_data(&ctx);
        break;

    case ESP_MEM_END:
        accumulated_result = s_mem_end(&ctx);
        break;

    case ESP_WRITE_REG:
        accumulated_result = s_write_reg(&ctx);
        break;

    case ESP_READ_REG:
        accumulated_result = s_read_reg(&ctx, &response.value);
        break;

    case ESP_SPI_ATTACH:
        accumulated_result = s_spi_attach(&ctx);
        break;

    case ESP_SPI_SET_PARAMS:
        accumulated_result = s_spi_set_params(&ctx);
        break;

    case ESP_CHANGE_BAUDRATE:
        accumulated_result = s_change_baudrate(&ctx);
        break;

    case ESP_FLASH_DEFL_BEGIN:
        accumulated_result = s_flash_defl_begin(&ctx);
        break;

    case ESP_FLASH_DEFL_DATA:
        accumulated_result = s_flash_defl_data(&ctx);
        break;

    case ESP_FLASH_DEFL_END:
        accumulated_result = s_flash_defl_end(&ctx);
        break;

    case ESP_SPI_FLASH_MD5:
        accumulated_result = s_spi_flash_md5(&ctx, response.data);
        response.data_size = 16;  // MD5 hash is always 16 bytes
        break;

    case ESP_GET_SECURITY_INFO:
        accumulated_result = s_get_security_info(&ctx, response.data, &response.data_size);
        break;

    case ESP_READ_FLASH:
        accumulated_result = s_read_flash(&ctx);
        break;

    case ESP_ERASE_FLASH:
        accumulated_result = s_erase_flash(&ctx);
        break;

    case ESP_ERASE_REGION:
        accumulated_result = s_erase_region(&ctx);
        break;

    case ESP_RUN_USER_CODE:
        /*
        This command does not send response.
        TODO: Try to implement WDT reset to trigger system reset
        */
        return;  // No response needed

    case ESP_SPI_NAND_ATTACH:
        s_spi_nand_attach(data, packet_size);
        break;

    case ESP_SPI_NAND_READ_SPARE:
        s_spi_nand_read_spare(data, packet_size);
        break;

    case ESP_SPI_NAND_WRITE_SPARE:
        s_spi_nand_write_spare(data, packet_size);
        break;

    default:
        accumulated_result = RESPONSE_INVALID_COMMAND;
        break;
    }

    if (accumulated_result == RESPONSE_SUCCESS) {
        s_send_response(command, RESPONSE_SUCCESS, &response);
    } else {
        s_send_response(command, accumulated_result, NULL);
        s_pending_post_process = NULL;
    }

    accumulated_result = RESPONSE_SUCCESS;

    // Execute post-process AFTER response is sent (if registered by command)
    // Any error from post-process carries over to the next command
    if (s_pending_post_process) {
        accumulated_result = s_pending_post_process(&ctx);
        s_pending_post_process = NULL;
    }
}
