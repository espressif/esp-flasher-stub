/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// SPI NAND Flash commands
#define CMD_SET_REGISTER    0x1F
#define CMD_READ_REGISTER   0x0F
#define CMD_WRITE_ENABLE    0x06
#define CMD_PAGE_READ       0x13
#define CMD_PROGRAM_EXECUTE 0x10
#define CMD_PROGRAM_LOAD    0x84
#define CMD_READ_FROM_CACHE 0x03
#define CMD_ERASE_BLOCK     0xD8

// NAND Flash registers
#define REG_PROTECT         0xA0
#define REG_CONFIG          0xB0
#define REG_STATUS          0xC0

// Status register bits
#define STAT_BUSY           (1 << 0)
#define STAT_WRITE_ENABLED  (1 << 1)
#define STAT_ERASE_FAILED   (1 << 2)
#define STAT_PROGRAM_FAILED (1 << 3)

// NAND flash configuration
typedef struct {
    uint32_t page_size;      // typically 2048 bytes
    uint32_t pages_per_block; // typically 64
    uint32_t block_size;     // page_size * pages_per_block
    bool initialized;
} nand_config_t;

/**
 * @brief Initialize NAND flash interface
 * @param hspi_arg SPI configuration argument (reserved for future use)
 * @return 0 on success, negative on error
 */
int nand_attach(uint32_t hspi_arg);

/**
 * @brief Read NAND spare area (OOB) from a page
 * @param page_number Page number to read spare from
 * @param spare_data Output buffer for spare data (at least 2 bytes)
 * @return 0 on success, negative on error
 */
int nand_read_spare(uint32_t page_number, uint8_t *spare_data);

/**
 * @brief Write NAND spare area (OOB) to mark bad blocks
 * @param page_number Page number to write spare to
 * @param is_bad 1 to mark as bad block, 0 to mark as good
 * @return 0 on success, negative on error
 */
int nand_write_spare(uint32_t page_number, uint8_t is_bad);

#ifdef __cplusplus
}
#endif
