# SPI Transport

The SPI transport lets [esp-serial-flasher](https://github.com/espressif/esp-serial-flasher) talk to the stub over the SPI download interface. It is a [transport plugin](../plugin-system.md#transport-plugins), so the base stub does not carry the SPI code; the host loads the plugin only when it uses SPI.

## Supported MCUs

ESP32-C2, ESP32-C3, ESP32-P4 (including revision 1), ESP32-S2 and ESP32-S3: the chips whose SPI slave driver exists in esp-stub-lib. The regular build embeds the plugin as `plugins.spi` in `<target>.json` for these chips. ESP32 and ESP8266 do not support the plugin system.

## Protocol Overview

SPI carries raw command frames without SLIP framing, like SDIO. The receive DMA writes each host frame into the base stub's frame buffer, which the plugin uses through the base stub's `frame_buffer_*` functions. Stub-to-host frames have a maximum size of 4092 bytes: the transmit path uses one DMA descriptor with a 12-bit size field and a 4-byte-aligned data buffer. Host-to-stub frames are not subject to this limit, because the receive path chains multiple DMA descriptors.

## Loading the Plugin

To start the stub over SPI, the host:

1. Writes `text_start + transport.is_active` and `text_start + transport.init` of `plugins.spi` into the base `.data` at `transport_plugin_offset` and `transport_plugin_offset + 4` (32-bit little-endian).
2. Uploads the base stub `.text` and the patched `.data`.
3. Zeroes `bss_size` bytes at `plugins.spi.bss_start`. On ESP32-S3 the SPI plugin is stacked after the NAND plugin, so its `.bss` does not start right after the base `.data`.
4. Uploads `plugins.spi.text` at `plugins.spi.text_start`.
5. Starts the stub at `entry`.

The stub then detects the SPI link through the plugin before it probes the built-in transports, and sends the `OHAI` greeting over SPI.
