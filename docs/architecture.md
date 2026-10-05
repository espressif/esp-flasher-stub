# Architecture

## Overview

The esp-flasher-stub firmware is a bare-metal C application that runs on ESP chip RAM. It communicates with a host tool (esptool) through a transport abstraction to perform flash programming and other operations. UART, USB-Serial-JTAG, and USB-OTG carry [SLIP](https://datatracker.ietf.org/doc/html/rfc1055)-framed commands; SDIO and SPI carry raw command frames. SPI is optional and is built only with `STUB_SPI_ENABLED` (see [Build System](#build-system)).

```
┌─────────────────────────────────────────────────────────┐
│  Host (esptool / esp-serial-flasher)                    │
│  Sends command frames over UART / USB / SDIO / SPI      │
└───────────────────────┬─────────────────────────────────┘
                        │
         UART / USB-Serial-JTAG / USB-OTG / SDIO / SPI
                        │
┌───────────────────────▼─────────────────────────────────┐
│  ESP Flasher Stub (runs in chip RAM)                    │
│                                                         │
│  ┌──────────┐  ┌───────────────┐  ┌──────────────────┐  │
│  │Transport │→ │ Frame Buffer  │→ │ Command Handler  │  │
│  │  Layer   │  │ + SLIP/raw    │  │   & Dispatch     │  │
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

The optional `-DSTUB_SPI_ENABLED=ON` parameter builds in the SPI transport by defining `STUB_SPI_ENABLED` for the firmware sources and names the output `<target>-spi.json`. It is off by default: only esp-serial-flasher uses SPI, and the transport adds about 1.1–1.3 KB to the stub. The SPI slave driver in esp-stub-lib exists for ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3 and ESP32-P4. On other chips the transport is never detected.

SPI slave-to-host frames have a maximum size of 4092 bytes. The transmit path uses one DMA descriptor with a 12-bit size field and a 4-byte-aligned data buffer. The receive path is not subject to this per-frame limit because it chains multiple DMA descriptors for host-to-stub frames.

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

For chips that support a plugin (currently ESP32-S3 with the NAND plugin), the plugin's load address is derived from the base stub, so the build proceeds as a dependency chain:

1. Build the base stub ELF.
2. `tools/compute_plugin_addrs.py` reads the base ELF sizes and emits a linker fragment with the plugin load addresses.
3. Build the plugin ELF, which pulls in that fragment via `-T` and relinks automatically whenever the base stub changes.
4. `tools/elf2json.py` embeds the plugin into the JSON stub file.

For the full guide including how to add a new plugin, see [Plugin System](plugin-system.md).
