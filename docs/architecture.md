# Architecture

## Overview

The esp-flasher-stub firmware is a bare-metal C application that runs on ESP chip RAM. It communicates with a host tool (esptool) through a transport abstraction to perform flash programming and other operations. UART, USB-Serial-JTAG, and USB-OTG carry [SLIP](https://datatracker.ietf.org/doc/html/rfc1055)-framed commands; SDIO carries raw command frames.

```
┌─────────────────────────────────────────────────────────┐
│  Host (esptool)                                         │
│  Sends command frames over UART / USB / SDIO            │
└───────────────────────┬─────────────────────────────────┘
                        │
            UART / USB-Serial-JTAG / USB-OTG / SDIO
                        │
┌───────────────────────▼─────────────────────────────────┐
│  ESP Flasher Stub (runs in chip RAM)                    │
│                                                         │
│  ┌──────────┐  ┌───────────────┐  ┌──────────────────┐  │
│  │Transport │→ │ Frame Buffer  │→ │ Command Handler  │  │
│  │  Layer   │  │ + SLIP/SDIO   │  │   & Dispatch     │  │
│  └──────────┘  └───────────────┘  └────────┬─────────┘  │
│                                            │            │
│                                   ┌────────▼─────────┐  │
│                                   │  Plugin (FPT)    │  │
│                                   │ (optional, e.g.  │  │
│                                   │  NAND plugin)    │  │
│                                   └────────┬─────────┘  │
│                                            │            │
│                                   ┌────────▼─────────┐  │
│                                   │   esp-stub-lib   │  │
│                                   │  (flash, UART,   │  │
│                                   │   security, …)   │  │
│                                   └──────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Source Code Structure

```
src/
├── main.c              Entry point (esp_main), BSS init, main loop
├── frame_buffer.c / .h Shared double-buffered RX frame storage
├── slip.c / slip.h     SLIP byte-stuffing and byte-stream decoding
├── command_handler.c   Command parsing, dispatch, and response
├── command_handler.h   Command context and response API
├── commands.h          Command IDs and response codes
├── plugin_table.h      Function Pointer Table ABI (plugin system)
├── nand_plugin.c       NAND flash plugin (9 handlers)
├── transport.c / .h    Transport detection, initialization, and RX/TX operations
├── endian_utils.h      Byte-order conversion helpers
└── ld/                 Linker scripts (one per chip + common.ld + nand_plugin.ld)
```

## Build System

The project uses CMake with Ninja:

```
CMakeLists.txt              Root build configuration
├── cmake/esp-targets.cmake Chip definitions, toolchain setup, compiler flags
├── src/CMakeLists.txt      Firmware executable and post-build JSON generation
└── esp-stub-lib/           Submodule with its own CMakeLists.txt
```

The build requires the `-DTARGET_CHIP=<chip>` parameter. The CMake configuration:

1. Validates the target chip.
2. Selects the appropriate cross-compiler prefix (Xtensa or RISC-V).
3. Applies architecture-specific compiler flags.
4. Links with a chip-specific linker script from `src/ld/`.
5. Applies target-local size workarounds where needed, such as `-fdata-sections` only for ESP8266 `miniz_obj`.
6. Runs `tools/elf2json.py` as a post-build step to generate the JSON stub file.

## Linker Scripts

Each chip has a dedicated linker script that defines memory regions:

```
MEMORY {
  iram : org = 0x40028000, len = 0x18000   /* Instruction RAM */
  dram : org = 0x3FFD0000, len = 0x28000   /* Data RAM */
}
INCLUDE common.ld
```

The `common.ld` file defines sections (`.text`, `.bss`, `.data`) and sets the entry point to `esp_main`.

## Plugin System

The stub supports runtime-loadable plugins that extend the command set. Plugins are dispatched through a **Function Pointer Table (FPT)**: a global array in the base stub's `.data` segment, indexed by opcode (range `0xD5`–`0xEF`). At startup all entries default to `s_plugin_unsupported`. esptool patches the relevant entries and uploads the plugin binary before handing off control.

Command handlers and plugin post-process callbacks receive a `struct cmd_ctx` that includes the selected transport operations. Streaming code should send data and poll ACKs through `ctx->transport`, so plugin code stays independent of whether the host is using SLIP-based UART/USB or raw SDIO.

For chips that support a plugin (currently ESP32-S3 with the NAND plugin), the build uses a **two-pass** process:

1. Build the base stub ELF (Pass 1).
2. `tools/compute_plugin_addrs.py` computes plugin load addresses from the base ELF sizes.
3. Build the plugin ELF linked at those addresses (Pass 2).
4. `tools/elf2json.py` embeds the plugin into the JSON stub file.

For the full guide including how to add a new plugin, see [Plugin System](plugin-system.md).
