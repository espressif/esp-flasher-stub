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
