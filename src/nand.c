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
static uint8_t s_last_status_byte = 0xFF;

static int nand_wait_ready(void)
{
    int timeout = 100000;

    while (timeout-- > 0) {
        uint8_t status;
        uint8_t reg_addr = REG_STATUS;

        int ret = spi_transaction_wrapper(CMD_READ_REGISTER, &reg_addr, 8, NULL, 0, &status, 8);
        if (ret != 0) {
            return ret;
        }

        s_last_status_byte = status;

        if ((status & STAT_BUSY) == 0) {
            if (status & STAT_ERASE_FAILED) {
                return -2;
            }
            if (status & STAT_PROGRAM_FAILED) {
                return -3;
            }
            return 0;
        }

        esp_rom_delay_us(10);
    }

    return -5;
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
static int nand_write_register(uint8_t reg, uint8_t val)
{
    /* Set Feature: command 0x1F, then 2-byte data (register, value) */
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

int nand_read_id(uint8_t *manufacturer_id, uint16_t *device_id)
{
    // JEDEC Read ID: cmd 0x9F + 1 dummy byte → 3 bytes (mfr, dev_hi, dev_lo)
    // Use address phase for the dummy byte
    uint8_t dummy = 0x00;
    uint8_t id_buf[3] = {0};
    int ret = spi_transaction_wrapper(0x9F, &dummy, 8, NULL, 0, id_buf, 24);
    if (ret != 0) {
        return ret;
    }
    if (manufacturer_id) {
        *manufacturer_id = id_buf[0];
    }
    if (device_id) {
        *device_id = ((uint16_t)id_buf[1] << 8) | id_buf[2];
    }
    return 0;
}

static uint8_t s_debug_id[3] = {0};
static uint8_t s_debug_extra[3] = {0};

int nand_attach(uint32_t hspi_arg)
{
    (void)hspi_arg;

    int ret;

    ret = spi2_init();
    if (ret != 0) {
        return ret;
    }

    esp_rom_delay_us(5000);

    // Reset the NAND chip
    ret = spi_transaction_wrapper(0xFF, NULL, 0, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -100;
    }

    esp_rom_delay_us(10000);

    // Wait for chip to become ready after reset
    ret = nand_wait_ready();
    if (ret != 0) {
        return -(0x100 + s_last_status_byte);
    }

    s_nand_config.initialized = true;
    s_nand_config.page_size = 2048;

    // Read JEDEC ID: cmd 0x9F + 1 dummy byte (address phase) + 3 bytes response
    uint8_t dummy = 0x00;
    spi_transaction_wrapper(0x9F, &dummy, 8, NULL, 0, s_debug_id, 24);

    // Read status register (0xC0)
    uint8_t status = 0xFF;
    uint8_t reg_addr = 0xC0;
    spi_transaction_wrapper(0x0F, &reg_addr, 8, NULL, 0, &status, 8);
    s_debug_extra[0] = status;

    // Read protection register (0xA0)
    reg_addr = 0xA0;
    spi_transaction_wrapper(0x0F, &reg_addr, 8, NULL, 0, &status, 8);
    s_debug_extra[1] = status;

    // Read configuration register (0xB0)
    reg_addr = 0xB0;
    spi_transaction_wrapper(0x0F, &reg_addr, 8, NULL, 0, &status, 8);
    s_debug_extra[2] = status;

    // Clear write protection (W25N01GV ships with all blocks protected)
    ret = nand_write_register(REG_PROTECT, 0x00);
    if (ret != 0) {
        return ret;
    }

    // Verify protection was cleared (read back)
    uint8_t prot_after = 0xFF;
    nand_read_register(REG_PROTECT, &prot_after);
    s_debug_extra[1] = prot_after;
    if (prot_after != 0x00) {
        return -50;  /* Protection register could not be cleared */
    }

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

    uint8_t bad_block_marker[2];
    if (is_bad != 0) {
        bad_block_marker[0] = 0x00;
        bad_block_marker[1] = 0x00;
    } else {
        bad_block_marker[0] = 0xFF;
        bad_block_marker[1] = 0xFF;
    }

    int ret = nand_write_enable();
    if (ret != 0) {
        return ret;
    }

    uint16_t column = (uint16_t)s_nand_config.page_size;
    uint8_t col_addr[2];
    col_addr[0] = (uint8_t)((column >> 8) & 0xFFU);
    col_addr[1] = (uint8_t)(column & 0xFFU);

    ret = spi_transaction_wrapper(CMD_PROGRAM_LOAD_RANDOM, col_addr, 16, bad_block_marker, 16, NULL, 0);
    if (ret != 0) {
        return -20;
    }

    uint8_t page_addr[3];
    page_addr[0] = (uint8_t)((page_number >> 16) & 0xFF);
    page_addr[1] = (uint8_t)((page_number >> 8) & 0xFF);
    page_addr[2] = (uint8_t)(page_number & 0xFF);

    ret = spi_transaction_wrapper(CMD_PROGRAM_EXECUTE, page_addr, 24, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -30;
    }

    ret = nand_wait_ready();
    if (ret != 0) {
        return -40 + ret;
    }

    return 0;
}

#define SPI2_MAX_RX_BYTES 64
#define SPI2_MAX_TX_BYTES 64

int nand_write_page(uint32_t page_number, const uint8_t *buf)
{
    if (!s_nand_config.initialized) {
        return -1;
    }

    int ret = nand_write_enable();
    if (ret != 0) {
        return ret;
    }

    // Program Load in 64-byte chunks: first chunk 0x02 (Load at col 0), rest 0x84 (Random)
    uint32_t offset = 0;
    while (offset < s_nand_config.page_size) {
        uint32_t chunk = s_nand_config.page_size - offset;
        if (chunk > SPI2_MAX_TX_BYTES) {
            chunk = SPI2_MAX_TX_BYTES;
        }

        uint16_t column = (uint16_t)offset;
        uint8_t col_addr[2];
        col_addr[0] = (uint8_t)((column >> 8) & 0xFF);
        col_addr[1] = (uint8_t)(column & 0xFF);

        uint8_t cmd = (offset == 0) ? CMD_PROGRAM_LOAD : CMD_PROGRAM_LOAD_RANDOM;
        ret = spi_transaction_wrapper(cmd, col_addr, 16, buf + offset, (uint16_t)(chunk * 8), NULL, 0);
        if (ret != 0) {
            return -20;
        }

        offset += chunk;
    }

    // Program Execute with 3-byte page address
    uint8_t page_addr[3];
    page_addr[0] = (uint8_t)((page_number >> 16) & 0xFF);
    page_addr[1] = (uint8_t)((page_number >> 8) & 0xFF);
    page_addr[2] = (uint8_t)(page_number & 0xFF);

    ret = spi_transaction_wrapper(CMD_PROGRAM_EXECUTE, page_addr, 24, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -30;
    }

    esp_rom_delay_us(500);  /* Allow program to start before polling */
    ret = nand_wait_ready();
    if (ret != 0) {
        return -40 + ret;
    }

    return 0;
}

int nand_erase_block(uint32_t page_number)
{
    if (!s_nand_config.initialized) {
        return -1;
    }

    int ret = nand_write_enable();
    if (ret != 0) {
        return ret;
    }

    uint8_t page_addr[3];
    page_addr[0] = (uint8_t)((page_number >> 16) & 0xFF);
    page_addr[1] = (uint8_t)((page_number >> 8) & 0xFF);
    page_addr[2] = (uint8_t)(page_number & 0xFF);

    ret = spi_transaction_wrapper(CMD_ERASE_BLOCK, page_addr, 24, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -10;
    }

    ret = nand_wait_ready();
    if (ret != 0) {
        return -20 + ret;
    }

    esp_rom_delay_us(2000);  /* Allow block to settle after erase */
    return 0;
}

int nand_read_page(uint32_t page_number, uint8_t *buf, uint32_t buf_size)
{
    if (!s_nand_config.initialized) {
        return -1;
    }

    uint32_t read_len = buf_size;
    if (read_len > s_nand_config.page_size) {
        read_len = s_nand_config.page_size;
    }

    // Step 1: PAGE_READ (0x13) — load page from NAND array into cache
    uint8_t page_addr[3];
    page_addr[0] = (uint8_t)((page_number >> 16) & 0xFF);
    page_addr[1] = (uint8_t)((page_number >> 8) & 0xFF);
    page_addr[2] = (uint8_t)(page_number & 0xFF);

    int ret = spi_transaction_wrapper(CMD_PAGE_READ, page_addr, 24, NULL, 0, NULL, 0);
    if (ret != 0) {
        return -10;
    }

    // Step 2: Wait for page transfer to complete
    ret = nand_wait_ready();
    if (ret != 0) {
        return -20 + ret;
    }

    // Step 3: READ_FROM_CACHE (0x03) in chunks
    // W25N01GV READ FROM CACHE: 0x03 + 2-byte column addr + 1 dummy byte + data out
    // Column address = byte offset within the page
    uint32_t offset = 0;
    while (offset < read_len) {
        uint32_t chunk = read_len - offset;
        if (chunk > SPI2_MAX_RX_BYTES) {
            chunk = SPI2_MAX_RX_BYTES;
        }

        uint16_t column = (uint16_t)offset;
        uint8_t col_addr[3];
        col_addr[0] = (uint8_t)((column >> 8) & 0xFF);
        col_addr[1] = (uint8_t)(column & 0xFF);
        col_addr[2] = 0x00;  // dummy byte

        ret = spi_transaction_wrapper(CMD_READ_FROM_CACHE, col_addr, 24,
                                      NULL, 0, buf + offset, (uint16_t)(chunk * 8));
        if (ret != 0) {
            return -30;
        }

        offset += chunk;
    }

    return 0;
}

uint32_t nand_get_page_size(void)
{
    return s_nand_config.page_size;
}

const uint8_t *nand_get_debug_id(void)
{
    return s_debug_id;
}

const uint8_t *nand_get_debug_extra(void)
{
    return s_debug_extra;
}
