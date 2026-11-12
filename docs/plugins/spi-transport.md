# SPI Transport

The SPI transport lets [esp-serial-flasher](https://github.com/espressif/esp-serial-flasher) talk to the stub over the SPI download interface. It is a [transport plugin](../plugin-system.md#transport-plugins), so the base stub does not carry the SPI code; the host loads the plugin only when it uses SPI.

## Supported MCUs

ESP32-C2, ESP32-C3, ESP32-P4 (including revision 1), ESP32-S2 and ESP32-S3: the chips whose SPI slave driver exists in esp-stub-lib. The regular build embeds the plugin as `plugins.spi` in `<target>.json` for these chips. ESP32 and ESP8266 do not support the plugin system.

## Protocol Overview

SPI carries raw command frames without SLIP framing, like SDIO. The receive DMA writes each host frame into the base stub's frame buffer, which the plugin reaches through the exported base `frame_buffer_*` functions. Stub-to-host frames have a maximum size of 4092 bytes: the transmit path uses one DMA descriptor with a 12-bit size field and a 4-byte-aligned data buffer. Host-to-stub frames are not subject to this limit, because the receive path chains multiple DMA descriptors.

## Loading the Plugin

To start the stub over SPI, the host:

1. Writes `plugins.spi.entry` into a slot of `plugin_start[]` in the base `.data`, at `plugin_start_offset` (32-bit little-endian).
2. Uploads the base stub `.text` and the patched `.data`.
3. Uploads `plugins.spi.text` at `plugins.spi.text_start` and `plugins.spi.data` at `plugins.spi.data_start`.
4. Starts the stub at `entry`.

At boot the stub calls `spi_transport_plugin_start()`, which zeroes the plugin's `.bss` and fills the transport hook. The stub then detects the SPI link through the plugin before it probes the built-in transports, and sends the `OHAI` greeting over SPI.
