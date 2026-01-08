/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
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
#define ESP_MEM_END             0x06
#define ESP_MEM_DATA            0x07
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
#define ESP_ERASE_FLASH         0xD0
#define ESP_ERASE_REGION        0xD1
#define ESP_READ_FLASH          0xD2
#define ESP_RUN_USER_CODE       0xD3

/**
 * @brief ESP command response codes (16-bit)
 * Wire format: big-endian (2 bytes, high byte first)
 */
typedef enum {
    RESPONSE_SUCCESS             = 0x0000,
    RESPONSE_BAD_DATA_LEN        = 0xC000,
    RESPONSE_BAD_DATA_CHECKSUM   = 0xC100,
    RESPONSE_BAD_BLOCKSIZE       = 0xC200,
    RESPONSE_INVALID_COMMAND     = 0xC300,
    RESPONSE_FAILED_SPI_OP       = 0xC400,
    RESPONSE_FAILED_SPI_UNLOCK   = 0xC500,
    RESPONSE_NOT_IN_FLASH_MODE   = 0xC600,
    RESPONSE_INFLATE_ERROR       = 0xC700,
    RESPONSE_NOT_ENOUGH_DATA     = 0xC800,
    RESPONSE_TOO_MUCH_DATA       = 0xC900,
    RESPONSE_CMD_NOT_IMPLEMENTED = 0xFF00
} esp_response_code_t;

/**
 * @brief ESP command expected data sizes (in bytes)
 * Used for parameter validation in command handlers
 */
#define SYNC_SIZE                   36
#define FLASH_BEGIN_SIZE            16
#define FLASH_BEGIN_ENC_SIZE        20
#define FLASH_DATA_HEADER_SIZE      16
#define FLASH_END_SIZE              4
#define MEM_BEGIN_SIZE              16
#define MEM_DATA_HEADER_SIZE        16
#define MEM_END_SIZE                8
#define WRITE_REG_ENTRY_SIZE        16
#define READ_REG_SIZE               4
#define SPI_ATTACH_SIZE             4
#define SPI_SET_PARAMS_SIZE         24
#define CHANGE_BAUDRATE_SIZE        8
#define FLASH_DEFL_BEGIN_SIZE       16
#define FLASH_DEFL_BEGIN_ENC_SIZE   20
#define FLASH_DEFL_DATA_HEADER_SIZE 16
#define FLASH_DEFL_END_SIZE         4
#define SPI_FLASH_MD5_SIZE          16
#define GET_SECURITY_INFO_SIZE      0
#define READ_FLASH_SIZE             16
#define ERASE_FLASH_SIZE            0
#define ERASE_REGION_SIZE           8

#ifdef __cplusplus
}
#endif
