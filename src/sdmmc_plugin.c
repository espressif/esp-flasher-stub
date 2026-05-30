/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * SDMMC card plugin — compiled as a separate binary loaded at runtime by
 * esptool.  Bridges the stub's plugin opcodes (0xDF..0xE5) to the per-target
 * SDMMC HAL.  Streaming callbacks use ctx->transport so the plugin is
 * independent of the host transport (UART/USB/SDIO).
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <esp-stub-lib/bit_utils.h>
#include <esp-stub-lib/md5.h>
#include <target/sdmmc.h>
#include "commands.h"
#include "command_handler.h"
#include "endian_utils.h"
#include "plugin_table.h"
#include "transport.h"

#define SECTOR_SIZE      SDMMC_SECTOR_SIZE
#define MD5_DIGEST_SIZE  16U
// Upper bound for per-packet host transfer; bounds send_buf in the streaming
// read post-process.  4 KB matches esptool's FLASH_SECTOR_SIZE.
#define MAX_PACKET_SIZE  (SECTOR_SIZE * 8U)
#define CHECKSUM_INIT    0xEFU
// Host flow control: one in-flight packet at a time.  Raising this would
// increase throughput at the cost of a larger host reassembly buffer.
#define MAX_UNACKED_PACKETS  1U

// ---- Plugin BSS state --------------------------------------------------- //

// Scratch sector used by read fill / write flush.
static uint8_t s_sector_buf[SECTOR_SIZE] __attribute__((aligned(4)));

// Write session — page_buf accumulates partial sectors until full.
static struct {
    uint32_t offset;
    uint32_t total_remaining;
    uint8_t  sector_buf[SECTOR_SIZE] __attribute__((aligned(4)));
    uint32_t sector_buf_filled;
    bool     in_progress;
} s_write_state;

// Read session — params stashed by sdmmc_plugin_read_flash for the post-process.
static struct {
    uint32_t offset;
    uint32_t read_size;
    uint32_t packet_size;
} s_read_state;

// ---- Helpers ------------------------------------------------------------ //

static int sdmmc_err_to_response(int err)
{
    if (err == 0) {
        return RESPONSE_SUCCESS;
    }
    // Reuse NAND program-failed for chip-side data errors; everything else
    // surfaces as the generic SPI_OP code with the SDMMC_ERR_* code packed in
    // resp->value (decoded by the host).
    if (err == SDMMC_ERR_DATA_FAIL) {
        return RESPONSE_NAND_PROGRAM_FAILED;
    }
    if (err == SDMMC_ERR_BAD_ARG) {
        return RESPONSE_BAD_DATA_LEN;
    }
    return RESPONSE_FAILED_SPI_OP;
}

// Pack {stage, err, rintsts} so the host can decode which init step died and
// what controller-interrupt bits looked like at that moment.  Layout:
//   bits[31:24] = stage (SDMMC_DIAG_STAGE_* or SD CMD index)
//   bits[23:16] = (int8_t)err — signed SDMMC_ERR_* code
//   bits[15:0]  = lower 16 bits of RINTSTS
static uint32_t pack_diag_value(int err)
{
    uint8_t  stage   = SDMMC_DIAG_STAGE_NONE;
    uint32_t rintsts = 0;
    stub_target_sdmmc_get_last_diag(&stage, &rintsts);
    return ((uint32_t)stage << 24)
           | ((uint32_t)((uint8_t)err) << 16)
           | (rintsts & 0xFFFFU);
}

// Fill one host send packet by issuing single-sector reads covering
// [current_offset, current_offset + size).
static int fill_send_packet(uint8_t *dst, uint32_t *current_offset, uint32_t size)
{
    uint32_t filled = 0;
    while (filled < size) {
        uint32_t lba        = *current_offset / SECTOR_SIZE;
        uint32_t sector_off = *current_offset % SECTOR_SIZE;
        uint32_t avail      = SECTOR_SIZE - sector_off;
        uint32_t need       = size - filled;
        uint32_t chunk      = (avail < need) ? avail : need;

        if (stub_target_sdmmc_read_sector(lba, s_sector_buf) != 0) {
            return -1;
        }
        memcpy(dst + filled, s_sector_buf + sector_off, chunk);
        filled          += chunk;
        *current_offset += chunk;
    }
    return 0;
}

static int flush_write_sector(void)
{
    uint32_t lba = s_write_state.offset / SECTOR_SIZE;
    int ret = stub_target_sdmmc_write_sector(lba, s_write_state.sector_buf);
    if (ret != 0) {
        return sdmmc_err_to_response(ret);
    }
    s_write_state.offset += SECTOR_SIZE;
    s_write_state.sector_buf_filled = 0;
    return RESPONSE_SUCCESS;
}

// ---- Streaming read post-process ---------------------------------------- //

// On read failure we continue sending zero-filled packets so the host's
// expected byte count is satisfied, then poison the final MD5 (XOR each byte
// with 0xFF) so verification fails on the host side.  Same convention as
// the NAND plugin.
static int read_flash_post_process(const struct cmd_ctx *ctx)
{
    uint32_t offset      = s_read_state.offset;
    uint32_t read_size   = s_read_state.read_size;
    uint32_t packet_size = s_read_state.packet_size;

    // In plugin BSS to avoid pushing it on the stack.
    static uint8_t send_buf[MAX_PACKET_SIZE] __attribute__((aligned(4)));

    ctx->transport->recv_release();

    uint32_t remaining      = read_size;
    uint32_t sent_packets   = 0;
    uint32_t acked_bytes    = 0;
    uint32_t acked_packets  = 0;
    uint32_t current_offset = offset;
    bool     read_failed    = false;

    struct stub_lib_md5_ctx md5;
    stub_lib_md5_init(&md5);

    while (remaining > 0 || acked_bytes < read_size) {
        size_t         slip_size;
        bool           ack_err;
        const uint8_t *slip_data = ctx->transport->recv_poll(&slip_size, &ack_err);
        if (ack_err) {
            ctx->transport->recv_release();
        } else if (slip_data != NULL) {
            if (slip_size == sizeof(acked_bytes)) {
                memcpy(&acked_bytes, slip_data, sizeof(acked_bytes));
                if (acked_packets < sent_packets) {
                    acked_packets++;
                }
            }
            ctx->transport->recv_release();
        }

        uint32_t unacked = (sent_packets >= acked_packets) ? (sent_packets - acked_packets) : 0;
        if (remaining > 0 && unacked < MAX_UNACKED_PACKETS) {
            uint32_t this_packet = (remaining < packet_size) ? remaining : packet_size;
            if (!read_failed) {
                if (fill_send_packet(send_buf, &current_offset, this_packet) != 0) {
                    read_failed = true;
                }
            }
            if (read_failed) {
                memset(send_buf, 0, this_packet);
            }

            stub_lib_md5_update(&md5, send_buf, this_packet);
            ctx->transport->send_frame(send_buf, this_packet);
            remaining -= this_packet;
            sent_packets++;
        }
    }

    uint8_t digest[MD5_DIGEST_SIZE];
    stub_lib_md5_final(&md5, digest);
    if (read_failed) {
        for (size_t i = 0; i < sizeof(digest); i++) {
            digest[i] ^= 0xFF;
        }
    }
    ctx->transport->send_frame(digest, sizeof(digest));
    return RESPONSE_SUCCESS;
}

// ---- Handlers ----------------------------------------------------------- //

int sdmmc_plugin_attach(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != SDMMC_ATTACH_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    // Mirror the 16-byte on-wire payload into the config struct.
    stub_target_sdmmc_attach_config_t cfg;
    cfg.slot     = data[0];
    cfg.width    = data[1];
    cfg.freq_khz = get_le_to_u16(data + 2);
    cfg.cd_pin   = data[4];
    cfg.wp_pin   = data[5];
    cfg.pin_clk  = data[6];
    cfg.pin_cmd  = data[7];
    for (uint32_t i = 0; i < 8; i++) {
        cfg.pin_d[i] = data[8 + i];
    }

    int err = stub_target_sdmmc_attach(&cfg);
    if (err != 0) {
        resp->value = pack_diag_value(err);
        return RESPONSE_FAILED_SPI_OP;
    }

    // Response: value = capacity in sectors; data carries OCR / RCA / flags.
    const stub_target_sdmmc_card_info_t *ci = stub_target_sdmmc_get_card_info();
    resp->value = (uint32_t)(ci->capacity_bytes / SECTOR_SIZE);
    set_u32_to_le(resp->data + 0, ci->ocr);
    set_u32_to_le(resp->data + 4, ci->rca);
    uint32_t flags = (ci->is_mmc ? 1U : 0U) | (ci->is_high_capacity ? 2U : 0U) | ((uint32_t)ci->width << 4);
    set_u32_to_le(resp->data + 8, flags);
    resp->data_size = 12;
    return RESPONSE_SUCCESS;
}

int sdmmc_plugin_read_flash(uint8_t command, const uint8_t *data, uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != SDMMC_READ_FLASH_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t offset      = get_le_to_u32(data + 0);
    uint32_t read_size   = get_le_to_u32(data + 4);
    uint32_t packet_size = get_le_to_u32(data + 8);
    // data + 12 = max_inflight (informational; the stub enforces window=1)
    if (packet_size == 0 || packet_size > MAX_PACKET_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    s_read_state.offset      = offset;
    s_read_state.read_size   = read_size;
    s_read_state.packet_size = packet_size;
    resp->post_process       = read_flash_post_process;
    return RESPONSE_SUCCESS;
}

int sdmmc_plugin_write_flash_begin(uint8_t command, const uint8_t *data, uint32_t len,
                                   struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len != SDMMC_WRITE_FLASH_BEGIN_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t offset     = get_le_to_u32(data + 0);
    uint32_t total_size = get_le_to_u32(data + 4);
    // CMD24 (WRITE_BLOCK) cannot do partial-sector programs.
    if ((offset % SECTOR_SIZE) != 0) {
        return RESPONSE_BAD_DATA_LEN;
    }

    s_write_state.offset            = offset;
    s_write_state.total_remaining   = total_size;
    s_write_state.sector_buf_filled = 0;
    s_write_state.in_progress       = true;
    return RESPONSE_SUCCESS;
}

int sdmmc_plugin_write_flash_data(uint8_t command, const uint8_t *data, uint32_t len,
                                  struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len < SDMMC_WRITE_FLASH_DATA_HEADER_SIZE) {
        return RESPONSE_NOT_ENOUGH_DATA;
    }
    if (!s_write_state.in_progress) {
        return RESPONSE_NOT_IN_FLASH_MODE;
    }

    uint32_t       data_len    = get_le_to_u32(data);
    const uint8_t *flash_data  = data + SDMMC_WRITE_FLASH_DATA_HEADER_SIZE;
    uint32_t       actual_size = len - SDMMC_WRITE_FLASH_DATA_HEADER_SIZE;
    if (data_len != actual_size) {
        return RESPONSE_TOO_MUCH_DATA;
    }
    if (actual_size > s_write_state.total_remaining) {
        return RESPONSE_TOO_MUCH_DATA;
    }

    // Same XOR checksum as base FLASH_DATA / NAND_WRITE_FLASH_DATA.  The
    // checksum word sits in the SLIP header at offset -4 relative to data.
    uint32_t chk = CHECKSUM_INIT;
    for (uint32_t i = 0; i < actual_size; i++) {
        chk ^= flash_data[i];
    }
    if (chk != get_le_to_u32(data - sizeof(uint32_t))) {
        return RESPONSE_BAD_DATA_CHECKSUM;
    }

    uint32_t       to_process = actual_size;
    const uint8_t *src        = flash_data;
    while (to_process > 0) {
        uint32_t space = SECTOR_SIZE - s_write_state.sector_buf_filled;
        uint32_t chunk = (to_process < space) ? to_process : space;
        memcpy(s_write_state.sector_buf + s_write_state.sector_buf_filled, src, chunk);
        s_write_state.sector_buf_filled += chunk;
        s_write_state.total_remaining   -= chunk;
        src        += chunk;
        to_process -= chunk;

        if (s_write_state.sector_buf_filled >= SECTOR_SIZE) {
            int r = flush_write_sector();
            if (r != RESPONSE_SUCCESS) {
                s_write_state.sector_buf_filled = 0;
                return r;
            }
        }
    }
    return RESPONSE_SUCCESS;
}

int sdmmc_plugin_write_flash_end(uint8_t command, const uint8_t *data, uint32_t len,
                                 struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len != SDMMC_WRITE_FLASH_END_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    if (!s_write_state.in_progress) {
        return RESPONSE_NOT_IN_FLASH_MODE;
    }
    if (s_write_state.total_remaining != 0) {
        return RESPONSE_BAD_DATA_LEN;
    }

    if (s_write_state.sector_buf_filled > 0) {
        // Pad partial trailing sector with 0xFF so we program a full block.
        memset(s_write_state.sector_buf + s_write_state.sector_buf_filled,
               0xFF, SECTOR_SIZE - s_write_state.sector_buf_filled);
        s_write_state.sector_buf_filled = SECTOR_SIZE;
        int r = flush_write_sector();
        if (r != RESPONSE_SUCCESS) {
            s_write_state.sector_buf_filled = 0;
            return r;
        }
    }
    (void)get_le_to_u32(data);  // reboot_flag — no chip-side reboot on SDMMC
    memset(&s_write_state, 0, sizeof(s_write_state));
    return RESPONSE_SUCCESS;
}

int sdmmc_plugin_erase_region(uint8_t command, const uint8_t *data, uint32_t len,
                              struct command_response_data *resp)
{
    (void)command;
    (void)resp;
    if (len != SDMMC_ERASE_REGION_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    uint32_t start = get_le_to_u32(data + 0);
    uint32_t size  = get_le_to_u32(data + 4);
    if ((start % SECTOR_SIZE) != 0 || (size % SECTOR_SIZE) != 0 || size == 0) {
        return RESPONSE_BAD_DATA_LEN;
    }
    uint32_t start_lba = start / SECTOR_SIZE;
    uint32_t end_lba   = start_lba + (size / SECTOR_SIZE) - 1U;
    return sdmmc_err_to_response(stub_target_sdmmc_erase_range(start_lba, end_lba));
}

int sdmmc_plugin_get_info(uint8_t command, const uint8_t *data, uint32_t len,
                          struct command_response_data *resp)
{
    (void)command;
    (void)data;
    if (len != SDMMC_GET_INFO_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }

    const stub_target_sdmmc_card_info_t *ci = stub_target_sdmmc_get_card_info();
    if (ci == NULL) {
        return RESPONSE_NOT_IN_FLASH_MODE;
    }

    // Same payload as attach + 16 bytes of CID for host-side identification.
    resp->value = (uint32_t)(ci->capacity_bytes / SECTOR_SIZE);
    set_u32_to_le(resp->data + 0, ci->ocr);
    set_u32_to_le(resp->data + 4, ci->rca);
    uint32_t flags = (ci->is_mmc ? 1U : 0U) | (ci->is_high_capacity ? 2U : 0U) | ((uint32_t)ci->width << 4);
    set_u32_to_le(resp->data + 8, flags);
    for (uint32_t i = 0; i < 4; i++) {
        set_u32_to_le(resp->data + 12 + i * 4, ci->cid[i]);
    }
    resp->data_size = 12 + 16;
    return RESPONSE_SUCCESS;
}
