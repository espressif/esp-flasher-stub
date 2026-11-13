# Plugin System

The stub supports plugins, separately built binaries that the host uploads with the stub to extend its command set with new capabilities. Plugins are general-purpose — they can add support for external flash chips, memory devices, security features, or any other hardware functionality. A plugin can also provide the transport that links the stub to the host (see [Transport Plugins](#transport-plugins)). This document describes the plugin architecture, the build process, and how to add a new plugin.

## Architecture

### Function Pointer Table (FPT)

Plugins are dispatched through a **Function Pointer Table (FPT)**: a global array of function pointers located in the base stub's `.data` segment. At startup all entries are initialized to `s_plugin_unsupported`, which sends an error response for any unknown opcode. A plugin's start function writes its handlers into the table (see [Start Function](#start-function)).

Key constants (defined in `src/plugin_table.h`):

| Constant | Value | Meaning |
|---|---|---|
| `PLUGIN_FIRST_OPCODE` | `0xD5` | First opcode reserved for plugins |
| `PLUGIN_LAST_OPCODE` | `0xEF` | Last opcode reserved for plugins |
| `PLUGIN_TABLE_SIZE` | `27` | Number of FPT slots |
| `PLUGIN_START_SLOTS` | `2` | Plugins that the host can load at the same time |

### Handler ABI

Every plugin handler must match the following signature:

```c
typedef int (*plugin_cmd_handler_t)(uint8_t command,
                                    const uint8_t *data,
                                    uint32_t len,
                                    struct command_response_data *resp);
```

- `command` — the opcode byte.
- `data` — pointer to the command payload.
- `len` — payload length in bytes.
- `resp` — output struct (zeroed by the dispatcher before the call); the handler fills `resp->value`, `resp->data[]`, and `resp->data_size` as needed. Streaming handlers may also set `resp->post_process` to a callback that runs after the primary response has been sent.

The handler returns an `esp_response_code` value (`RESPONSE_SUCCESS` == 0 on success). The base dispatcher owns primary response framing and the post-process step — handlers must **not** send their own response frames. Post-process callbacks receive `struct cmd_ctx`, including `ctx->transport`, and should use that transport for streaming data and polling ACK frames.

### Plugin Memory

A plugin can have `.text`, `.data` (which holds the `.rodata` constants too, see `common.ld`) and `.bss`. The host uploads `.text` and `.data`, but not `.bss`: the plugin zeroes its own `.bss`, from `_bss_start` to `_bss_end` of its own link. So a large buffer costs no upload time, and the host does not need to know where the plugin's `.bss` is.

The plugin zeroes its `.bss` first in its start function, before any other plugin code runs, by calling `plugin_zero_bss()` from `plugin_table.h`.

### Start Function

The entry point of every plugin is its start function. The host writes it into a free slot of `plugin_start[]` in the base `.data` (see [Upload Order](#upload-order)), and `esp_main` calls it after it zeroes the base `.bss`. The start function zeroes the plugin's `.bss` and registers the plugin. A command plugin writes each handler into its FPT slot; `plugin_table` is exported by the base stub (see [Calling the Base Stub](#calling-the-base-stub)):

```c
void my_plugin_start(void)
{
    plugin_zero_bss();

    plugin_table[MY_PLUGIN_ATTACH - PLUGIN_FIRST_OPCODE] = my_plugin_attach;
    plugin_table[MY_PLUGIN_READ - PLUGIN_FIRST_OPCODE] = my_plugin_read;
}
```

A transport plugin fills the transport hook instead (see [Transport Plugins](#transport-plugins)). Start functions run before clock init and before any transport is up, so they only register and do not touch the hardware.

The plugin linker script sets **`ENTRY(<start_function>)`** after `INCLUDE common.ld`, which overrides `ENTRY(esp_main)` from `common.ld` (required for Xtensa). The start function references everything that the base stub calls, so `--gc-sections` keeps it without `EXTERN()`.

### Dispatch

`handle_command()` in `src/command_handler.c` checks whether the incoming opcode falls in the range `0xD5`–`0xEF`. If so, it indexes into the FPT and calls the corresponding handler. If the entry is still `s_plugin_unsupported`, an error response is returned.

### Upload Order

The host uploads the plugins together with the base stub, before it starts it:

1. Writes each plugin's `entry` into a slot of `plugin_start[]`, at `plugin_start_offset + 4 * slot` in the base `.data`, as a 32-bit little-endian value. The host loads at most `plugin_start_slots` plugins at the same time, and unused slots stay zero.
2. Uploads the base stub `.text` and the patched `.data`.
3. Uploads each plugin's `text` to `text_start`, and its `data` to `data_start` if the plugin has `.data`.
4. Starts the base stub at `entry`.

Plugins stack, so each plugin has its own memory and the host can load any of them on its own. Nothing runs a plugin that is uploaded after the stub starts.

After startup, plugin opcodes use the same command dispatcher and transport operations as base commands. Streaming callbacks use `ctx->transport`, because the base stub selects the transport at runtime.

### Calling the Base Stub

A plugin can call base stub functions and use base stub variables that are marked `STUB_EXPORT` or `STUB_EXPORT_DATA` (`src/stub_export.h`). LTO makes every other base symbol local, so `compute_plugin_addrs.py` adds the address of every global function and variable of the base ELF to the plugin's `<name>_plugin_addrs.ld` fragment as `PROVIDE(<symbol> = <address>);`. The plugin calls them like any other function. A plugin is linked against one base stub build, so these addresses are always current.

`STUB_EXPORT` includes `noinline`: the base keeps an out-of-line copy of an exported function anyway, and inlining it into the base callers only adds code.

### Xtensa IRAM Caveat

On Xtensa cores (ESP32-S2, ESP32-S3), IRAM only supports 32-bit stores. Writing non-4-byte-aligned data to IRAM causes a `LoadStoreError` exception (EXCCAUSE=3). `elf2json.py` automatically pads the plugin `.text` to a 4-byte boundary before embedding it in the JSON file.

## Build

A plugin must be linked at a fixed address immediately after the base stub (and after any earlier plugins), so its load address depends on where the base stub's `.text`, `.data` and `.bss` end. Those addresses are only known once the base stub is linked, so the address is computed at **build time**, after the base stub:

```
stub-<chip>.elf                          (base stub linked)
            │
            ▼
tools/compute_plugin_addrs.py
  Reads where the base ELF .text/.data/.bss end (and those of any
  preceding plugin ELFs passed via --after) and writes a linker
  fragment <name>_plugin_addrs.ld:  PLUGIN_TEXT_ADDR / PLUGIN_BSS_ADDR
            │
            ▼
stub-<chip>-<name>-plugin.elf
  Linked with  -T <name>_plugin_addrs.ld  -T <name>_plugin.ld
  and its own copy of esp-stub-lib.
  A LINK_DEPENDS on the fragment relinks the plugin whenever the
  base stub's size changes.
            │
            ▼
tools/elf2json.py
  Converts the base stub ELF to JSON and embeds each plugin's
  .text, .data and entry addresses under the "plugins" key.
```

The generated fragment replaces the older `-Wl,--defsym` approach: `compute_plugin_addrs.py` emits linker-script symbol assignments that the plugin linker script consumes in its `MEMORY` block. Multiple plugins **stack** — each plugin's fragment is computed from the base stub plus every preceding plugin ELF (`--after`). The fragment uses the linked end addresses, not the section sizes, because the linker can move a section up to its alignment. For example, the ESP32-P4 SPI plugin's `.bss` holds DMA descriptors and is 8-byte aligned.

### Key Tool Files

| File | Purpose |
|---|---|
| `tools/compute_plugin_addrs.py` | Reads where the base (+ preceding plugin) ELF sections end and emits a `<name>_plugin_addrs.ld` fragment defining `PLUGIN_TEXT_ADDR`/`PLUGIN_BSS_ADDR` and the addresses of the exported base symbols |
| `src/stub_export.h` | `STUB_EXPORT` and `STUB_EXPORT_DATA`, which mark the base symbols that plugins can use |
| `tools/elf2json.py` | Converts ELF to JSON; embeds plugin `.text`, `.data` and entry addresses |
| `src/plugin_table.h` | FPT ABI constants, `plugin_cmd_handler_t` typedef and `plugin_start[]` |
| `src/command_handler.c` | Opcode dispatch to FPT |
| `src/main.c` | `plugin_start[]`; `esp_main` calls the plugin start functions |
| `src/transport.h` / `src/transport.c` | Transport plugin hook (`transport_plugin`) and its ABI |
| `src/nand_plugin.c` | Reference plugin (10 NAND handlers and `nand_plugin_start`) |
| `src/ld/nand_plugin.ld` | Reference plugin linker script |
| `src/spi_transport_plugin.c` | Reference transport plugin (SPI slave) |
| `src/ld/spi_plugin.ld` | Reference transport plugin linker script |
| `src/sdio_transport_plugin.c` | SDIO transport plugin |
| `src/ld/sdio_plugin.ld` | SDIO transport plugin linker script |

## Chip Support

Plugins are not supported on ESP8266 or ESP32. All other chips (ESP32-S2, ESP32-S3, ESP32-C3, etc.) have the FPT and the transport hook built in to the base stub. These plugins ship today:

| Plugin | Chips |
|---|---|
| [NAND](plugins/nand-flash.md) | ESP32-S3 |
| [SPI transport](plugins/spi-transport.md) | ESP32-C2, ESP32-C3, ESP32-P4, ESP32-S2, ESP32-S3 |
| [SDIO transport](plugins/sdio-transport.md) | ESP32-C5, ESP32-C6, ESP32-C61 |

On ESP32-S3 the SPI transport plugin is stacked after the NAND plugin.

## Adding a New Plugin

The following steps use a hypothetical "SPI RAM" plugin as a concrete example.

### Step 1 — Choose Opcodes

Pick unused opcodes from the `0xDE`–`0xEF` range (`0xD5`–`0xDD` are already reserved by NAND). Document the new opcodes in `src/commands.h`:

```c
#define ESP_SPI_RAM_ATTACH       0xDE
#define ESP_SPI_RAM_READ         0xDF
#define ESP_SPI_RAM_WRITE        0xE0
```

### Step 2 — Create `src/spi_ram_plugin.c`

Implement functions matching `plugin_cmd_handler_t` and a start function that registers them (see [Start Function](#start-function)). Include `plugin_table.h` for the ABI typedef and `plugin_table`:

```c
#include "commands.h"
#include "plugin_table.h"

static uint32_t s_ram_size;   /* .bss — zeroed by spi_ram_plugin_start() */

int spi_ram_plugin_attach(uint8_t command, const uint8_t *data,
                          uint32_t len, struct command_response_data *resp)
{
    (void)command;
    /* init hardware, fill resp if needed, return status code */
    return RESPONSE_SUCCESS;
}

int spi_ram_plugin_read(uint8_t command, const uint8_t *data,
                        uint32_t len, struct command_response_data *resp)
{
    (void)command;
    /* read, fill resp->data / resp->data_size, return status code */
    return RESPONSE_SUCCESS;
}

void spi_ram_plugin_start(void)
{
    plugin_zero_bss();

    plugin_table[ESP_SPI_RAM_ATTACH - PLUGIN_FIRST_OPCODE] = spi_ram_plugin_attach;
    plugin_table[ESP_SPI_RAM_READ - PLUGIN_FIRST_OPCODE] = spi_ram_plugin_read;
}
```

### Step 3 — Create `src/ld/spi_ram_plugin.ld`

Follow the pattern of `src/ld/nand_plugin.ld`. `PLUGIN_TEXT_ADDR` and `PLUGIN_BSS_ADDR` come from the generated `<name>_plugin_addrs.ld` fragment (pulled in via `-T`). Reuse `common.ld` for section layout and set the start function as the entry point:

```ld
MEMORY {
    iram : org = PLUGIN_TEXT_ADDR, len = 0x10000
    dram : org = PLUGIN_BSS_ADDR,  len = 0x10000
}

INCLUDE common.ld

ENTRY(spi_ram_plugin_start)
```

### Step 4 — Add Target HAL in `esp-stub-lib`

Implement target-specific functions (e.g., `src/target/esp32s3/src/spi_ram.c`) following the same pattern as `nand.c` and `spi_nand.c`, and add the file to the chip's library in esp-stub-lib.

Every plugin links its own copy of esp-stub-lib, so the plugin gets the HAL, the include paths and the ROM linker scripts from the library. The copy does not share state with the base stub: library globals start zeroed in the plugin, whatever the base stub set at runtime.

### Step 5 — Update `src/CMakeLists.txt`

List the chips that build the plugin, then call `stub_add_plugin()` with the plugin sources and linker script. The macro derives the target name, generates the `<name>_plugin_addrs.ld` fragment, wires the build-time dependencies, links esp-stub-lib, and stacks the plugin after any earlier ones — no addresses to specify by hand:

```cmake
set(SPI_RAM_PLUGIN_CHIPS esp32s3)

if(TARGET_CHIP IN_LIST SPI_RAM_PLUGIN_CHIPS)
    stub_add_plugin(
        NAME          spi_ram
        SOURCES       spi_ram_plugin.c
        LINKER_SCRIPT ${LINKER_SCRIPTS_DIR}/spi_ram_plugin.ld
    )
endif()
```

The list holds `TARGET_CHIP` values, so a chip revision such as `esp32p4-rev1` is listed on its own.

Plugins load in the order `stub_add_plugin()` is called: the first is placed right after the base stub and each subsequent plugin stacks after the previous one.

### Step 6 — Check the JSON

`elf2json.py` needs no change. It accepts repeated `--plugin <name> <elf>` arguments, reads each plugin ELF's `.text` and `.data` (padded to 4 bytes for Xtensa) and its entry point, and records the plugin in the output JSON under the `plugins` key. All addresses are absolute:

```json
"plugins": {
    "spi_ram": {
        "text": "<base64-encoded padded .text>",
        "text_start": 1077944160,
        "data": "<base64-encoded padded .data>",
        "data_start": 1070293120,
        "entry": 1077944800
    }
}
```

`data` and `data_start` appear only if the plugin has `.data`.

### Step 7 — Update esptool (Python Side)

In `StubFlasher` (esptool `loader.py`), load the plugin as [Upload Order](#upload-order) describes: write its `entry` into a `plugin_start[]` slot of the base data, and upload its `text` and `data` after the base stub.

## Transport Plugins

A transport plugin moves a transport (for example SPI or SDIO) out of the base stub, so stubs for hosts that do not use it do not carry its code. It is an ordinary plugin with a start function, but the start function fills the transport hook instead of the FPT. The [SPI transport](plugins/spi-transport.md) (`src/spi_transport_plugin.c`) is the reference transport plugin; the [SDIO transport](plugins/sdio-transport.md) (`src/sdio_transport_plugin.c`) follows the same pattern.

### Transport Hook

The base stub holds one transport hook, `transport_plugin`, in its `.bss` (`src/transport.h`):

```c
struct stub_transport_plugin {
    bool (*is_active)(void);
    const struct stub_transport_ops *(*init)(void);
};
```

If no start function fills the hook, the stub uses only its built-in transports. When one does:

1. `stub_transport_detect()` calls `is_active()` before it probes the built-in transports. If it returns `true`, the stub selects `TRANSPORT_PLUGIN`.
2. `stub_transport_init()` calls `init()` at the point where a built-in transport would be initialized. `init()` initializes the hardware and returns the transport operations that the stub uses for the rest of the session.

Detection runs before clock init and `init()` after it, so the hook has two functions. The base stub raises the CPU clock before it calls `init()`, so the plugin must not call `stub_lib_clock_init()`: its own copy of esp-stub-lib would set the clock, but the base stub would not see the new frequency. It has one slot, so the host loads at most one transport plugin — the one for the link it is using.

### Plugin Rules

- In the start function, zero the plugin's `.bss` and then write both functions into `transport_plugin`:

  ```c
  void spi_transport_plugin_start(void)
  {
      plugin_zero_bss();

      transport_plugin.is_active = spi_transport_is_active;
      transport_plugin.init = spi_transport_init;
  }
  ```

- The operations table that `init()` returns can be `static const`.
- Receive frames into the base stub's frame buffer by calling the exported `frame_buffer_*` functions (see [Calling the Base Stub](#calling-the-base-stub)). A second pair of frame buffers does not fit in RAM on every chip. `recv_release` can be `frame_buffer_reset`.

The JSON entry and the upload are the same as for any plugin.
