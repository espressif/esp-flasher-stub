# NAND Flash Support

> [!WARNING]
> NAND flash support is in **preview** state. It has been tested only with the W25N01GV chip on ESP32-S3. The API and behavior may change in future releases.

## Supported Hardware

### NAND Chips

The W25N01GVxxxG/T/R family is supported (1 Gbit, SPI NAND).

### Supported MCUs

**ESP32-S3 only** (for now). Support for additional chips is planned. ESP32 and ESP8266 do not support the plugin system and will not be supported.

## How It Works

NAND support is implemented as a [plugin](plugin-system.md) loaded at runtime on top of the base stub. The plugin is included in the ESP32-S3 JSON stub file and is automatically loaded by esptool when `--flash-type nand` is specified.

The plugin initializes SPI2 and communicates with the NAND chip over a standard SPI connection specified via `--spi-connection CLK,Q,D,HD,CS`.

## NAND Opcodes

All NAND commands are dispatched through the plugin Function Pointer Table (FPT):

| Opcode | Command | Description |
|---|---|---|
| `0xD5` | `ESP_SPI_NAND_ATTACH` | Initialize SPI2 and detect the NAND chip |
| `0xD6` | `ESP_SPI_NAND_READ_BBM` | Read the bad-block marker of a page |
| `0xD7` | `ESP_SPI_NAND_WRITE_BBM` | Write the bad-block marker |
| `0xD8` | `ESP_SPI_NAND_READ_FLASH` | Read data pages |
| `0xD9` | `ESP_SPI_NAND_WRITE_FLASH_BEGIN` | Begin a write session |
| `0xDA` | `ESP_SPI_NAND_WRITE_FLASH_DATA` | Stream write data |
| `0xDB` | `ESP_SPI_NAND_ERASE_FLASH` | Erase the entire chip |
| `0xDC` | `ESP_SPI_NAND_ERASE_REGION` | Erase a block-aligned region |
| `0xDD` | `ESP_SPI_NAND_READ_PAGE_DEBUG` | Read one raw page (debug) |

## NAND Geometry (W25N01GV)

| Parameter | Value |
|---|---|
| Page size | 2048 bytes |
| Pages per block | 64 |
| Block size | 128 KB (131072 bytes) |
| Total blocks | 1024 |
| Total capacity | 1 Gbit (128 MB) |

## Bad-Block Management

The spare area of the first page in each block is used for bad-block marking (industry standard). The `write-flash` command:

1. Reads the spare area of each block before writing.
2. Skips blocks marked as bad and prints the actual write address.
3. Uses `write-nand-bbm` to mark newly detected bad blocks.

Use `read-nand-bbm` and `write-nand-bbm` to inspect or update the bad-block table manually.
