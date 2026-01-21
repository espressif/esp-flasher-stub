/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "spi2_nand.h"
#include <string.h>

// SPI2 base address
#define SPI2_BASE 0x60024000

// SPI Register offsets
#define SPI_CMD_REG(i)          (SPI2_BASE + 0x000)
#define SPI_ADDR_REG(i)         (SPI2_BASE + 0x004)
#define SPI_CTRL_REG(i)         (SPI2_BASE + 0x008)
#define SPI_CLOCK_REG(i)        (SPI2_BASE + 0x00C)
#define SPI_USER_REG(i)         (SPI2_BASE + 0x010)
#define SPI_USER1_REG(i)        (SPI2_BASE + 0x014)
#define SPI_USER2_REG(i)        (SPI2_BASE + 0x018)
#define SPI_MS_DLEN_REG(i)      (SPI2_BASE + 0x01C)
#define SPI_MISC_REG(i)         (SPI2_BASE + 0x020)
#define SPI_W0_REG(i)           (SPI2_BASE + 0x098)
#define SPI_DMA_CONF_REG(i)     (SPI2_BASE + 0x100)  // DMA configuration
#define SPI_CLK_GATE_REG(i)     (SPI2_BASE + 0x0E8)  // CRITICAL - was missing!

// System/GPIO registers
#define SYSTEM_PERIP_CLK_EN0_REG    0x600C0018U
#define SYSTEM_PERIP_RST_EN0_REG    0x600C001CU
#define IO_MUX_BASE                 0x60009000U
#define GPIO_FUNC0_OUT_SEL_CFG_BASE 0x60004554U
#define GPIO_ENABLE_REG             0x60004020U
#define GPIO_FUNC0_IN_SEL_CFG_BASE  0x60004154U

// System register bits
#define SYSTEM_SPI2_CLK_EN      (1 << 6)
#define SYSTEM_SPI2_RST         (1 << 6)

// SPI CMD register bits
#define SPI_USR                 (1 << 24)
#define SPI_UPDATE              (1 << 23)

// SPI USER register bits
#define SPI_USR_COMMAND         (1 << 31)
#define SPI_USR_ADDR            (1 << 30)
#define SPI_USR_DUMMY           (1 << 29)
#define SPI_USR_MISO            (1 << 28)
#define SPI_USR_MOSI            (1 << 27)
#define SPI_CS_SETUP            (1 << 7)
#define SPI_CS_HOLD             (1 << 6)

// SPI CLK_GATE register bits (CRITICAL - was missing!)
#define SPI_CLK_GATE_MST_CLK_ACTIVE (1 << 0)
#define SPI_CLK_GATE_MST_CLK_SEL    (1 << 1)

// GPIO function codes for SPI2
#define FUNC_SPI2_D         4
#define FUNC_SPI2_Q         4
#define FUNC_SPI2_CLK       4
#define FUNC_SPI2_CS0       4
#define FUNC_SPI2_HD        4
#define FUNC_SPI2_WP        4

// Pin mappings (from PLAN.md)
#define PIN_MOSI 11
#define PIN_MISO 13
#define PIN_CLK  12
#define PIN_CS   10
#define PIN_WP   14
#define PIN_HD   9

// Helper macros
#define REG_WRITE(_r, _v) (*(volatile uint32_t *)(_r)) = (_v)
#define REG_READ(_r) (*(volatile uint32_t *)(_r))
#define REG_SET_BIT(_r, _b) REG_WRITE((_r), REG_READ(_r) | (uint32_t)(_b))
#define REG_CLR_BIT(_r, _b) REG_WRITE((_r), REG_READ(_r) & ~(uint32_t)(_b))

// External ROM delay function
extern void esp_rom_delay_us(uint32_t us);

/**
 * @brief Configure GPIO pin for SPI2 function
 */
static void configure_spi2_pin(uint8_t gpio_num, uint8_t spi_signal, bool is_output)
{
    // Set IO_MUX function to SPI2 (function 4) and enable function
    uint32_t io_mux_reg = IO_MUX_BASE + 0x04U + ((uint32_t)gpio_num * 4U);
    uint32_t mux_val = REG_READ(io_mux_reg);
    mux_val &= ~(uint32_t)(0x7U << 12);  // Clear MCU_SEL bits
    mux_val |= (uint32_t)(FUNC_SPI2_D << 12);  // Set to SPI2 function
    mux_val |= (1U << 9);  // FUN_IE (Input Enable)
    mux_val &= ~(1U << 8);  // FUN_WPU (disable pullup for now)
    mux_val &= ~(1U << 7);  // FUN_WPD (disable pulldown)
    REG_WRITE(io_mux_reg, mux_val);

    // Configure GPIO matrix
    if (is_output) {
        // Output: GPIO matrix output routing
        if (spi_signal != 0xFF) {
            uint32_t gpio_func_reg = GPIO_FUNC0_OUT_SEL_CFG_BASE + ((uint32_t)gpio_num * 4U);
            REG_WRITE(gpio_func_reg, spi_signal);
        }
        // Enable GPIO output
        REG_SET_BIT(GPIO_ENABLE_REG, (1U << gpio_num));
    } else {
        // Input: GPIO matrix input routing
        uint32_t gpio_in_reg = GPIO_FUNC0_IN_SEL_CFG_BASE + ((uint32_t)spi_signal * 4U);
        REG_WRITE(gpio_in_reg, (1U << 7) | gpio_num);  // SIG_IN_SEL=1, FUNC_SEL=gpio_num
    }
}

int spi2_init(void)
{
    // Enable SPI2 peripheral clock
    REG_SET_BIT(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_SPI2_CLK_EN);

    // Release SPI2 from reset
    REG_CLR_BIT(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_SPI2_RST);

    // CRITICAL: Enable master clock gate (this was missing!)
    // Without this, the SPI master clock is not active at all
    REG_WRITE(SPI_CLK_GATE_REG(2), SPI_CLK_GATE_MST_CLK_ACTIVE | SPI_CLK_GATE_MST_CLK_SEL);

    // Clear all registers first (like ESP-IDF does)
    // This prevents stale configuration bits
    REG_WRITE(SPI_USER_REG(2), 0);
    REG_WRITE(SPI_CTRL_REG(2), 0);
    REG_WRITE(SPI_CLOCK_REG(2), 0);
    REG_WRITE(SPI_USER1_REG(2), 0);
    REG_WRITE(SPI_USER2_REG(2), 0);
    REG_WRITE(SPI_MISC_REG(2), 0);

    // Configure DMA (even though we're not using it, per ESP-IDF)
    // Clear DMA_CONF, then set segment transaction clear enables
    REG_WRITE(SPI_DMA_CONF_REG(2), 0);
    uint32_t dma_conf_val = (1 << 19) | (1 << 20);  // tx_seg_trans_clr_en | rx_seg_trans_clr_en
    REG_WRITE(SPI_DMA_CONF_REG(2), dma_conf_val);

    // Configure pins
    // SPI2 signal numbers (ESP32-S3):
    // SPIQ (MISO) = 9, SPID (MOSI) = 8, SPICLK = 7, SPICS0 = 6
    // SPIHD = 10, SPIWP = 11
    configure_spi2_pin(PIN_MISO, 9, false);  // MISO - input
    configure_spi2_pin(PIN_MOSI, 8, true);   // MOSI - output
    configure_spi2_pin(PIN_CLK, 7, true);    // CLK - output
    configure_spi2_pin(PIN_CS, 6, true);     // CS0 - output
    configure_spi2_pin(PIN_WP, 11, true);    // WP - output
    configure_spi2_pin(PIN_HD, 10, true);    // HD - output

    // Configure SPI2 clock: Start with 10MHz for more reliable operation
    // APB clock = 80 MHz, divide by 8 for 10 MHz
    // clkcnt_n = 7, clkcnt_h = 3, clkcnt_l = 3
    uint32_t clock_val = (3 << 0) |   // clkcnt_l = 3
                         (3 << 6) |   // clkcnt_h = 3
                         (7 << 12);   // clkcnt_n = 7 (80MHz / 8 = 10MHz)
    REG_WRITE(SPI_CLOCK_REG(2), clock_val);

    // Configure MISC register
    uint32_t misc_val = 0;
    misc_val &= ~(uint32_t)(1U << 7);  // master_cs_pol[0] = 0 (CS active low)
    misc_val &= ~(uint32_t)(1U << 30); // ck_idle_edge = 0 (CLK low when idle)
    REG_WRITE(SPI_MISC_REG(2), misc_val);

    // Configure USER register
    uint32_t user_val = SPI_CS_SETUP | SPI_CS_HOLD;
    REG_WRITE(SPI_USER_REG(2), user_val);

    // Configure timing (cs_setup=1, cs_hold=1)
    REG_WRITE(SPI_USER1_REG(2), (1 << 21) | (1 << 16));

    return 0;
}

int spi2_transaction(uint8_t cmd,
                     const uint8_t *addr, uint8_t addr_bits,
                     const uint8_t *tx_data, uint16_t tx_bits,
                     uint8_t *rx_data, uint16_t rx_bits)
{
    // Wait for any previous transaction to complete
    while (REG_READ(SPI_CMD_REG(2)) & SPI_USR) {
        esp_rom_delay_us(1);
    }

    // Configure USER register based on transaction type
    uint32_t user_val = SPI_CS_SETUP | SPI_CS_HOLD;

    // Always send command
    user_val |= (uint32_t)SPI_USR_COMMAND;

    // Configure address phase
    if (addr_bits > 0) {
        user_val |= SPI_USR_ADDR;
    }

    // Configure TX phase
    if (tx_bits > 0) {
        user_val |= SPI_USR_MOSI;
    }

    // Configure RX phase
    if (rx_bits > 0) {
        user_val |= SPI_USR_MISO;
    }

    REG_WRITE(SPI_USER_REG(2), user_val);

    // Set command
    uint32_t user2_val = (cmd << 0) | ((8 - 1) << 28); // command is 8 bits
    REG_WRITE(SPI_USER2_REG(2), user2_val);

    // Set address if present
    if (addr_bits > 0) {
        uint32_t addr_val = 0;
        for (int i = 0; i < (int)((addr_bits + 7) / 8); i++) {
            addr_val = (addr_val << 8) | addr[i];
        }
        REG_WRITE(SPI_ADDR_REG(2), addr_val);

        uint32_t user1_val = REG_READ(SPI_USER1_REG(2));
        user1_val &= ~(uint32_t)(0x1F << 0);  // Clear addr_bitlen
        user1_val |= (uint32_t)((addr_bits - 1) << 0);  // Set addr_bitlen
        REG_WRITE(SPI_USER1_REG(2), user1_val);
    }

    // Set TX data if present
    if (tx_bits > 0) {
        uint32_t tx_bytes = (uint32_t)((tx_bits + 7) / 8);
        for (uint32_t i = 0; i < (tx_bytes + 3) / 4; i++) {
            uint32_t word = 0;
            for (uint32_t j = 0; j < 4 && (i * 4 + j) < tx_bytes; j++) {
                word |= ((uint32_t)tx_data[i * 4 + j]) << (j * 8);
            }
            REG_WRITE(SPI_W0_REG(2) + (i * 4), word);
        }
        REG_WRITE(SPI_MS_DLEN_REG(2), (uint32_t)(tx_bits - 1));
    }

    // Set RX length if receiving
    if (rx_bits > 0 && tx_bits == 0) {
        REG_WRITE(SPI_MS_DLEN_REG(2), (uint32_t)(rx_bits - 1));
    }

    // Start transaction
    REG_WRITE(SPI_CMD_REG(2), SPI_UPDATE);
    while (REG_READ(SPI_CMD_REG(2)) & SPI_UPDATE) {
        esp_rom_delay_us(1);
    }

    REG_WRITE(SPI_CMD_REG(2), SPI_USR);

    // Wait for completion
    while (REG_READ(SPI_CMD_REG(2)) & SPI_USR) {
        esp_rom_delay_us(1);
    }

    // Read RX data if expected
    if (rx_bits > 0 && rx_data != NULL) {
        uint32_t rx_bytes = (uint32_t)((rx_bits + 7) / 8);
        for (uint32_t i = 0; i < (rx_bytes + 3) / 4; i++) {
            uint32_t word = REG_READ(SPI_W0_REG(2) + (i * 4));
            for (uint32_t j = 0; j < 4 && (i * 4 + j) < rx_bytes; j++) {
                rx_data[i * 4 + j] = (uint8_t)((word >> (j * 8)) & 0xFF);
            }
        }
    }

    return 0;
}
