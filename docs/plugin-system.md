# Plugin System

The stub supports runtime-loadable plugins that extend its command set with new capabilities. Plugins are general-purpose — they can add support for external flash chips, memory devices, security features, or any other hardware functionality. This document describes the plugin architecture, the build process, and how to add a new plugin.

## Architecture

### Function Pointer Table (FPT)

Plugins are dispatched through a **Function Pointer Table (FPT)**: a global array of function pointers located in the base stub's `.data` segment. At startup all entries are initialized to `s_plugin_unsupported`, which sends an error response for any unknown opcode.

Key constants (defined in `src/plugin_table.h`):

| Constant | Value | Meaning |
|---|---|---|
| `PLUGIN_FIRST_OPCODE` | `0xD5` | First opcode reserved for plugins |
| `PLUGIN_LAST_OPCODE` | `0xEF` | Last opcode reserved for plugins |
| `PLUGIN_TABLE_SIZE` | `27` | Number of FPT slots |
| `plugin_table_offset` | *(computed)* | Byte offset of FPT within the `.data` segment, derived from the `plugin_table` ELF symbol at JSON generation time |

### Handler ABI

Every plugin handler must match the following signature:

```c
typedef void (*plugin_cmd_handler_t)(uint8_t command, const uint8_t *data, uint16_t size);
```

- `command` — the opcode byte, passed so the handler can embed the correct opcode in its SLIP response frame.
- `data` — pointer to the command payload.
- `size` — payload length in bytes.

The handler is responsible for sending its own SLIP response frame(s). It must not return a value.

Plugin handlers must use **BSS-only global state** — no initialized (`.data`) globals. Only zero-initialized globals (`.bss`) are permitted because only the `.text` and `.bss` sections are present in the plugin binary.

### Dispatch

`handle_command()` in `src/command_handler.c` checks whether the incoming opcode falls in the range `0xD5`–`0xEF`. If so, it indexes into the FPT and calls the corresponding handler. If the entry is still `s_plugin_unsupported`, an error response is returned.

### Upload Order

Before uploading the stub, esptool (Python side):

1. Patches the relevant FPT entries with the plugin handler addresses.
2. Uploads in this order:
   1. Base stub `.text`
   2. Base stub `.data` + `.bss` zeros + plugin `.bss` zeros
   3. Plugin `.text`
   4. Calls `mem_finish` to start execution

### Xtensa IRAM Caveat

On Xtensa cores (ESP32-S2, ESP32-S3), IRAM only supports 32-bit stores. Writing non-4-byte-aligned data to IRAM causes a `LoadStoreError` exception (EXCCAUSE=3). `elf2json.py` automatically pads the plugin `.text` to a 4-byte boundary before embedding it in the JSON file.

## Two-Pass Build

Because the plugin must be linked at fixed addresses that depend on the size of the base stub, the build uses two passes:

```
Pass 1: cmake/ninja → base stub ELF
            │
            ▼
tools/compute_plugin_addrs.py
  Reads base stub ELF, extracts .text, .data, and .bss sizes,
  computes PLUGIN_TEXT_ADDR and PLUGIN_BSS_ADDR
            │
            ▼
Pass 2: cmake/ninja → plugin ELF (linked at computed addresses)
            │
            ▼
tools/elf2json.py
  Converts base stub ELF to JSON, embeds plugin .text and
  .bss metadata as an additional section in the JSON file
```

### Key Tool Files

| File | Purpose |
|---|---|
| `tools/compute_plugin_addrs.py` | Extracts base ELF sizes and computes plugin load addresses |
| `tools/elf2json.py` | Converts ELF to JSON; embeds plugin `.text` and `.bss` size |
| `src/plugin_table.h` | FPT ABI constants and `plugin_cmd_handler_t` typedef |
| `src/command_handler.c` | Opcode dispatch to FPT |
| `src/nand_plugin.c` | Reference plugin (9 NAND handlers) |
| `src/ld/nand_plugin.ld` | Reference plugin linker script |

## Chip Support

Plugins are not supported on ESP8266 or ESP32. All other chips (ESP32-S2, ESP32-S3, ESP32-C3, etc.) have the FPT built in to the base stub. Currently, ESP32-S3 is the only chip that ships with a plugin (the NAND plugin). Support for additional chips is planned.

## Adding a New Plugin

The following steps use a hypothetical "SPI RAM" plugin as a concrete example.

### Step 1 — Choose Opcodes

Pick unused opcodes from the `0xDE`–`0xEF` range (below `0xD5`–`0xDD` which are used by NAND). Document the new opcodes in `src/commands.h`:

```c
#define ESP_SPI_RAM_ATTACH       0xDE
#define ESP_SPI_RAM_READ         0xDF
#define ESP_SPI_RAM_WRITE        0xE0
```

### Step 2 — Create `src/spi_ram_plugin.c`

Implement functions matching `plugin_cmd_handler_t`. Use only BSS globals (no `.data` initializers). Include `plugin_table.h`:

```c
#include "plugin_table.h"

static uint32_t s_ram_size;   /* BSS — zero-initialized */

static void cmd_spi_ram_attach(uint8_t command, const uint8_t *data, uint16_t size)
{
    /* init hardware, send SLIP response */
}

static void cmd_spi_ram_read(uint8_t command, const uint8_t *data, uint16_t size)
{
    /* read, send SLIP response */
}
```

### Step 3 — Create `src/ld/spi_ram_plugin.ld`

Follow the pattern of `src/ld/nand_plugin.ld`. The plugin must contain only `.text` and `.bss` sections and must be linked at the addresses supplied by CMake:

```ld
MEMORY {
    iram (rx) : org = PLUGIN_TEXT_ADDR, len = 0x10000
    dram (rw) : org = PLUGIN_BSS_ADDR,  len = 0x10000
}

SECTIONS {
    .text : { *(.text .text.*) } > iram
    .bss  : { *(.bss  .bss.*)  } > dram
}
```

### Step 4 — Add Target HAL in `esp-stub-lib`

Implement target-specific functions (e.g., `src/target/esp32s3/src/spi_ram.c`) following the same pattern as `nand.c` and `spi_nand.c`.

### Step 5 — Update `CMakeLists.txt`

Add detection of the new HAL file (analogous to `_NAND_C`), then define an `add_executable` target for the plugin and pass the computed load addresses:

```cmake
set(_SPI_RAM_C ${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/src/spi_ram.c)

if(EXISTS "${_SPI_RAM_C}")
    add_executable(${SPI_RAM_PLUGIN_NAME}
        spi_ram_plugin.c
        ${_SPI_RAM_C}
    )
    target_link_options(${SPI_RAM_PLUGIN_NAME} PRIVATE
        -T${PLUGIN_LD}
        -Wl,--defsym,PLUGIN_TEXT_ADDR=${PLUGIN_TEXT_ADDR}
        -Wl,--defsym,PLUGIN_BSS_ADDR=${PLUGIN_BSS_ADDR}
    )
endif()
```

### Step 6 — Update `tools/elf2json.py`

Add `--plugin spi_ram <elf>` argument handling. The script must read the plugin ELF's `.text` (padded to 4 bytes for Xtensa), resolve handler symbol addresses, and record the plugin in the output JSON under the `plugins` key:

```json
"plugins": {
    "spi_ram": {
        "text": "<base64-encoded padded .text>",
        "text_start": 1077944160,
        "bss_size": 4096,
        "handlers": {
            "0xDE": 0,
            "0xDF": 128,
            "0xE0": 256
        }
    }
}
```

The `handlers` map contains opcode → offset-from-plugin-text-start pairs. Register the handler symbols in the `PLUGIN_HANDLER_SYMBOLS` dict at the top of `elf2json.py`.

### Step 7 — Update esptool (Python Side)

In `StubFlasher` (esptool `loader.py`):

1. Detect the `"plugins"` key in the JSON stub file.
2. Patch the FPT entries for the new opcodes with the plugin handler addresses (plugin text start + handler offset).
3. Upload plugin `.bss` zeros appended to the base data segment, then upload plugin `.text` after the base text.
