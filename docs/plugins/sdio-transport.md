# SDIO Transport

The SDIO transport lets a host talk to the stub over the SDIO download interface. It is a [transport plugin](../plugin-system.md#transport-plugins), so the base stub does not carry the SDIO code; the host loads the plugin only when it uses SDIO.

## Supported MCUs

ESP32-C5, ESP32-C6 and ESP32-C61: the chips whose SDIO slave driver exists in esp-stub-lib. The regular build embeds the plugin as `plugins.sdio` in `<target>.json` for these chips. ESP32 and ESP8266 do not support the plugin system.

## Protocol Overview

SDIO carries raw command frames without SLIP framing, like SPI. The receive DMA writes each host frame into the base stub's frame buffer, which the plugin reaches through the exported base `frame_buffer_*` functions. Frames in both directions have a maximum size of `SDIO_DMA_DESC_MAX_LEN` (0x3FFF bytes), the limit of the SDIO DMA descriptor size and length fields.

The base stub raises the CPU clock before it initializes the plugin, on every supported chip.

## Loading the Plugin

To start the stub over SDIO, the host:

1. Writes `plugins.sdio.entry` into a slot of `plugin_start[]` in the base `.data`, at `plugin_start_offset` (32-bit little-endian).
2. Uploads the base stub `.text` and the patched `.data`.
3. Uploads `plugins.sdio.text` at `plugins.sdio.text_start` and `plugins.sdio.data` at `plugins.sdio.data_start`.
4. Starts the stub at `entry`.

At boot the stub calls `sdio_transport_plugin_start()`, which zeroes the plugin's `.bss` and fills the transport hook. The stub then detects the SDIO link through the plugin before it probes the built-in transports, and sends the `OHAI` greeting over SDIO.
