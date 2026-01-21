/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "nand.h"
#include "spi2_nand.h"
#include <string.h>
#include <esp-stub-lib/err.h>

// Wrapper function to allow runtime switching
static inline int spi_transaction_wrapper(uint8_t cmd,
                                          const uint8_t *addr, uint8_t addr_bits,
                                          const uint8_t *tx_data, uint16_t tx_bits,
                                          uint8_t *rx_data, uint16_t rx_bits)
{
    return spi2_transaction(cmd, addr, addr_bits, tx_data, tx_bits, rx_data, rx_bits);
}

// Delay function
extern void esp_rom_delay_us(uint32_t us);

static nand_config_t s_nand_config = {
    .page_size = 2048,        // Default 2KB page size
    .pages_per_block = 64,    // Default 64 pages per block
    .block_size = 128 * 1024, // 128KB blocks
    .initialized = false
};

/**
 * @brief Wait for NAND to be ready by polling status register
 * @return 0 on success, negative on error or timeout
 */
static int nand_wait_ready(void)
{
    int timeout = 100000; // Timeout counter

    while (timeout-- > 0) {
        uint8_t status;
        uint8_t reg_addr = REG_STATUS;

        // Read status register (CMD 0x0F + address 0xC0)
        int ret = spi_transaction_wrapper(CMD_READ_REGISTER, &reg_addr, 8, NULL, 0, &status, 8);
        if (ret != 0) {
            return ret;
        }

        if ((status & STAT_BUSY) == 0) {
            // Check for errors
            if (status & STAT_ERASE_FAILED) {
                return -2;  // Erase failed
            }
            if (status & STAT_PROGRAM_FAILED) {
                return -3;  // Program failed
            }
            return 0; // Ready
        }

        esp_rom_delay_us(10); // Small delay between polls
    }

    return -5; // Timeout
}

/**
 * @brief Read NAND register
 * @param reg Register address
 * @param val Output value
 * @return 0 on success, negative on error
 */
static int nand_read_register(uint8_t reg, uint8_t *val) __attribute__((unused));
static int nand_read_register(uint8_t reg, uint8_t *val)
{
    return spi_transaction_wrapper(CMD_READ_REGISTER, &reg, 8, NULL, 0, val, 8);
}

/**
 * @brief Write NAND register
 * @param reg Register address
 * @param val Value to write
 * @return 0 on success, negative on error
 */
static int nand_write_register(uint8_t reg, uint8_t val) __attribute__((unused));
static int nand_write_register(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = {reg, val};
    return spi_transaction_wrapper(CMD_SET_REGISTER, NULL, 0, data, 16, NULL, 0);
}

/**
 * @brief Issue write enable command
 * @return 0 on success, negative on error
 */
static int nand_write_enable(void)
{
    return spi_transaction_wrapper(CMD_WRITE_ENABLE, NULL, 0, NULL, 0, NULL, 0);
}

int nand_attach(uint32_t hspi_arg)
{
    (void)hspi_arg;

    // Initialize both drivers for comparison testing
    int ret;

    ret = spi2_init();
    if (ret != 0) {
        return ret;
    }

    // W25N01GV requires 5ms after power-up for internal initialization (tVSL)
    esp_rom_delay_us(5000);

    // Issue RESET command (0xFF) to ensure clean state
    ret = spi_transaction_wrapper(0xFF, NULL, 0, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -100;  // Failed at RESET
    }

    // Wait for reset completion (tRST = 5ms for W25N01GV)
    esp_rom_delay_us(5000);

    // Verify chip is ready by reading status register
    uint8_t status;
    uint8_t reg_addr = REG_STATUS;  // 0xC0
    ret = spi_transaction_wrapper(CMD_READ_REGISTER, &reg_addr, 8, NULL, 0, &status, 8);
    if (ret != 0) {
        return -101;  // Failed at status read
    }

    // Check BUSY bit (bit 0) should be clear
    if (status & STAT_BUSY) {
        return -4;  // Chip still busy after reset
    }

    s_nand_config.initialized = true;
    s_nand_config.page_size = 2048;  // W25N01GV page size
    return 0;
}

int nand_read_spare(uint32_t page_number, uint8_t *spare_data)
{
    if (!s_nand_config.initialized) {
        return -1;
    }

    // Step 1: Issue PAGE READ command (0x13) with 3-byte page address
    // This loads the page from NAND array into cache
    uint8_t page_addr[3];
    page_addr[0] = (uint8_t)((page_number >> 16) & 0xFF);
    page_addr[1] = (uint8_t)((page_number >> 8) & 0xFF);
    page_addr[2] = (uint8_t)(page_number & 0xFF);

    int ret = spi_transaction_wrapper(CMD_PAGE_READ, page_addr, 24, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -10;  // Failed at PAGE READ
    }

    // Step 2: Wait for page read to complete
    ret = nand_wait_ready();
    if (ret != 0) {
        return -20 + ret;  // Failed waiting for ready
    }

    // Step 3: Read from cache at column address = page_size (spare area starts here)
    // READ FROM CACHE command (0x03) + 2-byte column address + 1 dummy byte
    uint16_t column = (uint16_t)s_nand_config.page_size;
    uint8_t col_addr[3];
    col_addr[0] = (uint8_t)((column >> 8) & 0xFF);
    col_addr[1] = (uint8_t)(column & 0xFF);
    col_addr[2] = 0;  // Dummy byte

    // Read first 2 bytes of spare area (bad block marker)
    ret = spi_transaction_wrapper(CMD_READ_FROM_CACHE, col_addr, 24, NULL, 0, spare_data, 16);
    if (ret != 0) {
        return -30;  // Failed at READ FROM CACHE
    }

    return 0;
}

int nand_write_spare(uint32_t page_number, uint8_t is_bad)
{
    if (!s_nand_config.initialized) {
        return -1;
    }

    // Prepare bad block marker
    uint8_t bad_block_marker[2];
    if (is_bad != 0) {
        bad_block_marker[0] = 0x00;
        bad_block_marker[1] = 0x00;
    } else {
        bad_block_marker[0] = 0xFF;
        bad_block_marker[1] = 0xFF;
    }

    // Step 1: Write enable
    int ret = nand_write_enable();
    if (ret != 0) {
        return ret;
    }

    // Step 2: PROGRAM LOAD command (0x84) + 2-byte column address + data
    uint16_t column = (uint16_t)s_nand_config.page_size;
    uint8_t col_addr[2];
    col_addr[0] = (uint8_t)((column >> 8) & 0xFFU);
    col_addr[1] = (uint8_t)(column & 0xFFU);

    ret = spi_transaction_wrapper(CMD_PROGRAM_LOAD, col_addr, 16, bad_block_marker, 16, NULL, 0);
    if (ret != 0) {
        return -20;  // Failed PROGRAM LOAD
    }

    // Step 3: PROGRAM EXECUTE command (0x10) + 3-byte page address
    uint8_t page_addr[3];
    page_addr[0] = (uint8_t)((page_number >> 16) & 0xFF);
    page_addr[1] = (uint8_t)((page_number >> 8) & 0xFF);
    page_addr[2] = (uint8_t)(page_number & 0xFF);

    ret = spi_transaction_wrapper(CMD_PROGRAM_EXECUTE, page_addr, 24, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -30;  // Failed PROGRAM EXECUTE
    }

    // Step 4: Wait for program to complete
    ret = nand_wait_ready();
    if (ret != 0) {
        return -40 + ret;  // Failed waiting for program complete
    }

    return 0;
}
