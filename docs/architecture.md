# Architecture

## Overview

The esp-flasher-stub firmware is a bare-metal C application that runs on ESP chip RAM. It communicates with a host tool (esptool) over a serial transport using the [SLIP](https://datatracker.ietf.org/doc/html/rfc1055) protocol to perform flash programming and other operations.

```
┌─────────────────────────────────────────────────────────┐
│  Host (esptool)                                         │
│  Sends SLIP-framed commands over UART / USB             │
└───────────────────────┬─────────────────────────────────┘
                        │
            UART / USB-Serial-JTAG / USB-OTG
                        │
┌───────────────────────▼─────────────────────────────────┐
│  ESP Flasher Stub (runs in chip RAM)                    │
│                                                         │
│  ┌──────────┐  ┌───────────────┐  ┌──────────────────┐  │
│  │Transport │→ │ SLIP Protocol │→ │ Command Handler  │  │
│  │  Layer   │  │   Decoder     │  │   & Dispatch     │  │
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
├── slip.c / slip.h     SLIP protocol framing (send and receive)
├── command_handler.c   Command parsing, dispatch, and response
├── command_handler.h   Public API and buffer size definitions
├── commands.h          Command IDs and response codes
├── plugin_table.h      Function Pointer Table ABI (plugin system)
├── nand_plugin.c       NAND flash plugin (9 handlers, ESP32-S3 only)
├── transport.c / .h    Transport detection and initialization
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
5. Runs `tools/elf2json.py` as a post-build step to generate the JSON stub file.

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

For chips that support a plugin (currently ESP32-S3 with the NAND plugin), the build uses a **two-pass** process:

1. Build the base stub ELF (Pass 1).
2. `tools/compute_plugin_addrs.py` computes plugin load addresses from the base ELF sizes.
3. Build the plugin ELF linked at those addresses (Pass 2).
4. `tools/elf2json.py` embeds the plugin into the JSON stub file.

For the full guide including how to add a new plugin, see [Plugin System](plugin-system.md).
