/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

/**
 * Host unit tests for esp-stub-lib/src/nand.c.
 *
 * The target bus layer (stub_target_nand_bus_*) is replaced by a fake SPI NAND chip
 * that decodes the commands nand.c sends. Each test configures the chip, attaches,
 * and checks the result and what the chip saw.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include <esp-stub-lib/err.h>
#include <esp-stub-lib/nand.h>
#include <target/nand.h>

/* ---- Fake SPI NAND chip ---------------------------------------------------- */

#define FAKE_PAGE_SIZE     2048U
#define FAKE_SPARE_SIZE    64U
#define FAKE_RAW_SIZE      (FAKE_PAGE_SIZE + FAKE_SPARE_SIZE)
#define FAKE_PAGES         (8U * 64U) /* Pages backed by memory. Others read as erased. */
#define FAKE_COLUMN_MASK   0x0FFFU

#define CMD_SET_REGISTER   0x1F
#define CMD_READ_REGISTER  0x0F
#define CMD_WRITE_ENABLE   0x06
#define CMD_PAGE_READ      0x13
#define CMD_PROGRAM_EXEC   0x10
#define CMD_PROGRAM_LOAD   0x02
#define CMD_PROGRAM_RANDOM 0x84
#define CMD_READ_CACHE     0x03
#define CMD_ERASE_BLOCK    0xD8
#define CMD_READ_ID        0x9F
#define CMD_RESET          0xFF

#define CFG_BUF            0x08
#define CFG_ECC_EN         0x10
#define CFG_OTP_EN         0x40

static struct {
    uint8_t manufacturer_id;
    uint16_t device_id;
    bool buf_reserved;        /* Micron: CONFIG bit 3 reads back as 0 */
    bool has_parameter_page;
    bool prot_stuck;          /* PROTECT ignores writes */
    uint8_t ecc_status;       /* STATUS ECC bits reported after a normal PAGE_READ */
    int bus_init_result;
    uint8_t prot;
    uint8_t cfg;
    uint8_t status;
    uint8_t cache[FAKE_RAW_SIZE];
    uint8_t otp_page1[FAKE_RAW_SIZE];
    uint8_t array[FAKE_PAGES][FAKE_RAW_SIZE];
    /* Observations */
    stub_lib_nand_pins_t init_pins;
    int otp_page_reads;
    uint8_t cfg_at_otp_read;
    uint32_t last_read_column;
    uint32_t last_load_column;
} s_chip;

static uint16_t ref_onfi_crc(const uint8_t *p, size_t len)
{
    uint16_t crc = 0x4F4E;
    while (len--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0));
        }
    }
    return crc;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Fill the three parameter page copies of the OTP page. */
static void fake_set_parameter_page(uint32_t page_size, uint32_t pages_per_block, uint32_t blocks_per_lun,
                                    uint8_t luns)
{
    uint8_t copy[256] = { 0 };
    memcpy(copy, "ONFI", 4);
    put_le32(&copy[80], page_size);
    put_le32(&copy[92], pages_per_block);
    put_le32(&copy[96], blocks_per_lun);
    copy[100] = luns;
    uint16_t crc = ref_onfi_crc(copy, 254);
    copy[254] = (uint8_t)crc;
    copy[255] = (uint8_t)(crc >> 8);
    for (int i = 0; i < 3; i++) {
        memcpy(&s_chip.otp_page1[i * 256], copy, sizeof(copy));
    }
    s_chip.has_parameter_page = true;
}

static void fake_chip_reset(uint8_t manufacturer_id, uint16_t device_id)
{
    memset(&s_chip, 0, sizeof(s_chip));
    memset(s_chip.array, 0xFF, sizeof(s_chip.array));
    memset(s_chip.otp_page1, 0xFF, sizeof(s_chip.otp_page1));
    s_chip.manufacturer_id = manufacturer_id;
    s_chip.device_id = device_id;
    s_chip.buf_reserved = manufacturer_id == 0x2C;
    s_chip.prot = 0x7C; /* Power-on: all blocks locked */
}

static uint8_t *fake_array_page(uint32_t row)
{
    return row < FAKE_PAGES ? s_chip.array[row] : NULL;
}

void stub_lib_delay_us(uint32_t us)
{
    (void)us;
}

int stub_target_nand_bus_init(const stub_lib_nand_pins_t *pins)
{
    s_chip.init_pins = *pins;
    return s_chip.bus_init_result;
}

int stub_target_nand_bus_transaction(uint8_t cmd,
                                     uint32_t addr,
                                     uint8_t addr_bits,
                                     const uint8_t *tx_data,
                                     uint16_t tx_bits,
                                     uint8_t *rx_data,
                                     uint16_t rx_bits)
{
    uint32_t tx_len = tx_bits / 8U;
    uint32_t rx_len = rx_bits / 8U;
    (void)addr_bits;

    switch (cmd) {
    case CMD_RESET:
        s_chip.status = 0;
        s_chip.cfg &= (uint8_t)~CFG_OTP_EN;
        break;
    case CMD_READ_ID:
        rx_data[0] = s_chip.manufacturer_id;
        rx_data[1] = (uint8_t)(s_chip.device_id >> 8);
        rx_data[2] = (uint8_t)s_chip.device_id;
        break;
    case CMD_READ_REGISTER:
        rx_data[0] = addr == NAND_REG_PROTECT ? s_chip.prot : addr == NAND_REG_CONFIG ? s_chip.cfg : s_chip.status;
        break;
    case CMD_SET_REGISTER:
        if (tx_data[0] == NAND_REG_PROTECT) {
            if (!s_chip.prot_stuck) {
                s_chip.prot = tx_data[1];
            }
        } else if (tx_data[0] == NAND_REG_CONFIG) {
            s_chip.cfg = s_chip.buf_reserved ? (uint8_t)(tx_data[1] & ~CFG_BUF) : tx_data[1];
        }
        break;
    case CMD_WRITE_ENABLE:
        s_chip.status |= 0x02;
        break;
    case CMD_PAGE_READ:
        if (s_chip.cfg & CFG_OTP_EN) {
            s_chip.otp_page_reads++;
            s_chip.cfg_at_otp_read = s_chip.cfg;
            memcpy(s_chip.cache, addr == 1 ? s_chip.otp_page1 : s_chip.array[0], FAKE_RAW_SIZE);
            if (addr == 1 && !s_chip.has_parameter_page) {
                memset(s_chip.cache, 0xFF, FAKE_RAW_SIZE);
            }
        } else {
            uint8_t *page = fake_array_page(addr);
            if (page != NULL) {
                memcpy(s_chip.cache, page, FAKE_RAW_SIZE);
            } else {
                memset(s_chip.cache, 0xFF, FAKE_RAW_SIZE);
            }
            s_chip.status = (uint8_t)((s_chip.status & 0x0F) | s_chip.ecc_status);
        }
        break;
    case CMD_READ_CACHE: {
        uint32_t column = addr >> 8;
        s_chip.last_read_column = column;
        memcpy(rx_data, &s_chip.cache[column & FAKE_COLUMN_MASK], rx_len);
        break;
    }
    case CMD_PROGRAM_LOAD:
        memset(s_chip.cache, 0xFF, FAKE_RAW_SIZE);
    /* fall through */
    case CMD_PROGRAM_RANDOM:
        s_chip.last_load_column = addr;
        memcpy(&s_chip.cache[addr & FAKE_COLUMN_MASK], tx_data, tx_len);
        break;
    case CMD_PROGRAM_EXEC: {
        uint8_t *page = fake_array_page(addr);
        if (page != NULL) {
            for (uint32_t i = 0; i < FAKE_RAW_SIZE; i++) {
                page[i] &= s_chip.cache[i];
            }
        }
        s_chip.status &= (uint8_t)~0x02;
        break;
    }
    case CMD_ERASE_BLOCK:
        for (uint32_t row = addr & ~63U; row < (addr & ~63U) + 64U; row++) {
            uint8_t *page = fake_array_page(row);
            if (page != NULL) {
                memset(page, 0xFF, FAKE_RAW_SIZE);
            }
        }
        s_chip.status &= (uint8_t)~0x02;
        break;
    default:
        TEST_FAIL_MESSAGE("unexpected NAND command");
    }
    return 0;
}

/* ---- Helpers ---------------------------------------------------------------- */

static const stub_lib_nand_pins_t s_default_pins = {
    .clk = STUB_LIB_NAND_PIN_DEFAULT, .q = STUB_LIB_NAND_PIN_DEFAULT, .d = STUB_LIB_NAND_PIN_DEFAULT,
    .cs = STUB_LIB_NAND_PIN_DEFAULT, .hd = STUB_LIB_NAND_PIN_DEFAULT, .wp = STUB_LIB_NAND_PIN_DEFAULT,
};

static void attach_ok(stub_lib_nand_geometry_t *geometry)
{
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_attach(&s_default_pins, geometry));
}

void setUp(void)
{
    fake_chip_reset(0xEF, 0xAA21);
}

void tearDown(void)
{
}

/* ---- Attach ------------------------------------------------------------------ */

void test_not_initialized_after_failed_attach(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };
    uint8_t buf[16];

    s_chip.bus_init_result = NAND_ERR_PIN_INVALID;
    TEST_ASSERT_EQUAL_INT(NAND_ERR_PIN_INVALID, stub_lib_nand_attach(&s_default_pins, &geometry));
    TEST_ASSERT_EQUAL_INT(NAND_ERR_NOT_INITIALIZED, stub_lib_nand_read_page(0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, stub_lib_nand_get_page_size());
}

void test_attach_passes_pins_to_bus(void)
{
    const stub_lib_nand_pins_t pins = { .clk = 20, .q = 22, .d = 6, .cs = 21, .hd = 4, .wp = 5 };
    stub_lib_nand_geometry_t geometry = { 0 };

    fake_set_parameter_page(2048, 64, 1024, 1);
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_attach(&pins, &geometry));
    TEST_ASSERT_EQUAL_MEMORY(&pins, &s_chip.init_pins, sizeof(pins));
}

void test_attach_reads_geometry_from_parameter_page(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };

    fake_set_parameter_page(2048, 64, 1024, 1);
    attach_ok(&geometry);

    TEST_ASSERT_EQUAL_UINT32(2048, geometry.page_size);
    TEST_ASSERT_EQUAL_UINT32(64, geometry.pages_per_block);
    TEST_ASSERT_EQUAL_UINT32(1024, geometry.block_count);
    TEST_ASSERT_EQUAL_UINT32(2048, stub_lib_nand_get_page_size());
    TEST_ASSERT_EQUAL_UINT32(64, stub_lib_nand_get_pages_per_block());
    TEST_ASSERT_EQUAL_UINT32(0x20000, stub_lib_nand_get_block_size());
    TEST_ASSERT_EQUAL_UINT32(1024, stub_lib_nand_get_block_count());
    /* Parameter page read in OTP mode with ECC off and buffer mode kept */
    TEST_ASSERT_EQUAL_INT(1, s_chip.otp_page_reads);
    TEST_ASSERT_EQUAL_HEX8(CFG_OTP_EN | CFG_BUF, s_chip.cfg_at_otp_read);
    /* Left in normal mode with ECC and buffer mode on, blocks unlocked */
    TEST_ASSERT_EQUAL_HEX8(CFG_ECC_EN | CFG_BUF, s_chip.cfg);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_chip.prot);
}

void test_attach_multiplies_blocks_by_luns(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };

    fake_set_parameter_page(2048, 64, 1024, 2);
    attach_ok(&geometry);

    TEST_ASSERT_EQUAL_UINT32(2048, geometry.block_count);
}

void test_attach_uses_redundant_copy_when_first_is_corrupt(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };

    fake_set_parameter_page(2048, 64, 2048, 1);
    s_chip.otp_page1[80] ^= 0x01; /* Breaks the CRC of copy 0 */
    attach_ok(&geometry);

    TEST_ASSERT_EQUAL_UINT32(2048, geometry.page_size);
    TEST_ASSERT_EQUAL_UINT32(2048, geometry.block_count);
}

void test_attach_overrides_take_precedence(void)
{
    stub_lib_nand_geometry_t geometry = { .page_size = 0, .pages_per_block = 0, .block_count = 512 };

    fake_set_parameter_page(2048, 64, 1024, 1);
    attach_ok(&geometry);

    TEST_ASSERT_EQUAL_UINT32(2048, geometry.page_size);
    TEST_ASSERT_EQUAL_UINT32(64, geometry.pages_per_block);
    TEST_ASSERT_EQUAL_UINT32(512, geometry.block_count);
}

void test_attach_without_parameter_page_needs_overrides(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };

    TEST_ASSERT_EQUAL_INT(NAND_ERR_GEOMETRY_UNKNOWN, stub_lib_nand_attach(&s_default_pins, &geometry));
    TEST_ASSERT_EQUAL_HEX8(0, s_chip.cfg & CFG_OTP_EN);

    fake_chip_reset(0xEF, 0xAA21);
    geometry = (stub_lib_nand_geometry_t) {
        .page_size = 2048, .pages_per_block = 64, .block_count = 1024
    };
    attach_ok(&geometry);
    TEST_ASSERT_EQUAL_INT(0, s_chip.otp_page_reads);
}

void test_attach_rejects_unsupported_geometry(void)
{
    stub_lib_nand_geometry_t geometry = { .page_size = 1024, .pages_per_block = 64, .block_count = 1024 };
    TEST_ASSERT_EQUAL_INT(STUB_LIB_ERR_INVALID_ARG, stub_lib_nand_attach(&s_default_pins, &geometry));

    /* One block more than a 24-bit row address reaches */
    geometry = (stub_lib_nand_geometry_t) {
        .page_size = 2048, .pages_per_block = 256, .block_count = 0x10001
    };
    TEST_ASSERT_EQUAL_INT(STUB_LIB_ERR_INVALID_ARG, stub_lib_nand_attach(&s_default_pins, &geometry));
}

void test_attach_fails_when_protection_stays_set(void)
{
    stub_lib_nand_geometry_t geometry = { .page_size = 2048, .pages_per_block = 64, .block_count = 1024 };

    s_chip.prot_stuck = true;
    TEST_ASSERT_EQUAL_INT(NAND_ERR_PROTECTION, stub_lib_nand_attach(&s_default_pins, &geometry));
}

/* ---- Page access ------------------------------------------------------------- */

static void fill_pattern(uint8_t *buf, uint32_t len, uint8_t seed)
{
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(seed + i);
    }
}

static void write_and_read_back(uint32_t page)
{
    static uint8_t out[FAKE_PAGE_SIZE];
    static uint8_t in[FAKE_PAGE_SIZE];

    fill_pattern(out, sizeof(out), (uint8_t)page);
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_erase_block(page - (page % 64)));
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_write_page(page, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_read_page(page, in, sizeof(in)));
    TEST_ASSERT_EQUAL_MEMORY(out, in, sizeof(out));
}

void test_winbond_odd_block_selects_plane_with_ca15(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };
    fake_set_parameter_page(2048, 64, 1024, 1);
    attach_ok(&geometry);

    write_and_read_back(64 + 3);
    TEST_ASSERT_EQUAL_HEX32(0x8000, s_chip.last_read_column & 0xF000);
    TEST_ASSERT_EQUAL_HEX32(0x8000, s_chip.last_load_column & 0xF000);

    write_and_read_back(128 + 3);
    TEST_ASSERT_EQUAL_HEX32(0x0000, s_chip.last_read_column & 0xF000);
}

void test_micron_odd_block_selects_plane_with_ca12(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };
    fake_chip_reset(0x2C, 0x242C);
    fake_set_parameter_page(2048, 64, 2048, 1);
    attach_ok(&geometry);

    TEST_ASSERT_EQUAL_HEX8(CFG_ECC_EN, s_chip.cfg);
    write_and_read_back(64 + 3);
    TEST_ASSERT_EQUAL_HEX32(0x1000, s_chip.last_read_column & 0xF000);
    TEST_ASSERT_EQUAL_HEX32(0x1000, s_chip.last_load_column & 0xF000);
}

void test_read_page_reports_uncorrectable_ecc(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };
    uint8_t buf[16];

    fake_set_parameter_page(2048, 64, 1024, 1);
    attach_ok(&geometry);
    s_chip.ecc_status = 0x10; /* corrected */
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_read_page(0, buf, sizeof(buf)));
    s_chip.ecc_status = 0x20; /* uncorrectable */
    TEST_ASSERT_EQUAL_INT(NAND_ERR_ECC_UNCORRECTABLE, stub_lib_nand_read_page(0, buf, sizeof(buf)));
}

void test_micron_ecc_status_encoding(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };
    uint8_t buf[16];

    fake_chip_reset(0x2C, 0x242C);
    fake_set_parameter_page(2048, 64, 2048, 1);
    attach_ok(&geometry);

    s_chip.ecc_status = 0x30; /* 011: 4-6 bits corrected */
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_read_page(0, buf, sizeof(buf)));
    s_chip.ecc_status = 0x50; /* 101: 7-8 bits corrected */
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_read_page(0, buf, sizeof(buf)));
    s_chip.ecc_status = 0x20; /* 010: uncorrectable */
    TEST_ASSERT_EQUAL_INT(NAND_ERR_ECC_UNCORRECTABLE, stub_lib_nand_read_page(0, buf, sizeof(buf)));
}

void test_page_checks(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };
    uint8_t buf[16];

    fake_set_parameter_page(2048, 64, 1024, 1);
    attach_ok(&geometry);

    TEST_ASSERT_EQUAL_INT(STUB_LIB_ERR_INVALID_ARG, stub_lib_nand_read_page(1024 * 64, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_read_page(1024 * 64 - 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(STUB_LIB_ERR_INVALID_ARG, stub_lib_nand_erase_block(1));
    TEST_ASSERT_EQUAL_INT(STUB_LIB_ERR_INVALID_ARG, stub_lib_nand_read_page(0, NULL, 16));
}

void test_bad_block_marker_round_trip(void)
{
    stub_lib_nand_geometry_t geometry = { 0 };
    uint32_t spare;

    fake_set_parameter_page(2048, 64, 1024, 1);
    attach_ok(&geometry);

    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_read_bbm(64, (uint8_t *)&spare));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, spare);
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_write_bbm(64, true));
    TEST_ASSERT_EQUAL_HEX32(0x8000 | FAKE_PAGE_SIZE, s_chip.last_load_column);
    TEST_ASSERT_EQUAL_INT(0, stub_lib_nand_read_bbm(64, (uint8_t *)&spare));
    TEST_ASSERT_EQUAL_HEX32(0x00000000, spare);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_not_initialized_after_failed_attach);
    RUN_TEST(test_attach_passes_pins_to_bus);
    RUN_TEST(test_attach_reads_geometry_from_parameter_page);
    RUN_TEST(test_attach_multiplies_blocks_by_luns);
    RUN_TEST(test_attach_uses_redundant_copy_when_first_is_corrupt);
    RUN_TEST(test_attach_overrides_take_precedence);
    RUN_TEST(test_attach_without_parameter_page_needs_overrides);
    RUN_TEST(test_attach_rejects_unsupported_geometry);
    RUN_TEST(test_attach_fails_when_protection_stays_set);
    RUN_TEST(test_winbond_odd_block_selects_plane_with_ca15);
    RUN_TEST(test_micron_odd_block_selects_plane_with_ca12);
    RUN_TEST(test_read_page_reports_uncorrectable_ecc);
    RUN_TEST(test_micron_ecc_status_encoding);
    RUN_TEST(test_page_checks);
    RUN_TEST(test_bad_block_marker_round_trip);
    return UNITY_END();
}
