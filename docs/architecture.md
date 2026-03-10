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
├── transport.c / .h    Transport detection and initialization
├── endian_utils.h      Byte-order conversion helpers
└── ld/                 Linker scripts (one per chip + common.ld)
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
