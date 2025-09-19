/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "commands.h"
#include "command_handler.h"

#define MAX_COMMAND_SIZE (sizeof(common_command_t) + 0x4000)
#define MAX_RESPONSE_DATA_SIZE (64)
#define MAX_RESPONSE_SIZE (sizeof(common_response_t) + sizeof(response_status_t) + \
                          sizeof(error_code_t) + MAX_RESPONSE_DATA_SIZE)

typedef struct __attribute__((packed))
{
    uint8_t direction;      /* 0x00 for request, 0x01 for response */
    esp_command_t command;     /* Command ID */
    uint16_t size;          /* Data payload size */
    uint32_t checksum;      /* Simple checksum of data */
} common_command_t;

typedef struct __attribute__((packed))
{
    uint8_t direction;
    esp_command_t command;
    uint16_t size;
    uint32_t value;
} common_response_t;

typedef struct {
    uint32_t total_size;
    uint32_t num_blocks;
    uint32_t block_size;
    uint32_t offset;
    uint32_t blocks_written;
    bool in_progress;
} operation_state_t;

static operation_state_t s_flash_state = {0};
static operation_state_t s_memory_state = {0};

static void handle_sync(void);
static void handle_flash_begin(const void* data, size_t len);
static void handle_flash_data(const void* data, size_t len);
static void handle_flash_end(const void* data, size_t len);
static void handle_mem_begin(const void* data, size_t len);
static void handle_mem_data(const void* data, size_t len);
static void handle_mem_end(const void* data, size_t len);
static void handle_write_reg(const void* data, size_t len);
static void handle_read_reg(const void* data, size_t len);
static void handle_spi_attach(const void* data, size_t len);
static void handle_spi_set_params(const void* data, size_t len);
static void handle_change_baudrate(const void* data, size_t len);
static void handle_flash_defl_begin(const void* data, size_t len);
static void handle_flash_defl_data(const void* data, size_t len);
static void handle_flash_defl_end(const void* data, size_t len);
static void handle_spi_flash_md5(const void* data, size_t len);
static void handle_get_security_info(const void* data, size_t len);
static void handle_read_flash(const void* data, size_t len);
static void handle_erase_flash(const void* data, size_t len);
static void handle_erase_region(const void* data, size_t len);

static void send_response_packet(esp_command_t command, uint32_t value, const void* data, size_t data_len,
                                 response_status_t status, error_code_t error);

static inline void send_error_response(esp_command_t command, error_code_t error)
{
    send_response_packet(command, 0, NULL, 0, FAIL, error);
}

static inline void send_success_response(esp_command_t command, uint32_t value, const void* data, size_t data_len)
{
    send_response_packet(command, value, data, data_len, SUCCESS, NO_ERROR);
}

static inline uint32_t read_reg(uint32_t reg)
{
    return *(volatile uint32_t*)reg;
}

static inline void write_reg(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t*)reg = val;
}

void run_command_loop()
{
    uint8_t data_buffer[MAX_COMMAND_SIZE];

    while (1) {
        size_t len = 0; // TODO: Receive SLIP frame
        if (len == 0) {
            continue;
        }

        const common_command_t *packet = (const common_command_t*)data_buffer;
        const uint8_t* data = data_buffer + sizeof(common_command_t);

        if (len != packet->size + sizeof(common_command_t)) {
            send_error_response(packet->command, BAD_DATA_LEN);
            continue;
        }

        switch (packet->command) {
        case ESP_SYNC:
            handle_sync();
            break;

        case ESP_FLASH_BEGIN:
            handle_flash_begin(data, packet->size);
            break;

        case ESP_FLASH_DATA:
            handle_flash_data(data, packet->size);
            break;

        case ESP_FLASH_END:
            handle_flash_end(data, packet->size);
            break;

        case ESP_MEM_BEGIN:
            handle_mem_begin(data, packet->size);
            break;

        case ESP_MEM_DATA:
            handle_mem_data(data, packet->size);
            break;

        case ESP_MEM_END:
            handle_mem_end(data, packet->size);
            break;

        case ESP_WRITE_REG:
            handle_write_reg(data, packet->size);
            break;

        case ESP_READ_REG:
            handle_read_reg(data, packet->size);
            break;

        case ESP_SPI_ATTACH:
            handle_spi_attach(data, packet->size);
            break;

        case ESP_SPI_SET_PARAMS:
            handle_spi_set_params(data, packet->size);
            break;

        case ESP_CHANGE_BAUDRATE:
            handle_change_baudrate(data, packet->size);
            break;

        case ESP_FLASH_DEFL_BEGIN:
            handle_flash_defl_begin(data, packet->size);
            break;

        case ESP_FLASH_DEFL_DATA:
            handle_flash_defl_data(data, packet->size);
            break;

        case ESP_FLASH_DEFL_END:
            handle_flash_defl_end(data, packet->size);
            break;

        case ESP_SPI_FLASH_MD5:
            handle_spi_flash_md5(data, packet->size);
            break;

        case ESP_GET_SECURITY_INFO:
            handle_get_security_info(data, packet->size);
            break;

        case ESP_READ_FLASH:
            handle_read_flash(data, packet->size);
            break;

        case ESP_ERASE_FLASH:
            handle_erase_flash(data, packet->size);
            break;

        case ESP_ERASE_REGION:
            handle_erase_region(data, packet->size);
            break;

        case ESP_RUN_USER_CODE:
            /* This command does not send response, return will run user code */
            return;

        default:
            send_error_response(packet->command, INVALID_COMMAND);
            break;
        }
    }
}

static void handle_sync(void)
{
    send_response_packet(ESP_SYNC, 0, NULL, 0, SUCCESS, NO_ERROR);
}

static void handle_flash_begin(const void* data, size_t len)
{
    (void)len; // TODO: Consider verifying data length with known command size

    const flash_begin_params_t *params = (const flash_begin_params_t*)data;
    s_flash_state.total_size = params->total_size;
    s_flash_state.num_blocks = params->num_blocks;
    s_flash_state.block_size = params->block_size;
    s_flash_state.offset = params->offset;
    s_flash_state.blocks_written = 0;
    s_flash_state.in_progress = true;

    // TODO: Do any necessary flash initialization
    send_success_response(ESP_FLASH_BEGIN, 0, NULL, 0);
}

static void handle_flash_data(const void* data, size_t len)
{
    if (!s_flash_state.in_progress) {
        send_error_response(ESP_FLASH_DATA, NOT_IN_FLASH_MODE);
        return;
    }

    if (len < sizeof(flash_data_params_t)) {
        send_error_response(ESP_FLASH_DATA, NOT_ENOUGH_DATA);
        return;
    }

    const flash_data_params_t *params = (const flash_data_params_t*)data;
    const uint8_t* flash_data = (const uint8_t*)data + sizeof(flash_data_params_t);
    const size_t data_size = len - sizeof(flash_data_params_t);

    if (params->data_len != data_size) {
        send_error_response(ESP_FLASH_DATA, TOO_MUCH_DATA);
        return;
    }

    const uint32_t flash_addr = s_flash_state.offset + (params->seq * s_flash_state.block_size);

    // TODO: Write data to flash using stub_lib_flash_write
    (void)flash_addr;
    (void)flash_data;

    s_flash_state.blocks_written++;
    send_success_response(ESP_FLASH_DATA, 0, NULL, 0);
}

static void handle_flash_end(const void* data, size_t len)
{
    (void)len;  // TODO: Consider verifying data length with known command size

    if (!s_flash_state.in_progress) {
        send_error_response(ESP_FLASH_END, NOT_IN_FLASH_MODE);
        return;
    }

    const flash_end_params_t *params = (const flash_end_params_t*)data;
    s_flash_state.total_size = 0;
    s_flash_state.num_blocks = 0;
    s_flash_state.block_size = 0;
    s_flash_state.offset = 0;
    s_flash_state.blocks_written = 0;
    s_flash_state.in_progress = false;

    // TODO: Perform any necessary cleanup
    send_success_response(ESP_FLASH_END, 0, NULL, 0);

    // If reboot flag is set, reboot the device
    if (params->flag != 0) {
        // TODO: Implement reboot
    }
}

static void handle_mem_begin(const void* data, size_t len)
{
    (void)len; // TODO: Consider verifying data length with known command size

    const mem_begin_params_t *params = (const mem_begin_params_t*)data;
    s_memory_state.total_size = params->total_size;
    s_memory_state.num_blocks = params->num_blocks;
    s_memory_state.block_size = params->block_size;
    s_memory_state.offset = params->offset;
    s_memory_state.blocks_written = 0;
    s_memory_state.in_progress = true;

    send_success_response(ESP_MEM_BEGIN, 0, NULL, 0);
}

static void handle_mem_data(const void* data, size_t len)
{
    // TODO: Consider verifying data length with known command size
    if (!s_memory_state.in_progress) {
        send_error_response(ESP_MEM_DATA, NOT_IN_FLASH_MODE);
        return;
    }

    if (len < sizeof(mem_data_params_t)) {
        send_error_response(ESP_MEM_DATA, NOT_ENOUGH_DATA);
        return;
    }

    const mem_data_params_t *params = (const mem_data_params_t*)data;
    const uint8_t* mem_data = (const uint8_t*)data + sizeof(mem_data_params_t);
    const size_t data_size = len - sizeof(mem_data_params_t);

    if (params->data_len != data_size) {
        send_error_response(ESP_MEM_DATA, TOO_MUCH_DATA);
        return;
    }

    const uint32_t mem_addr = s_memory_state.offset + (params->seq * s_memory_state.block_size);

    memcpy((void*)mem_addr, mem_data, data_size);

    s_memory_state.blocks_written++;
    send_success_response(ESP_MEM_DATA, 0, NULL, 0);
}

static void handle_mem_end(const void* data, size_t len)
{
    (void)len;  // TODO: Consider verifying data length with known command size

    if (!s_memory_state.in_progress) {
        send_error_response(ESP_MEM_END, NOT_IN_FLASH_MODE);
        return;
    }

    const mem_end_params_t *params = (const mem_end_params_t*)data;
    s_memory_state.total_size = 0;
    s_memory_state.num_blocks = 0;
    s_memory_state.block_size = 0;
    s_memory_state.offset = 0;
    s_memory_state.blocks_written = 0;
    s_memory_state.in_progress = false;

    send_success_response(ESP_MEM_END, 0, NULL, 0);

    if (params->flag == 1) {
        // Flush potential response data
        // TODO: Add flush
        // TODO: Add delay

        // ROM loader firstly exits the loader routine and then executes the entrypoint,
        // but for our purposes, keeping a bit of extra stuff on the stack doesn't really matter.
        void (*run_user_ram_code)(void) = (void(*)(void))params->entrypoint;
        run_user_ram_code();
    }
}

static void handle_write_reg(const void* data, size_t len)
{
    // write reg command can send multiple registers in one packet
    if (len == 0 || len % sizeof(write_reg_params_t) != 0) {
        send_error_response(ESP_WRITE_REG, NOT_ENOUGH_DATA);
        return;
    }

    const size_t command_count = len / sizeof(write_reg_params_t);
    const write_reg_params_t* params_array = (const write_reg_params_t*)data;

    for (size_t i = 0; i < command_count; ++i) {
        const write_reg_params_t *params = &params_array[i];

        // TODO: Add delay based on params->delay_us

        uint32_t value = params->value & params->mask;
        if (params->mask != 0xFFFFFFFF) {
            value |= read_reg(params->addr) & ~params->mask;
        }
        write_reg(params->addr, value);
    }

    send_success_response(ESP_WRITE_REG, 0, NULL, 0);
}

static void handle_read_reg(const void* data, size_t len)
{
    (void)len; // TODO: Consider verifying data length with known command size

    const read_reg_params_t *params = (const read_reg_params_t*)data;
    const uint32_t value = read_reg(params->addr);

    send_success_response(ESP_READ_REG, value, NULL, 0);
}

static void handle_spi_attach(const void* data, size_t len)
{
    (void)data;
    (void)len;
    send_error_response(ESP_SPI_ATTACH, CMD_NOT_IMPLEMENTED);
}

static void handle_spi_set_params(const void* data, size_t len)
{
    (void)data;
    (void)len;
    send_error_response(ESP_SPI_SET_PARAMS, CMD_NOT_IMPLEMENTED);
}

static void handle_change_baudrate(const void* data, size_t len)
{
    (void)data;
    (void)len;
    send_error_response(ESP_CHANGE_BAUDRATE, CMD_NOT_IMPLEMENTED);
}

static void handle_flash_defl_begin(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_FLASH_DEFL_BEGIN, CMD_NOT_IMPLEMENTED);
}

static void handle_flash_defl_data(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_FLASH_DEFL_DATA, CMD_NOT_IMPLEMENTED);
}

static void handle_flash_defl_end(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_FLASH_DEFL_END, CMD_NOT_IMPLEMENTED);
}

static void handle_spi_flash_md5(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_SPI_FLASH_MD5, CMD_NOT_IMPLEMENTED);
}

static void handle_get_security_info(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_GET_SECURITY_INFO, CMD_NOT_IMPLEMENTED);
}

static void handle_read_flash(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_READ_FLASH, CMD_NOT_IMPLEMENTED);
}

static void handle_erase_flash(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_ERASE_FLASH, CMD_NOT_IMPLEMENTED);
}

static void handle_erase_region(const void* data, size_t len)
{
    (void)data; (void)len;
    send_error_response(ESP_ERASE_REGION, CMD_NOT_IMPLEMENTED);
}

inline uint32_t calculate_checksum(const void* data, size_t len)
{
    uint32_t checksum = 0xEF;
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        checksum ^= bytes[i];
    }
    return checksum;
}

static void send_response_packet(esp_command_t command, uint32_t value, const void* data, size_t data_len,
                                 response_status_t status, error_code_t error)
{
    uint8_t response_buffer[MAX_RESPONSE_SIZE];

    if (data_len > MAX_RESPONSE_DATA_SIZE) {
        data_len = MAX_RESPONSE_DATA_SIZE;
    }

    const size_t combined_len = sizeof(response_status_t) + sizeof(error_code_t) + data_len;

    common_response_t packet;
    packet.direction = 0x01;
    packet.command = command;
    packet.size = (uint16_t)combined_len;
    packet.value = value;

    memcpy(response_buffer, &packet, sizeof(packet));
    response_buffer[sizeof(packet)] = (uint8_t)status;
    response_buffer[sizeof(packet) + 1] = (uint8_t)error;

    if (data_len > 0 && data != NULL) {
        memcpy(response_buffer, data, data_len);
    }
    // TODO: Implement send response packet using SLIP
}
