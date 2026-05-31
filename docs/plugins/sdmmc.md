# SDMMC Card Support

> [!WARNING]
> SDMMC support is in **preview** state. It has been tested with SDHC/SDSC SD cards on ESP32-S3. The API and behavior may change in future releases.

## Supported Hardware

### Cards

SD (SDSC/SDHC/SDXC) and eMMC chips accessed over the SDMMC bus in 1-bit or 4-bit mode. 8-bit eMMC init is not yet implemented.

### Supported MCUs

**ESP32-S3 only** (for now). Other chips with an SDMMC peripheral (ESP32, ESP32-P4) are not yet supported.

## Geometry

Fixed at 512-byte sectors (the SD/MMC standard). All host-facing offsets and sizes must be sector-aligned.

## Clock Source

The stub uses **XTAL (40 MHz)** as the SDMMC clock source rather than PLL_F160M.

The IDF bootloader enables PLL_F160M during startup, but the ROM bootloader (which runs before the stub) does not — selecting `clk_sel=1` in stub mode leaves the controller's internal state machines unclocked and every command times out. XTAL caps card clock at 20 MHz (SD Default Speed), which is sufficient for the flasher stub's use case.

## Protocol Overview

SDMMC support is implemented as a [plugin](../plugin-system.md) that runs on top of the base stub. The base stub JSON for ESP32-S3 includes a `plugins.sdmmc` section with the plugin binary, load addresses, and a handler offset table. The host tool detects the plugin via the `plugins` key in the stub JSON and loads it when SDMMC operations are requested.

All SDMMC opcodes (`0xDF`–`0xE5`) are dispatched through the FPT. The handler signature is `plugin_cmd_handler_t` (see `plugin_table.h`).

## Opcode Reference

### `0xDF` — `SDMMC_ATTACH`

**Request payload**: 16-byte `stub_target_sdmmc_attach_config_t`:

| Offset | Field | Notes |
|---|---|---|
| 0 | `slot` (u8) | 0 or 1; `0xFF` selects chip defaults |
| 1 | `width` (u8) | 1, 4, or 8 |
| 2 | `freq_khz` (u16 LE) | post-init bus frequency; 0 = 20 MHz default |
| 4 | `cd_pin` (u8) | card-detect GPIO; `0xFF` = none |
| 5 | `wp_pin` (u8) | write-protect GPIO; `0xFF` = none |
| 6 | `pin_clk` (u8) | |
| 7 | `pin_cmd` (u8) | |
| 8 | `pin_d[8]` (u8×8) | D0..D7; entries past `width-1` ignored |

Defaults (when `slot == 0xFF`) match ESP-IDF v6.0 `SDMMC_SLOT_CONFIG_DEFAULT` for ESP32-S3: CLK=14, CMD=15, D0=2, D1=4, D2=12, D3=13, D4=33, D5=34, D6=35, D7=36, slot 0, 4-bit, 20 MHz.

**Response**: `value` = capacity in sectors. Extra payload (12 bytes): OCR (LE), RCA (LE), flags (bit0=is_mmc, bit1=is_high_capacity, bits[7:4]=actual width). On failure `value` carries a packed `{stage, err, RINTSTS}` diagnostic word — see `target/sdmmc.h`.

---

### `0xE0` — `SDMMC_READ_FLASH`

**Request payload**: 4 × LE32: `offset`, `read_size`, `packet_size`, `max_inflight` (last informational; stub enforces a window of 1).

**Response**: initial `RESPONSE_SUCCESS` frame, then streamed data packets followed by a 16-byte MD5 digest. On mid-stream read failure the stub continues with zero-filled packets and corrupts the MD5 to surface as host-side verification failure.

---

### `0xE1` — `SDMMC_WRITE_FLASH_BEGIN`

**Request payload**: 4 × LE32: `offset` (sector-aligned), `total_size`, `block_size`, `packet_size` (last two informational).

**Response**: `RESPONSE_SUCCESS` or `RESPONSE_BAD_DATA_LEN`.

---

### `0xE2` — `SDMMC_WRITE_FLASH_DATA`

**Request payload**: 16-byte header (`data_len` at offset 0) + `data_len` bytes of data. The XOR checksum must be in the command frame header at offset −4 relative to the payload pointer (same scheme as base `ESP_FLASH_DATA`).

**Response**: `RESPONSE_SUCCESS` after each packet, or `RESPONSE_FAILED_SPI_OP` / `RESPONSE_NAND_PROGRAM_FAILED` on data-phase failure.

---

### `0xE3` — `SDMMC_WRITE_FLASH_END`

**Request payload**: 4-byte LE `reboot_flag` (ignored — no chip-side reboot action).

**Response**: `RESPONSE_SUCCESS` or `RESPONSE_BAD_DATA_LEN`. Pads any partial trailing sector with `0xFF` before flushing.

---

### `0xE4` — `SDMMC_ERASE_REGION`

**Request payload**: 2 × LE32: `offset`, `erase_size`. Both must be sector-aligned.

**Response**: `RESPONSE_SUCCESS`, `RESPONSE_BAD_DATA_LEN`, or `RESPONSE_FAILED_SPI_OP`. Implemented via CMD32/33/38 (SD) or CMD35/36/38 (eMMC).

---

### `0xE5` — `SDMMC_GET_INFO`

**Request payload**: empty.

**Response**: same fields as `SDMMC_ATTACH` plus 16 bytes of raw CID. Useful for host-side card identification once the card is attached.

## Limitations

- **Single-block transfers only** — CMD17/CMD24, one sector per command. Multi-block (CMD18/CMD25) would be faster but is not yet implemented.
- **No bus-frequency override above 20 MHz** — High Speed (50 MHz) and UHS-I are not implemented. Would require explicitly enabling PLL_F160M (regi2c sequence).
- **No SDSC capacity reporting** — only CSD v2.0 (SDHC/SDXC, eMMC ≥ 2 GB) capacity is decoded. SDSC cards still read/write correctly but `capacity_bytes` is reported as 0.
- **No 8-bit eMMC init** — CMD6 SWITCH for eMMC bus-width change is not implemented.
- **Polled I/O** — no interrupt handling. Single 512-byte IDMAC descriptor per transfer.
