/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <esp-stub-lib/soc_utils.h>
#include <esp-stub-lib/flash.h>
#include <esp-stub-lib/err.h>
#include <esp-stub-lib/uart.h>
#include <esp-stub-lib/rom_wrappers.h>
#include "slip.h"
#include "commands.h"
#include "command_handler.h"
#include <esp-stub-lib/rom_wrappers.h>
#include <esp-stub-lib/err.h>
#include <esp-stub-lib/security.h>
#include <esp-stub-lib/uart.h>

struct operation_state {
    uint32_t total_size;
    uint32_t num_blocks;
    uint32_t block_size;
    uint32_t offset;
    uint32_t blocks_written;
    bool encrypt;
    bool in_progress;
};

#define DIRECTION_REQUEST 0x00
#define DIRECTION_RESPONSE 0x01

#define RESPONSE_STATUS_SIZE 2
#define MAX_RESPONSE_DATA_SIZE 64
#define MAX_RESPONSE_SIZE (HEADER_SIZE + MAX_RESPONSE_DATA_SIZE + RESPONSE_STATUS_SIZE)

static struct operation_state s_flash_state = {0};
static struct operation_state s_memory_state = {0};

static void s_send_response_packet(uint8_t command, uint32_t value, uint8_t* data, uint16_t data_size,
                                   esp_response_code_t response);

static inline void s_send_error_response(uint8_t command, esp_response_code_t response)
{
    s_send_response_packet(command, 0, NULL, 0, response);
}

static inline void s_send_success_response(uint8_t command, uint32_t value, uint8_t* data, uint16_t data_size)
{
    s_send_response_packet(command, value, data, data_size, RESPONSE_SUCCESS);
}

static void s_sync(uint16_t size)
{
    /* Bootloader responds to the SYNC request with eight identical SYNC responses.
     * Stub flasher should react the same way so SYNC could be possible with the
     * flasher stub as well. This helps in cases when the chip cannot be reset and
     * the flasher stub keeps running. */

    if (size != SYNC_SIZE) {
        s_send_error_response(ESP_SYNC, RESPONSE_BAD_DATA_LEN);
        return;
    }

    /* Send 8 identical SYNC responses with value 0.
     * ROM bootloader also sends 8 responses but with non-zero values.
     * The value 0 is used by esptool to detect that it's talking to a flasher stub. */
    for (int i = 0; i < 8; ++i) {
        s_send_success_response(ESP_SYNC, 0, NULL, 0);
    }
}

static void s_flash_begin(const uint8_t* buffer, uint16_t size)
{
    if (size != FLASH_BEGIN_SIZE && size != FLASH_BEGIN_ENC_SIZE) {
        s_send_error_response(ESP_FLASH_BEGIN, RESPONSE_BAD_DATA_LEN);
        return;
    }

    const uint32_t* params = (const uint32_t*)buffer;
    s_flash_state.total_size = params[0];
    s_flash_state.num_blocks = params[1];
    s_flash_state.block_size = params[2];
    s_flash_state.offset = params[3];
    if (size == FLASH_BEGIN_ENC_SIZE) {
        s_flash_state.encrypt = params[4];
    } else {
        s_flash_state.encrypt = false;
    }
    s_flash_state.blocks_written = 0;
    s_flash_state.in_progress = true;

    // TODO: Do any necessary flash initialization
    s_send_success_response(ESP_FLASH_BEGIN, 0, NULL, 0);
}

static void s_flash_data(const uint8_t* buffer, uint16_t size)
{
    if (size < FLASH_DATA_HEADER_SIZE) {
        s_send_error_response(ESP_FLASH_DATA, RESPONSE_NOT_ENOUGH_DATA);
        return;
    }

    if (!s_flash_state.in_progress) {
        s_send_error_response(ESP_FLASH_DATA, RESPONSE_NOT_IN_FLASH_MODE);
        return;
    }

    const uint32_t* params = (const uint32_t*)buffer;
    uint32_t data_len = params[0];
    uint32_t seq = params[1];

    const uint8_t* flash_data = buffer + FLASH_DATA_HEADER_SIZE;
    const uint16_t actual_data_size = (uint16_t)(size - FLASH_DATA_HEADER_SIZE);

    if (data_len != actual_data_size) {
        s_send_error_response(ESP_FLASH_DATA, RESPONSE_TOO_MUCH_DATA);
        return;
    }

    const uint32_t flash_addr = s_flash_state.offset + (seq * s_flash_state.block_size);

    int result = stub_lib_flash_write_buff(flash_addr, flash_data, actual_data_size, s_flash_state.encrypt);
    if (result != STUB_LIB_OK) {
        s_send_error_response(ESP_FLASH_DATA, RESPONSE_FAILED_SPI_OP);
        return;
    }

    ++s_flash_state.blocks_written;
    s_send_success_response(ESP_FLASH_DATA, 0, NULL, 0);
}

static void s_flash_end(const uint8_t* buffer, uint16_t size)
{
    if (size != FLASH_END_SIZE) {
        s_send_error_response(ESP_FLASH_END, RESPONSE_BAD_DATA_LEN);
        return;
    }

    if (!s_flash_state.in_progress) {
        s_send_error_response(ESP_FLASH_END, RESPONSE_NOT_IN_FLASH_MODE);
        return;
    }

    uint32_t flag = *(const uint32_t*)buffer;

    s_flash_state.total_size = 0;
    s_flash_state.num_blocks = 0;
    s_flash_state.block_size = 0;
    s_flash_state.offset = 0;
    s_flash_state.blocks_written = 0;
    s_flash_state.in_progress = false;

    // TODO: Perform any necessary cleanup
    s_send_success_response(ESP_FLASH_END, 0, NULL, 0);

    // If reboot flag is set, reboot the device
    if (flag != 0) {
        // TODO: Implement reboot
    }
}

static void s_mem_begin(const uint8_t* buffer, uint16_t size)
{
    if (size != MEM_BEGIN_SIZE) {
        s_send_error_response(ESP_MEM_BEGIN, RESPONSE_BAD_DATA_LEN);
        return;
    }

    const uint32_t* params = (const uint32_t*)buffer;
    s_memory_state.total_size = params[0];
    s_memory_state.num_blocks = params[1];
    s_memory_state.block_size = params[2];
    s_memory_state.offset = params[3];

    s_memory_state.blocks_written = 0;
    s_memory_state.in_progress = true;

    s_send_success_response(ESP_MEM_BEGIN, 0, NULL, 0);
}

static void s_mem_data(const uint8_t* buffer, uint16_t size)
{
    if (size < MEM_DATA_HEADER_SIZE) {
        s_send_error_response(ESP_MEM_DATA, RESPONSE_NOT_ENOUGH_DATA);
        return;
    }

    if (!s_memory_state.in_progress) {
        s_send_error_response(ESP_MEM_DATA, RESPONSE_NOT_IN_FLASH_MODE);
        return;
    }

    const uint32_t* params = (const uint32_t*)buffer;
    uint32_t data_len = params[0];
    uint32_t seq = params[1];

    const uint8_t* mem_data = buffer + MEM_DATA_HEADER_SIZE;
    const uint16_t actual_data_size = (uint16_t)(size - MEM_DATA_HEADER_SIZE);

    if (data_len != actual_data_size) {
        s_send_error_response(ESP_MEM_DATA, RESPONSE_TOO_MUCH_DATA);
        return;
    }

    uint32_t mem_addr = s_memory_state.offset + (seq * s_memory_state.block_size);

    memcpy((void*)mem_addr, mem_data, actual_data_size);

    ++s_memory_state.blocks_written;
    s_send_success_response(ESP_MEM_DATA, 0, NULL, 0);
}

static void s_mem_end(const uint8_t* buffer, uint16_t size)
{
    if (size != MEM_END_SIZE) {
        s_send_error_response(ESP_MEM_END, RESPONSE_BAD_DATA_LEN);
        return;
    }

    if (!s_memory_state.in_progress) {
        s_send_error_response(ESP_MEM_END, RESPONSE_NOT_IN_FLASH_MODE);
        return;
    }

    const uint32_t* params = (const uint32_t*)buffer;
    uint32_t flag = params[0];
    uint32_t entrypoint = params[1];

    s_memory_state.total_size = 0;
    s_memory_state.num_blocks = 0;
    s_memory_state.block_size = 0;
    s_memory_state.offset = 0;
    s_memory_state.blocks_written = 0;
    s_memory_state.in_progress = false;

    s_send_success_response(ESP_MEM_END, 0, NULL, 0);

    if (flag == 1) {
        // TODO: consider delay - was in previous code
        stub_lib_uart_tx_flush();

        // ROM loader firstly exits the loader routine and then executes the entrypoint,
        // but for our purposes, keeping a bit of extra stuff on the stack doesn't really matter.
        void (*run_user_ram_code)(void) = (void(*)(void))entrypoint;
        run_user_ram_code();
    }
}

static void s_write_reg(const uint8_t* buffer, uint16_t size)
{
    if (size == 0 || size % WRITE_REG_ENTRY_SIZE != 0) {
        s_send_error_response(ESP_WRITE_REG, RESPONSE_NOT_ENOUGH_DATA);
        return;
    }

    const uint16_t command_count = (uint16_t)(size / WRITE_REG_ENTRY_SIZE);

    for (uint16_t i = 0; i < command_count; i++) {
        const uint32_t* reg_params = (const uint32_t*)(buffer + (i * WRITE_REG_ENTRY_SIZE));

        uint32_t addr = reg_params[0];
        uint32_t value = reg_params[1];
        uint32_t mask = reg_params[2];
        uint32_t delay_us = reg_params[3];

        stub_lib_delay_us(delay_us);

        uint32_t write_value = value & mask;
        if (mask != 0xFFFFFFFF) {
            write_value |= REG_READ(addr) & ~mask;
        }
        REG_WRITE(addr, write_value);
    }

    s_send_success_response(ESP_WRITE_REG, 0, NULL, 0);
}

static void s_read_reg(const uint8_t* buffer, uint16_t size)
{
    if (size != READ_REG_SIZE) {
        s_send_error_response(ESP_READ_REG, RESPONSE_BAD_DATA_LEN);
        return;
    }

    uint32_t addr = *(const uint32_t*)buffer;
    const uint32_t value = REG_READ(addr);
    s_send_success_response(ESP_READ_REG, value, NULL, 0);
}

static void s_spi_attach(const uint8_t* buffer, uint16_t size)
{
    if (size != SPI_ATTACH_SIZE) {
        s_send_error_response(ESP_SPI_ATTACH, RESPONSE_BAD_DATA_LEN);
        return;
    }
    const uint32_t* params = (const uint32_t*)buffer;
    uint32_t ishspi = params[0];
    bool legacy = params[1];

    stub_lib_flash_attach(ishspi, legacy);
    s_send_success_response(ESP_SPI_ATTACH, 0, NULL, 0);
}

static void s_spi_set_params(const uint8_t* buffer, uint16_t size)
{
    if (size != SPI_SET_PARAMS_SIZE) {
        s_send_error_response(ESP_SPI_SET_PARAMS, RESPONSE_BAD_DATA_LEN);
        return;
    }

    const uint32_t* params = (const uint32_t*)buffer;
    stub_lib_flash_config_t config = {
        .flash_id = params[0],
        .flash_size = params[1],
        .block_size = params[2],
        .sector_size = params[3],
        .page_size = params[4],
        .status_mask = params[5]
    };

    int result = stub_lib_flash_update_config(&config);
    if (result != STUB_LIB_OK) {
        s_send_error_response(ESP_SPI_SET_PARAMS, RESPONSE_FAILED_SPI_OP);
        return;
    }

    s_send_success_response(ESP_SPI_SET_PARAMS, 0, NULL, 0);
}

static void s_change_baudrate(const uint8_t* buffer, uint16_t size)
{
    if (size != CHANGE_BAUDRATE_SIZE) {
        s_send_error_response(ESP_CHANGE_BAUDRATE, RESPONSE_BAD_DATA_LEN);
        return;
    }

    (void)buffer;
    s_send_error_response(ESP_CHANGE_BAUDRATE, RESPONSE_CMD_NOT_IMPLEMENTED);
}

static void s_flash_defl_begin(const uint8_t* buffer, uint16_t size)
{
    const uint8_t expected_size = sizeof(uint32_t);
    if (size != expected_size) {
        s_send_error_response(ESP_FLASH_DEFL_BEGIN, RESPONSE_BAD_DATA_LEN);
        return;
    }

    (void)buffer;
    s_send_error_response(ESP_FLASH_DEFL_BEGIN, RESPONSE_CMD_NOT_IMPLEMENTED);
}

static void s_flash_defl_data(const uint8_t* buffer, uint16_t size)
{
    const uint8_t expected_size = 4 * sizeof(uint32_t);
    if (size < expected_size) {
        s_send_error_response(ESP_FLASH_DEFL_DATA, RESPONSE_BAD_DATA_LEN);
        return;
    }

    (void)buffer;
    s_send_error_response(ESP_FLASH_DEFL_DATA, RESPONSE_CMD_NOT_IMPLEMENTED);
}

static void s_flash_defl_end(const uint8_t* buffer, uint16_t size)
{
    const uint8_t expected_size = sizeof(uint32_t);
    if (size != expected_size) {
        s_send_error_response(ESP_FLASH_DEFL_END, RESPONSE_BAD_DATA_LEN);
        return;
    }

    (void)buffer;
    s_send_error_response(ESP_FLASH_DEFL_END, RESPONSE_CMD_NOT_IMPLEMENTED);
}

static void s_spi_flash_md5(const uint8_t* buffer, uint16_t size)
{
    const uint8_t expected_size = 4 * sizeof(uint32_t);
    if (size != expected_size) {
        s_send_error_response(ESP_SPI_FLASH_MD5, RESPONSE_BAD_DATA_LEN);
        return;
    }

    (void)buffer;
    s_send_error_response(ESP_SPI_FLASH_MD5, RESPONSE_CMD_NOT_IMPLEMENTED);
}

static void s_get_security_info(uint16_t size)
{
    if (size != GET_SECURITY_INFO_SIZE) {
        s_send_error_response(ESP_GET_SECURITY_INFO, RESPONSE_BAD_DATA_LEN);
        return;
    }

    uint32_t info_size = stub_lib_security_info_size();

    uint8_t security_info_buf[info_size];
    int ret = stub_lib_get_security_info(security_info_buf, sizeof(security_info_buf));

    switch (ret) {
    case STUB_LIB_OK:
        s_send_success_response(ESP_GET_SECURITY_INFO, 0, security_info_buf, (uint16_t)info_size);
        break;

    case STUB_LIB_ERR_NOT_SUPPORTED:
        s_send_error_response(ESP_GET_SECURITY_INFO, RESPONSE_CMD_NOT_IMPLEMENTED);
        break;

    case STUB_LIB_ERR_INVALID_ARG:
        s_send_error_response(ESP_GET_SECURITY_INFO, RESPONSE_BAD_DATA_LEN);
        break;

    case STUB_LIB_FAIL:
    default:
        s_send_error_response(ESP_GET_SECURITY_INFO, RESPONSE_BAD_DATA_LEN);
        break;
    }
}

static void s_read_flash(const uint8_t* buffer, uint16_t size)
{
    const uint8_t expected_size = 4 * sizeof(uint32_t);
    if (size != expected_size) {
        s_send_error_response(ESP_READ_FLASH, RESPONSE_BAD_DATA_LEN);
        return;
    }

    (void)buffer;
    s_send_error_response(ESP_READ_FLASH, RESPONSE_CMD_NOT_IMPLEMENTED);
}

static void s_erase_flash(void)
{
    int result = stub_lib_flash_erase_chip();
    if (result != STUB_LIB_OK) {
        s_send_error_response(ESP_ERASE_FLASH, RESPONSE_FAILED_SPI_OP);
        return;
    }

    s_send_success_response(ESP_ERASE_FLASH, 0, NULL, 0);
}

static void s_erase_region(const uint8_t* buffer, uint16_t size)
{
    if (size != ERASE_REGION_SIZE) {
        s_send_error_response(ESP_ERASE_REGION, RESPONSE_BAD_DATA_LEN);
        return;
    }

    const uint32_t* params = (const uint32_t*)buffer;
    uint32_t addr = params[0];
    uint32_t erase_size = params[1];

    int result = stub_lib_flash_erase_area(addr, erase_size);
    if (result != STUB_LIB_OK) {
        s_send_error_response(ESP_ERASE_REGION, RESPONSE_FAILED_SPI_OP);
        return;
    }

    s_send_success_response(ESP_ERASE_REGION, 0, NULL, 0);
}

inline uint32_t calculate_checksum(const void* data, uint16_t size)
{
    uint32_t checksum = 0xEF;
    const uint8_t* bytes = data;
    for (uint16_t i = 0; i < size; i++) {
        checksum ^= bytes[i];
    }
    return checksum;
}

static void s_send_response_packet(uint8_t command, uint32_t value, uint8_t* data, uint16_t data_size,
                                   esp_response_code_t response)
{
    uint8_t response_buffer[MAX_RESPONSE_SIZE] = {0};

    if (data_size > MAX_RESPONSE_DATA_SIZE) {
        data_size = MAX_RESPONSE_DATA_SIZE;
    }

    const uint16_t resp_data_size = (uint16_t)(data_size + RESPONSE_STATUS_SIZE);
    const uint16_t total_frame_size = (uint16_t)(HEADER_SIZE + resp_data_size);

    uint8_t direction_byte = DIRECTION_RESPONSE;

    uint8_t* ptr = response_buffer;
    *ptr++ = direction_byte;
    *ptr++ = command;
    *(uint16_t*)ptr = resp_data_size;
    ptr += sizeof(resp_data_size);
    *(uint32_t*)ptr = value;
    ptr += sizeof(value);

    if (data && data_size > 0) {
        memcpy(ptr, data, data_size);
        ptr += data_size;
    }
    // Write response code in big-endian format (remote reads as ">H")
    *ptr++ = (uint8_t)(((uint16_t)response >> 8) & 0xFF);
    *ptr++ = (uint8_t)((uint16_t)response & 0xFF);

    slip_send_frame(response_buffer, total_frame_size);
}

void handle_command(const uint8_t *buffer, size_t size)
{
    const uint8_t* ptr = buffer;
    uint8_t direction = *ptr++;
    uint8_t command = *ptr++;
    uint16_t packet_size = *(const uint16_t*)ptr;
    ptr += sizeof(packet_size);
    uint32_t checksum = *(const uint32_t*)ptr;
    ptr += sizeof(checksum);

    const uint8_t* data = ptr;

    if (direction != DIRECTION_REQUEST) {
        s_send_error_response(command, RESPONSE_INVALID_COMMAND);
        return;
    }

    if (size != (size_t)(packet_size + HEADER_SIZE)) {
        s_send_error_response(command, RESPONSE_BAD_DATA_LEN);
        return;
    }

    switch (command) {
    case ESP_SYNC:
        s_sync(packet_size);
        break;

    case ESP_FLASH_BEGIN:
        s_flash_begin(data, packet_size);
        break;

    case ESP_FLASH_DATA:
        s_flash_data(data, packet_size);
        break;

    case ESP_FLASH_END:
        s_flash_end(data, packet_size);
        break;

    case ESP_MEM_BEGIN:
        s_mem_begin(data, packet_size);
        break;

    case ESP_MEM_DATA:
        s_mem_data(data, packet_size);
        break;

    case ESP_MEM_END:
        s_mem_end(data, packet_size);
        break;

    case ESP_WRITE_REG:
        s_write_reg(data, packet_size);
        break;

    case ESP_READ_REG:
        s_read_reg(data, packet_size);
        break;

    case ESP_SPI_ATTACH:
        s_spi_attach(data, packet_size);
        break;

    case ESP_SPI_SET_PARAMS:
        s_spi_set_params(data, packet_size);
        break;

    case ESP_CHANGE_BAUDRATE:
        s_change_baudrate(data, packet_size);
        break;

    case ESP_FLASH_DEFL_BEGIN:
        s_flash_defl_begin(data, packet_size);
        break;

    case ESP_FLASH_DEFL_DATA:
        s_flash_defl_data(data, packet_size);
        break;

    case ESP_FLASH_DEFL_END:
        s_flash_defl_end(data, packet_size);
        break;

    case ESP_SPI_FLASH_MD5:
        s_spi_flash_md5(data, packet_size);
        break;

    case ESP_GET_SECURITY_INFO:
        s_get_security_info(packet_size);
        break;

    case ESP_READ_FLASH:
        s_read_flash(data, packet_size);
        break;

    case ESP_ERASE_FLASH:
        s_erase_flash();
        break;

    case ESP_ERASE_REGION:
        s_erase_region(data, packet_size);
        break;

    case ESP_RUN_USER_CODE:
        /*
        This command does not send response.
        TODO: Try to implement WDT reset to trigger system reset
        */
        break;

    default: {
        s_send_error_response(command, RESPONSE_INVALID_COMMAND);
        break;
    }
    }
    return;
}
