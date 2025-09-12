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
 * Should be max 0xFF to be compatible with protocol
 */
#define ESP_FLASH_BEGIN         0x02
#define ESP_FLASH_DATA          0x03
#define ESP_FLASH_END           0x04
#define ESP_MEM_BEGIN           0x05
#define ESP_MEM_DATA            0x06
#define ESP_MEM_END             0x07
#define ESP_SYNC                0x08
#define ESP_WRITE_REG           0x09
#define ESP_READ_REG            0x0A
#define ESP_SPI_SET_PARAMS      0x0B
#define ESP_SPI_ATTACH          0x0D
#define ESP_CHANGE_BAUDRATE     0x0F
#define ESP_FLASH_DEFL_BEGIN    0x10
#define ESP_FLASH_DEFL_DATA     0x11
#define ESP_FLASH_DEFL_END      0x12
#define ESP_SPI_FLASH_MD5       0x13
#define ESP_GET_SECURITY_INFO   0x14
#define ESP_READ_FLASH          0xD0
#define ESP_ERASE_FLASH         0xD1
#define ESP_ERASE_REGION        0xD2
#define ESP_RUN_USER_CODE       0xD3

/**
 * @brief Response status - SUCCESS or FAIL
 * Should be max 0xFF to be compatible with protocol
 */
#define SUCCESS 0x00
#define FAIL    0x01

/**
 * @brief ESP command error codes
 * Used as the error_msg byte when status is FAIL
 * Should be max 0xFF to be compatible with protocol
 */
#define NO_ERROR            0x00

#define BAD_DATA_LEN        0xC0
#define BAD_DATA_CHECKSUM   0xC1
#define BAD_BLOCKSIZE       0xC2
#define INVALID_COMMAND     0xC3
#define FAILED_SPI_OP       0xC4
#define FAILED_SPI_UNLOCK   0xC5
#define NOT_IN_FLASH_MODE   0xC6
#define INFLATE_ERROR       0xC7
#define NOT_ENOUGH_DATA     0xC8
#define TOO_MUCH_DATA       0xC9

#define CMD_NOT_IMPLEMENTED 0xFF

/**
 * @brief ESP command expected data sizes (in bytes)
 * Used for parameter validation in command handlers
 */
#define FLASH_BEGIN_SIZE        16
#define FLASH_DATA_HEADER_SIZE  16
#define FLASH_END_SIZE          4
#define MEM_BEGIN_SIZE          16
#define MEM_DATA_HEADER_SIZE    16
#define MEM_END_SIZE            8
#define WRITE_REG_ENTRY_SIZE    16
#define READ_REG_SIZE           4
#define SPI_ATTACH_SIZE         4
#define SPI_SET_PARAMS_SIZE     24
#define CHANGE_BAUDRATE_SIZE    8
#define FLASH_DEFL_BEGIN_SIZE   4
#define FLASH_DEFL_DATA_SIZE    16
#define FLASH_DEFL_END_SIZE     4
#define SPI_FLASH_MD5_SIZE      16
#define GET_SECURITY_INFO_SIZE  0
#define READ_FLASH_SIZE         16
#define ERASE_FLASH_SIZE        0
#define ERASE_REGION_SIZE       8

/**
 * @brief Combined response structure with status and error
 * Used to send response status and error code together
 */
struct esp_response {
    uint8_t status;     // response_status_t value (SUCCESS/FAIL)
    uint8_t error;      // error_code_t value
};

#ifdef __cplusplus
}
#endif
