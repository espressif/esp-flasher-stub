/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESPTool flasher stub commands
 */
typedef enum __attribute__((packed))
{
    ESP_SYNC = 0x08,
    ESP_FLASH_BEGIN = 0x02,
    ESP_FLASH_DATA = 0x03,
    ESP_FLASH_END = 0x04,
    ESP_MEM_BEGIN = 0x05,
    ESP_MEM_DATA = 0x06,
    ESP_MEM_END = 0x07,
    ESP_WRITE_REG = 0x09,
    ESP_READ_REG = 0x0a,
    ESP_SPI_ATTACH = 0x0d,
    ESP_SPI_SET_PARAMS = 0x0b,
    ESP_CHANGE_BAUDRATE = 0x0f,
    ESP_FLASH_DEFL_BEGIN = 0x10,
    ESP_FLASH_DEFL_DATA = 0x11,
    ESP_FLASH_DEFL_END = 0x12,
    ESP_SPI_FLASH_MD5 = 0x13,
    ESP_GET_SECURITY_INFO = 0x14,
    ESP_READ_FLASH = 0x15,
    ESP_ERASE_FLASH = 0x16,
    ESP_ERASE_REGION = 0x17,
    ESP_RUN_USER_CODE = 0x18
} esp_command_t;

/**
 * @brief Response status - SUCCESS or FAIL
 */
typedef enum __attribute__((packed))
{
    SUCCESS = 0x00,
    FAIL = 0x01
} response_status_t;

/**
 * @brief ESP command error codes
 * Used as the error_msg byte when status is FAIL
 */
typedef enum __attribute__((packed))
{
    NO_ERROR = 0x00,

    BAD_DATA_LEN = 0xC0,
    BAD_DATA_CHECKSUM = 0xC1,
    BAD_BLOCKSIZE = 0xC2,
    INVALID_COMMAND = 0xC3,
    FAILED_SPI_OP = 0xC4,
    FAILED_SPI_UNLOCK = 0xC5,
    NOT_IN_FLASH_MODE = 0xC6,
    INFLATE_ERROR = 0xC7,
    NOT_ENOUGH_DATA = 0xC8,
    TOO_MUCH_DATA = 0xC9,

    CMD_NOT_IMPLEMENTED = 0xFF,
} error_code_t;

// Command parameter structures

typedef struct __attribute__((packed))
{
    uint32_t total_size;     // Total size to be flashed
    uint32_t num_blocks;     // Number of blocks
    uint32_t block_size;     // Size of each block
    uint32_t offset;         // Flash offset
} flash_begin_params_t;

typedef struct __attribute__((packed))
{
    uint32_t data_len;       // Length of data
    uint32_t seq;            // Sequence number
    uint32_t reserved1;      // Reserved
    uint32_t reserved2;      // Reserved
} flash_data_params_t;

typedef struct __attribute__((packed))
{
    uint32_t flag;           // Reboot flag (0 = stay in stub, 1 = reboot)
} flash_end_params_t;

typedef struct __attribute__((packed))
{
    uint32_t total_size;     // Total size
    uint32_t num_blocks;     // Number of blocks
    uint32_t block_size;     // Block size
    uint32_t offset;         // Memory offset
} mem_begin_params_t;

typedef struct __attribute__((packed))
{
    uint32_t data_len;       // Length of data
    uint32_t seq;            // Sequence number
    uint32_t reserved1;      // Reserved
    uint32_t reserved2;      // Reserved
} mem_data_params_t;

typedef struct __attribute__((packed))
{
    uint32_t flag;           // Run user code flag (0 = stay in stub, 1 = run user code)
    uint32_t entrypoint;     // Entrypoint address
} mem_end_params_t;

typedef struct __attribute__((packed))
{
    uint32_t addr;           // Register address
    uint32_t value;          // Value to write
    uint32_t mask;           // Mask (optional)
    uint32_t delay_us;       // Delay after write (microseconds)
} write_reg_params_t;

typedef struct __attribute__((packed))
{
    uint32_t addr;           // Register address
} read_reg_params_t;

typedef struct __attribute__((packed))
{
    uint32_t config;         // SPI configuration
} spi_attach_params_t;

typedef struct __attribute__((packed))
{
    uint32_t id;             // Flash ID
    uint32_t total_size;     // Flash size
    uint32_t block_size;     // Block size
    uint32_t sector_size;    // Sector size
    uint32_t page_size;      // Page size
    uint32_t status_mask;    // Status mask
} spi_set_params_params_t;

typedef struct __attribute__((packed))
{
    uint32_t offset;         // Flash offset to read from
    uint32_t size;           // Number of bytes to read
    uint32_t block_size;     // Block size
    uint32_t max_in_flight;  // Maximum blocks in flight
} read_flash_params_t;

#ifdef __cplusplus
}
#endif
