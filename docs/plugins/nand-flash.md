# NAND Flash Support

> [!WARNING]
> NAND flash support is in **preview** state. It has been tested only with the W25N01GV chip on ESP32-S3. The API and behavior may change in future releases.

## Supported Hardware

### NAND Chips

The W25N01GVxxxG/T/R family is supported (1 Gbit, SPI NAND).

### Supported MCUs

**ESP32-S3 only** (for now). Support for additional chips is planned. ESP32 and ESP8266 do not support the plugin system and will not be supported.

## NAND Geometry (W25N01GV)

| Parameter | Value |
|---|---|
| Page size | 2048 bytes |
| Pages per block | 64 |
| Block size | 128 KB (131072 bytes) |
| Total blocks | 1024 |
| Total capacity | 1 Gbit (128 MB) |

## Protocol Overview

NAND support is implemented as a [plugin](../plugin-system.md) that runs on top of the base stub. The base stub JSON for ESP32-S3 includes a `plugins.nand` section with the plugin binary, load addresses, and a handler offset table. The host tool detects the plugin via the `plugins` key in the stub JSON and loads it when NAND operations are requested.

Loading sequence: the host parses the base stub JSON, patches the Function Pointer Table (FPT) in the base stub `.data` segment with the per-opcode handler addresses from the plugin, appends plugin BSS zeros to the data upload, and uploads the plugin `.text` immediately after the base stub `.text`. After that, the host issues plugin opcodes using the same SLIP framing as base stub commands.

All NAND opcodes (`0xD5–0xDE`) are dispatched through the FPT. The handler signature is `plugin_cmd_handler_t` (see `plugin_table.h`).

## Opcode Reference

### `0xD5` — `SPI_NAND_ATTACH`

**Request payload**: 4-byte LE `hspi_arg` (SPI pin configuration, same encoding as `SPI_ATTACH`).

**Response**: `value = status_reg<<24 | mfr_id<<16 | dev_id`, extra payload = 1-byte `prot_reg`. Status `RESPONSE_SUCCESS` or `RESPONSE_FAILED_SPI_OP`.

---

### `0xD6` — `SPI_NAND_READ_BBM`

**Request payload**: 4-byte LE `page_number`.

**Response**: `value` = bad-block marker word (4 bytes of spare area). Status `RESPONSE_SUCCESS` or `RESPONSE_FAILED_SPI_OP`.

---

### `0xD7` — `SPI_NAND_WRITE_BBM`

**Request payload**: 4-byte LE `page_number` + 1-byte `is_bad` (0 = good, non-zero = bad).

**Response**: status `RESPONSE_SUCCESS` or `RESPONSE_FAILED_SPI_OP`.

---

### `0xD8` — `SPI_NAND_READ_FLASH`

**Request payload**: 4 × LE32: `offset`, `read_size`, `packet_size`, `max_inflight` (last field is informational; the stub enforces its own flow-control window of 1).

**Response**: initial `RESPONSE_SUCCESS` frame, then streamed data packets of `packet_size` bytes each, followed by a 16-byte MD5 digest frame. If a NAND read error occurs mid-stream, the stub continues sending zero-filled packets to satisfy the expected byte count and deliberately corrupts the final MD5 frame (each byte XORed with `0xFF`), which surfaces as a verification failure on the host.

---

### `0xD9` — `SPI_NAND_WRITE_FLASH_BEGIN`

**Request payload**: 4 × LE32: `offset` (must be page-aligned), `total_size`, `block_size`, `packet_size` (last two fields are informational).

**Response**: `RESPONSE_SUCCESS` or `RESPONSE_BAD_DATA_LEN` (unaligned offset).

**State**: sets `in_progress = true`, `total_remaining = total_size`. Must be called before any `WRITE_FLASH_DATA` or `WRITE_FLASH_END`.

---

### `0xDA` — `SPI_NAND_WRITE_FLASH_DATA`

**Request payload**: 16-byte header (`data_len` at bytes 0–3, rest unused) + `data_len` bytes of flash data. The XOR checksum of the flash data must be in the SLIP framing header at offset −4 relative to the payload pointer.

**Response**: `RESPONSE_SUCCESS` after each packet. Mid-stream full-page flush errors are reported inline as `RESPONSE_FAILED_SPI_OP`.

**Precondition**: `WRITE_FLASH_BEGIN` must have been called first (`in_progress == true`). Does not clear `in_progress` when `total_remaining` reaches zero; `WRITE_FLASH_END` is responsible for the final flush and state-clear.

---

### `0xDE` — `SPI_NAND_WRITE_FLASH_END`

**Request payload**: 4-byte LE `reboot_flag` (exists for parity with `ESP_FLASH_END`; the NAND plugin has no chip-side reboot action and ignores this value).

**Response**: `RESPONSE_SUCCESS`, `RESPONSE_BAD_DATA_LEN` (wrong size or `total_remaining != 0`), `RESPONSE_NOT_IN_FLASH_MODE` (no active session), or `RESPONSE_FAILED_SPI_OP` (final page flush failure).

**Precondition**: `in_progress == true` and `total_remaining == 0`. If `page_buf_filled > 0`, pads the last partial page with `0xFF` and flushes it to NAND. Clears `s_nand_write_state` on success.

**State machine**: a complete write session is `BEGIN → DATA* → END`. The host must send exactly one `END` per `BEGIN`.

---

### `0xDB` — `SPI_NAND_ERASE_FLASH`

**Request payload**: empty.

**Response**: `RESPONSE_SUCCESS` or `RESPONSE_FAILED_SPI_OP` (first failing block).

Erases all 1024 blocks sequentially.

---

### `0xDC` — `SPI_NAND_ERASE_REGION`

**Request payload**: 2 × LE32: `offset`, `erase_size`. Both must be block-aligned (multiples of 128 KB).

**Response**: `RESPONSE_SUCCESS`, `RESPONSE_BAD_DATA_LEN` (misalignment), or `RESPONSE_FAILED_SPI_OP`.

---

### `0xDD` — `SPI_NAND_READ_PAGE_DEBUG`

**Request payload**: 4-byte LE `page_number`.

**Response**: `value = page_number`, extra payload = first 16 bytes of the page. Status `RESPONSE_SUCCESS` or `RESPONSE_FAILED_SPI_OP`. This opcode is for diagnostics only; it reads a raw page without decoding.

---

## Bad-Block Management

The spare area of the first page in each block holds the bad-block marker (industry-standard convention). The host tool is responsible for bad-block management:

1. Read the spare area of a block's first page via `SPI_NAND_READ_BBM` before writing.
2. Skip blocks where the low byte of the returned value is not `0xFF`.
3. Mark newly discovered bad blocks via `SPI_NAND_WRITE_BBM` after a program failure.
