/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * Key Manager plugin — compiled as a separate binary loaded at runtime by
 * esptool. Implements the seven Key Manager deploy/recovery sub-commands
 * (random, AES, ECDH0, ECDH1, key recovery, HUK deploy, HUK recovery)
 * that drive the Key Manager peripheral on ESP32-P4 (>= v3.0) and
 * ESP32-C5 (any revision).
 *
 * Wire protocol convention:
 *   - Each opcode receives a small payload (or empty) for the operation.
 *   - The handler returns RESPONSE_SUCCESS immediately if the operation
 *     started cleanly, then a post_process callback streams the result
 *     bytes back via slip_send_frame.
 *   - Handlers that need to stream data stash it in BSS (`s_km_state`)
 *     before returning, so the streaming happens after the success frame
 *     has reached the host.
 *
 * The host writes any persistent partition to flash itself (via the
 * existing FLASH_BEGIN/DATA/END opcodes) — the plugin does no flash I/O.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "commands.h"
#include "command_handler.h"
#include "plugin_table.h"
#include <target/key_mgr.h>

/* SLIP frame helpers — resolved to base stub via PROVIDE in the linker
 * script. Used by post_process callbacks to stream large responses. */
extern void slip_send_frame(const void *data, size_t size);

/* CRC32 from ROM. Used to compute huk_info / key_info checksums. */
extern uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);

/* ---------- Plugin BSS state -------------------------------------------- */
/*
 * All persistent state lives in BSS (zero-initialized; .data is forbidden
 * by the plugin linker script — it pulls in initialized data the host
 * doesn't upload).
 *
 * The HUK info buffer is 664 bytes (660 huk_info + 4 CRC32). The 660 size
 * matches IDF's HUK_INFO_LEN — the HUK Generator's MMIO `HUK_INFO_MEM`
 * window is only 384 bytes, but `esp_rom_km_huk_conf` reads/writes the
 * full 660 bytes. Sizing this any smaller silently corrupts neighboring
 * BSS on deploy and feeds truncated input on recovery. Aligned to 4
 * bytes because the KM memory blocks accept only 32-bit stores.
 */
#define STUB_KM_HUK_INFO_WIRE_SIZE  (STUB_KM_HUK_INFO_SIZE + sizeof(uint32_t))

/* Two key_info slots, each 64 bytes — covers single-stage (slot 0 only)
 * and multi-stage (XTS-AES-256, ECDSA-P-384) deploy / recovery flows. */
#define STUB_KM_KEY_INFO_TOTAL (2U * STUB_KM_KEY_RECOVERY_INFO_SIZE)

static struct {
    /* HUK info I/O buffer (input for huk_recovery, output for huk_deploy). */
    uint8_t  huk_info[STUB_KM_HUK_INFO_WIRE_SIZE] __attribute__((aligned(4)));
    /* Two key_info slots — first stage in [0..63], second in [64..127]. */
    uint8_t  key_info[STUB_KM_KEY_INFO_TOTAL] __attribute__((aligned(4)));
    /* k2*G output for ECDH0 (filled during ECDH0 deploy).  Two slots so the
     * multi-stage ECDH0 deploy (xts-aes-flash/psram-256, ecdsa-384) can
     * stream 128 bytes back to host; single-stage uses slot 0 only. */
    uint8_t  k2_g[2U * STUB_KM_ECDH0_INFO_SIZE] __attribute__((aligned(4)));
    /* Selects which response payload post_process should stream. */
    uint8_t  pending_kind;
    /* For deploys: how many key_info slots to stream (1 or 2). */
    uint8_t  pending_key_info_slots;
} s_km_state;

enum {
    KM_PENDING_NONE     = 0,
    KM_PENDING_HUK_INFO = 1,
    KM_PENDING_KEY_INFO = 2,            /* streams N * 64 bytes from key_info */
    KM_PENDING_ECDH0    = 3,            /* streams k2*G then key_info(s) */
};

/* ---------- post_process: stream the staged response -------------------- */

static int s_km_post_process(const struct cmd_ctx *ctx)
{
    (void)ctx;
    uint8_t kind = s_km_state.pending_kind;
    uint8_t slots = s_km_state.pending_key_info_slots;
    s_km_state.pending_kind = KM_PENDING_NONE;
    s_km_state.pending_key_info_slots = 0U;

    if (kind == KM_PENDING_HUK_INFO) {
        slip_send_frame(s_km_state.huk_info, STUB_KM_HUK_INFO_WIRE_SIZE);
    } else if (kind == KM_PENDING_ECDH0) {
        slip_send_frame(s_km_state.k2_g,
                        (size_t)slots * STUB_KM_ECDH0_INFO_SIZE);
    }
    if (kind == KM_PENDING_KEY_INFO || kind == KM_PENDING_ECDH0) {
        if (slots == 0U || slots > 2U) {
            slots = 1U;
        }
        slip_send_frame(s_km_state.key_info,
                        (size_t)slots * STUB_KM_KEY_RECOVERY_INFO_SIZE);
    }
    return RESPONSE_SUCCESS;
}

/* ---------- Key purpose / multi-stage helpers --------------------------- */

/* Map (key_type, key_len) to the *primary* KEYMNG_KEY_PURPOSE encoding.
 * For multi-stage types, the second stage uses the secondary purpose
 * returned by km_secondary_purpose(). Returns INVALID for unsupported
 * combinations — handlers must reject those up front.
 *
 * Implemented as an if/else chain instead of a switch because GCC turns
 * a 5-case switch into a jump table in `.rodata`, and the plugin linker
 * script forbids any data segments. */
static stub_km_key_purpose_t km_primary_purpose(stub_km_key_type_t kt,
                                                stub_km_key_len_t kl)
{
    if (kt == STUB_KM_KEY_TYPE_ECDSA) {
        if (kl == STUB_KM_KEY_LEN_ECDSA_192) {
            return STUB_KM_KEY_PURPOSE_ECDSA_192;
        }
        if (kl == STUB_KM_KEY_LEN_ECDSA_256) {
            return STUB_KM_KEY_PURPOSE_ECDSA_256;
        }
        if (kl == STUB_KM_KEY_LEN_ECDSA_384) {
            return STUB_KM_KEY_PURPOSE_ECDSA_384_H;
        }
    } else if (kt == STUB_KM_KEY_TYPE_FLASH_XTS_AES) {
        if (kl == STUB_KM_KEY_LEN_XTS_AES_128) {
            return STUB_KM_KEY_PURPOSE_FLASH_128;
        }
        if (kl == STUB_KM_KEY_LEN_XTS_AES_256) {
            return STUB_KM_KEY_PURPOSE_FLASH_256_1;
        }
    } else if (kt == STUB_KM_KEY_TYPE_PSRAM_XTS_AES) {
        if (kl == STUB_KM_KEY_LEN_XTS_AES_128) {
            return STUB_KM_KEY_PURPOSE_PSRAM_128;
        }
        if (kl == STUB_KM_KEY_LEN_XTS_AES_256) {
            return STUB_KM_KEY_PURPOSE_PSRAM_256_1;
        }
    } else if (kt == STUB_KM_KEY_TYPE_HMAC) {
        return STUB_KM_KEY_PURPOSE_HMAC;
    } else if (kt == STUB_KM_KEY_TYPE_DS) {
        return STUB_KM_KEY_PURPOSE_DS;
    }
    return STUB_KM_KEY_PURPOSE_INVALID;
}

static bool km_is_multi_stage(stub_km_key_purpose_t p)
{
    return p == STUB_KM_KEY_PURPOSE_FLASH_256_1
           || p == STUB_KM_KEY_PURPOSE_PSRAM_256_1
           || p == STUB_KM_KEY_PURPOSE_ECDSA_384_H;
}

static stub_km_key_purpose_t km_secondary_purpose(stub_km_key_purpose_t p)
{
    if (p == STUB_KM_KEY_PURPOSE_FLASH_256_1) {
        return STUB_KM_KEY_PURPOSE_FLASH_256_2;
    }
    if (p == STUB_KM_KEY_PURPOSE_PSRAM_256_1) {
        return STUB_KM_KEY_PURPOSE_PSRAM_256_2;
    }
    if (p == STUB_KM_KEY_PURPOSE_ECDSA_384_H) {
        return STUB_KM_KEY_PURPOSE_ECDSA_384_L;
    }
    return STUB_KM_KEY_PURPOSE_INVALID;
}

/* Match the IDF rule: index = is_multi_stage(current_purpose) ? 0 : 1.
 *
 *   - Single-stage (FLASH_128, ECDSA_192/256, HMAC, DS, PSRAM_128): slot 1.
 *   - Multi-stage primary (FLASH_256_1, PSRAM_256_1, ECDSA_384_H): slot 0.
 *   - Multi-stage secondary (FLASH_256_2, PSRAM_256_2, ECDSA_384_L): slot 1
 *     — these aren't multi-stage primaries, so is_multi_stage returns false.
 */
static uint32_t km_key_info_slot(stub_km_key_purpose_t p)
{
    /* Slot 1 ONLY for the multi-stage secondary purposes (FLASH_256_2,
     * PSRAM_256_2, ECDSA_384_L) — they are the second stage and feed the
     * upper 64 bytes of the 128-byte key_info wire payload.  Everything
     * else (single-stage AND multi-stage primary) writes slot 0. */
    if (p == STUB_KM_KEY_PURPOSE_FLASH_256_2
            || p == STUB_KM_KEY_PURPOSE_PSRAM_256_2
            || p == STUB_KM_KEY_PURPOSE_ECDSA_384_L) {
        return 1U;
    }
    return 0U;
}

/* ---------- Plugin handler implementations ------------------------------ */

/* Wire format common to all key_deploy_* opcodes:
 *   uint8_t  key_type     (stub_km_key_type_t)
 *   uint8_t  key_len      (stub_km_key_len_t)
 *   uint8_t  reserved[2]
 * followed by mode-specific payload (none for random).
 */
struct __attribute__((packed)) km_deploy_header {
    uint8_t key_type;
    uint8_t key_len;
    uint8_t reserved[2];
};

#define KM_DEPLOY_HEADER_SIZE 4U

/* Drive one stage of a deploy (random / AES / ECDH) — handles state-machine
 * sequencing and key_info readback. Mode-specific work happens via three
 * optional callbacks:
 *   - pre_start_cb: runs in IDLE, before KM_START. AES / ECDH1 set
 *     KEYMNG_USE_SW_INIT_KEY here when sw_init_key is in use.
 *   - load_cb: runs in LOAD phase, before KM_CONTINUE. Writes mode-
 *     specific inputs into the KM memory regions.
 *   - gain_cb: runs in GAIN phase, after the standard key_info readback.
 *     ECDH0 uses this to pull k2*G out of ASSIST_INFO_MEM.
 * Pass NULL for any callback that isn't needed.
 */
typedef int (*km_pre_start_cb_t)(stub_km_key_purpose_t purpose,
                                 const struct km_deploy_header *hdr,
                                 const uint8_t *data, uint32_t len);
typedef int (*km_load_cb_t)(stub_km_key_purpose_t purpose,
                            const struct km_deploy_header *hdr,
                            const uint8_t *data, uint32_t len);
typedef int (*km_gain_cb_t)(stub_km_key_purpose_t purpose);

static int km_run_one_stage(stub_km_keygen_mode_t mode,
                            stub_km_key_purpose_t purpose,
                            stub_km_key_type_t key_type,
                            stub_km_key_len_t key_len,
                            const struct km_deploy_header *hdr,
                            const uint8_t *data, uint32_t data_len,
                            km_pre_start_cb_t pre_start_cb,
                            km_load_cb_t load_cb,
                            km_gain_cb_t gain_cb)
{
    /* Phase 1 — IDLE: configure mode + purpose + (XTS only) key length,
     * plus any mode-specific static-register tweaks via pre_start_cb. */
    stub_target_km_wait_for_state(STUB_KM_STATE_IDLE);
    stub_target_km_set_keygen_mode(mode);
    stub_target_km_set_key_purpose(purpose);
    if (key_type == STUB_KM_KEY_TYPE_FLASH_XTS_AES
            || key_type == STUB_KM_KEY_TYPE_PSRAM_XTS_AES) {
        stub_target_km_set_xts_aes_key_len(
            key_type, key_len == STUB_KM_KEY_LEN_XTS_AES_256);
    }
    if (pre_start_cb != NULL) {
        int err = pre_start_cb(purpose, hdr, data, data_len);
        if (err != 0) {
            return err;
        }
    }
    stub_target_km_start();

    /* Phase 2 — LOAD: mode-specific data writes. */
    stub_target_km_wait_for_state(STUB_KM_STATE_LOAD);
    if (load_cb != NULL) {
        int err = load_cb(purpose, hdr, data, data_len);
        if (err != 0) {
            return err;
        }
    }
    stub_target_km_continue();

    /* Phase 3 — GAIN: read out key_info; some modes (ECDH0) also read k2*G. */
    stub_target_km_wait_for_state(STUB_KM_STATE_GAIN);
    uint32_t slot = km_key_info_slot(purpose);
    stub_target_km_read_public_info(
        s_km_state.key_info + slot * STUB_KM_KEY_RECOVERY_INFO_SIZE,
        STUB_KM_KEY_RECOVERY_INFO_SIZE);
    if (gain_cb != NULL) {
        int err = gain_cb(purpose);
        if (err != 0) {
            return err;
        }
    }

    /* Validation: KM reports per-key-type "deployment valid" once GAIN has
     * fully populated. Skipped on multi-stage primaries because the key is
     * not complete until both stages run. */
    if (!km_is_multi_stage(purpose)) {
        if (!stub_target_km_is_key_deployment_valid(key_type, key_len)) {
            return RESPONSE_INVALID_COMMAND;
        }
    }

    /* Phase 4 — POST → IDLE. */
    stub_target_km_continue();
    stub_target_km_wait_for_state(STUB_KM_STATE_IDLE);
    return RESPONSE_SUCCESS;
}

/* Drive a full deploy operation including the multi-stage second pass when
 * the chosen key_type/key_len pair maps to a multi-stage primary purpose.
 * Sets s_km_state.pending_kind / pending_key_info_slots for post_process
 * to stream the result back to host. */
static int km_run_deploy(stub_km_keygen_mode_t mode,
                         const struct km_deploy_header *hdr,
                         const uint8_t *data, uint32_t data_len,
                         km_pre_start_cb_t pre_start_cb,
                         km_load_cb_t load_cb, km_gain_cb_t gain_cb,
                         uint8_t pending_kind)
{
    if (!stub_target_km_is_supported()) {
        return RESPONSE_KM_UNSUPPORTED_CHIP;
    }

    stub_km_key_type_t key_type = (stub_km_key_type_t)hdr->key_type;
    stub_km_key_len_t  key_len  = (stub_km_key_len_t)hdr->key_len;

    stub_km_key_purpose_t purpose = km_primary_purpose(key_type, key_len);
    if (purpose == STUB_KM_KEY_PURPOSE_INVALID) {
        return RESPONSE_INVALID_COMMAND;
    }

    /* Bring up the KM peripheral once per command — idempotent. */
    stub_target_km_bringup();

    /* Each stub session is fresh — HUK lives in PUF SRAM and is lost across
     * resets unless EFUSE_KM_HUK_GEN_STATE is burned.  Generate a transient
     * HUK if none is present so key-deploy works on provisioning chips.  The
     * caller can still capture huk_info via the explicit `huk-deploy` op. */
    if (!stub_target_km_is_huk_valid()) {
        if (stub_target_huk_configure(STUB_HUK_MODE_GENERATE,
                                      s_km_state.huk_info) != 0) {
            return RESPONSE_INVALID_COMMAND;
        }
    }

    bool is_multi = km_is_multi_stage(purpose);

    int err = km_run_one_stage(mode, purpose, key_type, key_len, hdr,
                               data, data_len,
                               pre_start_cb, load_cb, gain_cb);
    if (err != 0) {
        return err;
    }

    if (is_multi) {
        stub_km_key_purpose_t secondary = km_secondary_purpose(purpose);
        err = km_run_one_stage(mode, secondary, key_type, key_len, hdr,
                               data, data_len,
                               pre_start_cb, load_cb, gain_cb);
        if (err != 0) {
            return err;
        }
    }

    /* Switch the static config to "use own key" so the downstream
     * peripheral picks up the KM-deployed key (rather than an eFuse one). */
    stub_target_km_set_key_usage(key_type, true);

    s_km_state.pending_kind = pending_kind;
    s_km_state.pending_key_info_slots = is_multi ? 2U : 1U;
    return RESPONSE_SUCCESS;
}

/* No load-phase data for random mode — KM auto-generates internally. */
static int km_load_random(stub_km_key_purpose_t purpose,
                          const struct km_deploy_header *hdr,
                          const uint8_t *data, uint32_t len)
{
    (void)purpose; (void)hdr; (void)data; (void)len;
    return 0;
}

int km_plugin_key_deploy_random(uint8_t command, const uint8_t *data,
                                uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != KM_DEPLOY_HEADER_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    const struct km_deploy_header *hdr = (const struct km_deploy_header *)data;
    int err = km_run_deploy(STUB_KM_KEYGEN_MODE_RANDOM, hdr, NULL, 0U,
                            NULL, km_load_random, NULL,
                            KM_PENDING_KEY_INFO);
    if (err != RESPONSE_SUCCESS) {
        return err;
    }
    resp->post_process = s_km_post_process;
    return RESPONSE_SUCCESS;
}

/* AES (advanced) deploy mode wire format (after the 4-byte header):
 *   uint8_t  use_sw_init_key   1 = sw_init_key in use; 0 = eFuse KM_INIT_KEY
 *   uint8_t  reserved[3]
 *   uint8_t  sw_init_key[32]   only meaningful if use_sw_init_key
 *   uint8_t  k2_info[64]
 *   uint8_t  k1_encrypted[2][32]   stage-0 first, stage-1 only used when
 *                                  the (key_type, key_len) maps to a
 *                                  multi-stage primary purpose
 *
 * Total: 4 + 32 + 64 + 64 = 164 bytes after the header (168 with header).
 * Stages get the right k1_encrypted slot via km_aes_k1_encrypted_slot(). */
#define KM_AES_PAYLOAD_SIZE  (4U + 32U + 64U + 2U * 32U)

#define KM_AES_OFFSET_USE_SW_INIT_KEY  0
#define KM_AES_OFFSET_SW_INIT_KEY      4
#define KM_AES_OFFSET_K2_INFO          (KM_AES_OFFSET_SW_INIT_KEY + 32)
#define KM_AES_OFFSET_K1_ENCRYPTED     (KM_AES_OFFSET_K2_INFO + 64)

/* Returns 0 for stage-0 (single-stage and multi-primary), 1 for stage-1
 * (multi-stage secondary purpose). Selects which k1_encrypted block to
 * write during this iteration of the deploy loop. */
static uint32_t km_aes_k1_encrypted_slot(stub_km_key_purpose_t p)
{
    if (p == STUB_KM_KEY_PURPOSE_FLASH_256_2
            || p == STUB_KM_KEY_PURPOSE_PSRAM_256_2
            || p == STUB_KM_KEY_PURPOSE_ECDSA_384_L) {
        return 1U;
    }
    return 0U;
}

static int km_pre_start_aes(stub_km_key_purpose_t purpose,
                            const struct km_deploy_header *hdr,
                            const uint8_t *data, uint32_t len)
{
    (void)purpose; (void)hdr;
    if (len != KM_AES_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    bool use_sw_init_key = data[KM_AES_OFFSET_USE_SW_INIT_KEY] != 0U;
    if (use_sw_init_key) {
        stub_target_km_use_sw_init_key();
    } else {
        /* Fall-through path: chip will use eFuse-burned KM_INIT_KEY to
         * decrypt k2_info. If no key block has purpose=KM_INIT_KEY the
         * deployment would silently produce KEY_VLD=0 after the full
         * state-machine cycle — surface it here as an explicit error so
         * the host can give a useful message instead of the generic
         * "deployment failed". */
        if (!stub_target_km_is_efuse_init_key_burned()) {
            return RESPONSE_KM_INIT_KEY_NOT_BURNED;
        }
    }
    return 0;
}

static int km_load_aes(stub_km_key_purpose_t purpose,
                       const struct km_deploy_header *hdr,
                       const uint8_t *data, uint32_t len)
{
    (void)hdr;
    if (len != KM_AES_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    bool use_sw_init_key = data[KM_AES_OFFSET_USE_SW_INIT_KEY] != 0U;
    if (use_sw_init_key) {
        stub_target_km_write_sw_init_key(data + KM_AES_OFFSET_SW_INIT_KEY, 32U);
    }
    stub_target_km_write_assist_info(data + KM_AES_OFFSET_K2_INFO, 64U);
    uint32_t slot = km_aes_k1_encrypted_slot(purpose);
    stub_target_km_write_public_info(
        data + KM_AES_OFFSET_K1_ENCRYPTED + slot * 32U, 32U);
    return 0;
}

int km_plugin_key_deploy_aes(uint8_t command, const uint8_t *data,
                             uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != KM_DEPLOY_HEADER_SIZE + KM_AES_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    const struct km_deploy_header *hdr = (const struct km_deploy_header *)data;
    int err = km_run_deploy(STUB_KM_KEYGEN_MODE_AES, hdr,
                            data + KM_DEPLOY_HEADER_SIZE,
                            len - KM_DEPLOY_HEADER_SIZE,
                            km_pre_start_aes, km_load_aes, NULL,
                            KM_PENDING_KEY_INFO);
    if (err != RESPONSE_SUCCESS) {
        return err;
    }
    resp->post_process = s_km_post_process;
    return RESPONSE_SUCCESS;
}

/* ECDH0 deploy mode wire format (after the 4-byte header):
 *   uint8_t  k1_g[2][KM_ECDH0_INFO_SIZE]   k1*G points (LE-x || LE-y, 64 B
 *                                          each). Slot 0 is used for stage-0
 *                                          and single-stage; slot 1 is used
 *                                          for the multi-stage secondary.
 *
 * Total: 128 bytes after the header (132 with header).
 *
 * Response: a 64-byte k2*G frame followed by 64 or 128 bytes of key_info,
 * streamed via post_process. */
#define KM_ECDH0_PAYLOAD_SIZE  (2U * STUB_KM_ECDH0_INFO_SIZE)

static uint32_t km_ecdh_k1g_slot(stub_km_key_purpose_t p)
{
    if (p == STUB_KM_KEY_PURPOSE_FLASH_256_2
            || p == STUB_KM_KEY_PURPOSE_PSRAM_256_2
            || p == STUB_KM_KEY_PURPOSE_ECDSA_384_L) {
        return 1U;
    }
    return 0U;
}

static int km_load_ecdh0(stub_km_key_purpose_t purpose,
                         const struct km_deploy_header *hdr,
                         const uint8_t *data, uint32_t len)
{
    (void)hdr;
    if (len != KM_ECDH0_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    uint32_t slot = km_ecdh_k1g_slot(purpose);
    stub_target_km_write_public_info(
        data + slot * STUB_KM_ECDH0_INFO_SIZE, STUB_KM_ECDH0_INFO_SIZE);
    return 0;
}

/* ECDH0 GAIN: read k2*G out of ASSIST_INFO_MEM in addition to the standard
 * key_info readback that km_run_one_stage performs.  Slot 0 holds the
 * primary / single-stage k2*G; slot 1 the multi-stage secondary. */
static int km_gain_ecdh0(stub_km_key_purpose_t purpose)
{
    uint32_t slot = km_ecdh_k1g_slot(purpose);
    stub_target_km_read_assist_info(
        s_km_state.k2_g + slot * STUB_KM_ECDH0_INFO_SIZE,
        STUB_KM_ECDH0_INFO_SIZE);
    return 0;
}

int km_plugin_key_deploy_ecdh0(uint8_t command, const uint8_t *data,
                               uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != KM_DEPLOY_HEADER_SIZE + KM_ECDH0_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    const struct km_deploy_header *hdr = (const struct km_deploy_header *)data;
    int err = km_run_deploy(STUB_KM_KEYGEN_MODE_ECDH0, hdr,
                            data + KM_DEPLOY_HEADER_SIZE,
                            len - KM_DEPLOY_HEADER_SIZE,
                            NULL, km_load_ecdh0, km_gain_ecdh0,
                            KM_PENDING_ECDH0);
    if (err != RESPONSE_SUCCESS) {
        return err;
    }
    resp->post_process = s_km_post_process;
    return RESPONSE_SUCCESS;
}

/* ECDH1 deploy mode wire format (after the 4-byte header):
 *   uint8_t  use_sw_init_key   1 = sw_init_key in use; 0 = eFuse KM_INIT_KEY
 *   uint8_t  reserved[3]
 *   uint8_t  sw_init_key[32]
 *   uint8_t  k2_info[64]
 *   uint8_t  k1_g[2][64]       k1*G points; slot ordering same as ECDH0
 *
 * Total: 4 + 32 + 64 + 128 = 228 bytes after header. ECDH1 differs from
 * ECDH0 in that the user supplies k2 indirectly via k2_info instead of
 * receiving k2*G — the chip never returns anything beyond key_info. */
#define KM_ECDH1_PAYLOAD_SIZE  (4U + 32U + 64U + 2U * STUB_KM_ECDH0_INFO_SIZE)

#define KM_ECDH1_OFFSET_USE_SW_INIT_KEY  0
#define KM_ECDH1_OFFSET_SW_INIT_KEY      4
#define KM_ECDH1_OFFSET_K2_INFO          (KM_ECDH1_OFFSET_SW_INIT_KEY + 32)
#define KM_ECDH1_OFFSET_K1_G             (KM_ECDH1_OFFSET_K2_INFO + 64)

static int km_pre_start_ecdh1(stub_km_key_purpose_t purpose,
                              const struct km_deploy_header *hdr,
                              const uint8_t *data, uint32_t len)
{
    (void)purpose; (void)hdr;
    if (len != KM_ECDH1_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    bool use_sw_init_key = data[KM_ECDH1_OFFSET_USE_SW_INIT_KEY] != 0U;
    if (use_sw_init_key) {
        stub_target_km_use_sw_init_key();
    } else {
        /* ECDH1 also decrypts k2_info under init_key — same fall-through
         * rationale as km_pre_start_aes above. */
        if (!stub_target_km_is_efuse_init_key_burned()) {
            return RESPONSE_KM_INIT_KEY_NOT_BURNED;
        }
    }
    return 0;
}

static int km_load_ecdh1(stub_km_key_purpose_t purpose,
                         const struct km_deploy_header *hdr,
                         const uint8_t *data, uint32_t len)
{
    (void)hdr;
    if (len != KM_ECDH1_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    bool use_sw_init_key = data[KM_ECDH1_OFFSET_USE_SW_INIT_KEY] != 0U;
    if (use_sw_init_key) {
        stub_target_km_write_sw_init_key(
            data + KM_ECDH1_OFFSET_SW_INIT_KEY, 32U);
    }
    stub_target_km_write_assist_info(data + KM_ECDH1_OFFSET_K2_INFO, 64U);
    uint32_t slot = km_ecdh_k1g_slot(purpose);
    stub_target_km_write_public_info(
        data + KM_ECDH1_OFFSET_K1_G + slot * STUB_KM_ECDH0_INFO_SIZE,
        STUB_KM_ECDH0_INFO_SIZE);
    return 0;
}

int km_plugin_key_deploy_ecdh1(uint8_t command, const uint8_t *data,
                               uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != KM_DEPLOY_HEADER_SIZE + KM_ECDH1_PAYLOAD_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    const struct km_deploy_header *hdr = (const struct km_deploy_header *)data;
    int err = km_run_deploy(STUB_KM_KEYGEN_MODE_ECDH1, hdr,
                            data + KM_DEPLOY_HEADER_SIZE,
                            len - KM_DEPLOY_HEADER_SIZE,
                            km_pre_start_ecdh1, km_load_ecdh1, NULL,
                            KM_PENDING_KEY_INFO);
    if (err != RESPONSE_SUCCESS) {
        return err;
    }
    resp->post_process = s_km_post_process;
    return RESPONSE_SUCCESS;
}

/* For key_recovery the load callback writes the appropriate key_info
 * slot back into KEYMNG_ASSIST_INFO_MEM. Wire format: 4-byte header
 * followed by 128 bytes of key_info (slot 0 || slot 1). Single-stage
 * deploys leave slot 0 unused — the chip ignores it. */
static int km_load_recovery(stub_km_key_purpose_t purpose,
                            const struct km_deploy_header *hdr,
                            const uint8_t *data, uint32_t len)
{
    (void)hdr;
    if (len != STUB_KM_KEY_INFO_TOTAL) {
        return RESPONSE_BAD_DATA_LEN;
    }
    uint32_t slot = km_key_info_slot(purpose);
    stub_target_km_write_assist_info(
        data + slot * STUB_KM_KEY_RECOVERY_INFO_SIZE,
        STUB_KM_KEY_RECOVERY_INFO_SIZE);
    return 0;
}

int km_plugin_key_recovery(uint8_t command, const uint8_t *data,
                           uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != KM_DEPLOY_HEADER_SIZE + STUB_KM_KEY_INFO_TOTAL) {
        return RESPONSE_BAD_DATA_LEN;
    }
    const struct km_deploy_header *hdr = (const struct km_deploy_header *)data;
    /* No streamed response on success — recovery only activates the key in
     * the KM's static config; the host already has the key_info that drove
     * it. KM_PENDING_NONE skips the post_process streaming. */
    int err = km_run_deploy(STUB_KM_KEYGEN_MODE_RECOVER, hdr,
                            data + KM_DEPLOY_HEADER_SIZE,
                            len - KM_DEPLOY_HEADER_SIZE,
                            NULL, km_load_recovery, NULL,
                            KM_PENDING_NONE);
    if (err != RESPONSE_SUCCESS) {
        return err;
    }
    /* No post_process — successful key_recovery returns status only. */
    (void)resp;
    return RESPONSE_SUCCESS;
}

/*
 * km_plugin_huk_deploy: drive the HUK Generator in GENERATE mode and
 * stream back a 664-byte `esp_key_mgr_huk_info_t` (660 huk_info + 4
 * little-endian CRC32 over those 660 bytes). Idempotent within one stub
 * session — the chip will simply re-run the GENERATE flow on each call,
 * producing fresh huk_info bytes. Burning HUK_GEN_STATE in eFuse so the
 * HUK_RECOVERY mode latches is the host's responsibility (espefuse).
 *
 * Payload: empty.
 */
int km_plugin_huk_deploy(uint8_t command, const uint8_t *data,
                         uint32_t len, struct command_response_data *resp)
{
    (void)command; (void)data;
    if (len != 0U) {
        return RESPONSE_BAD_DATA_LEN;
    }
    if (!stub_target_km_is_supported()) {
        return RESPONSE_KM_UNSUPPORTED_CHIP;
    }

    /* Bring up KM peripheral first — same as huk-recovery / key-deploy. The
     * HUK Generator has its own power-up that succeeds without this, but
     * leaves the KM peripheral unclocked / unreset, which then breaks
     * subsequent operations on the same chip session that rely on KM seeing
     * the HUK (and the cross-boot RECOVER case where the KM-side state
     * carries over). Mirrors IDF's esp_security_init priority-103 flow. */
    stub_target_km_bringup();

    int err = stub_target_huk_configure(STUB_HUK_MODE_GENERATE, s_km_state.huk_info);
    if (err != 0) {
        return RESPONSE_INVALID_COMMAND;  /* Generic failure; the host's
                                           * espefuse / sanity checks should
                                           * already have ensured PUF SRAM is
                                           * writable on this part. */
    }

    /* Append CRC32 in little-endian over the 660-byte huk_info — the same
     * layout `struct esp_key_mgr_huk_info_t` uses on flash. */
    uint32_t crc = esp_rom_crc32_le(0U, s_km_state.huk_info, STUB_KM_HUK_INFO_SIZE);
    s_km_state.huk_info[STUB_KM_HUK_INFO_SIZE + 0] = (uint8_t)(crc & 0xFFU);
    s_km_state.huk_info[STUB_KM_HUK_INFO_SIZE + 1] = (uint8_t)((crc >> 8) & 0xFFU);
    s_km_state.huk_info[STUB_KM_HUK_INFO_SIZE + 2] = (uint8_t)((crc >> 16) & 0xFFU);
    s_km_state.huk_info[STUB_KM_HUK_INFO_SIZE + 3] = (uint8_t)((crc >> 24) & 0xFFU);

    s_km_state.pending_kind = KM_PENDING_HUK_INFO;
    resp->post_process = s_km_post_process;
    return RESPONSE_SUCCESS;
}

/*
 * km_plugin_huk_recovery: drive the HUK Generator in RECOVER mode using a
 * previously-deployed huk_info. Verifies the host-supplied 4-byte CRC over
 * the 660-byte huk_info before loading; rejects mismatches up front rather
 * than letting the HW silently produce an invalid HUK.
 *
 * Payload: 664 bytes (huk_info[660] || crc32_le[4]).
 * Response: status only; no streamed payload.
 */
int km_plugin_huk_recovery(uint8_t command, const uint8_t *data,
                           uint32_t len, struct command_response_data *resp)
{
    (void)command;
    if (len != STUB_KM_HUK_INFO_WIRE_SIZE) {
        return RESPONSE_BAD_DATA_LEN;
    }
    if (!stub_target_km_is_supported()) {
        return RESPONSE_KM_UNSUPPORTED_CHIP;
    }

    uint32_t expected_crc = esp_rom_crc32_le(0U, data, STUB_KM_HUK_INFO_SIZE);
    uint32_t supplied_crc = ((uint32_t)data[STUB_KM_HUK_INFO_SIZE + 0])
                            | ((uint32_t)data[STUB_KM_HUK_INFO_SIZE + 1] << 8)
                            | ((uint32_t)data[STUB_KM_HUK_INFO_SIZE + 2] << 16)
                            | ((uint32_t)data[STUB_KM_HUK_INFO_SIZE + 3] << 24);
    if (supplied_crc != expected_crc) {
        return RESPONSE_BAD_DATA_CHECKSUM;
    }

    memcpy(s_km_state.huk_info, data, STUB_KM_HUK_INFO_SIZE);

    /* The C5 ROM's km_key_recover flow calls pcr_ll_km_en() before any
     * HUK configure (see esp-rom rom/key_mgr/key_mgr.c). It clears both
     * the KM memory and the HUK memory FORCE_PD bits and waits for the
     * KM peripheral to report ready — without that, cross-boot HUK
     * recovery silently produces gen=2 / risk=7 because the HUK memory
     * isn't powered up when the ROM tries to load huk_info. The standalone
     * huk-recovery handler doesn't go through the key-deploy path that
     * would normally call km bringup, so we do it explicitly here. */
    stub_target_km_bringup();

    int err = stub_target_huk_configure(STUB_HUK_MODE_RECOVER, s_km_state.huk_info);
    if (err != 0) {
        return RESPONSE_INVALID_COMMAND;
    }
    /* Surface the post-configure HUK status (gen<<8 | risk) so the host can
     * tell at a glance whether the recovered HUK is healthy. */
    uint8_t gen = 0, risk = 0;
    stub_target_huk_get_status(&gen, &risk);
    resp->value = ((uint32_t)gen << 8) | (uint32_t)risk;
    return RESPONSE_SUCCESS;
}
