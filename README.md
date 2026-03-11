[![pre-commit.ci status](https://results.pre-commit.ci/badge/github/espressif/esp-flasher-stub/master.svg)](https://results.pre-commit.ci/latest/github/espressif/esp-flasher-stub/master)

# ESP Flasher Stub

ESP Flasher Stub is a set of small firmware programs (stubs) that run on Espressif ESP chips to enable fast and reliable flash programming via [esptool](https://github.com/espressif/esptool/). When esptool connects to an ESP chip, it uploads the flasher stub into the chip's RAM. The stub then takes over communication, providing faster flash operations and additional features compared to the chip's built-in ROM bootloader.

This project aims to replace the legacy [flasher stub of esptool](https://github.com/espressif/esptool-legacy-flasher-stub/) in the near future.

> [!IMPORTANT]
> This project is experimental and not yet ready for production use.

## Documentation

- [Architecture](docs/architecture.md) - Firmware architecture, source code structure, modules, and build system internals
- [Development Guide](docs/development-guide.md) - Contributing guidelines, testing, CI/CD, and release process

## Supported Chips

| Architecture | Chips |
|---|---|
| Xtensa | ESP32, ESP32-S2, ESP32-S3 |
| RISC-V | ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-C61, ESP32-H2, ESP32-H4, ESP32-P4, ESP32-P4 (rev1) |
| Xtensa (LX106) | ESP8266 |

## Build Dependencies

### Submodules

The project depends on [esp-stub-lib](https://github.com/espressif/esp-stub-lib/) as a git submodule. Make sure to initialize it before building:

```sh
git submodule update --init --recursive
```

### Toolchains

The following toolchains must be set up and available in your PATH:
1. [`xtensa-lx106-elf-*`](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html#setup-toolchain)
2. [`xtensa-*-elf-*`](https://github.com/espressif/crosstool-NG)
3. [`riscv32-esp-elf-*`](https://github.com/espressif/crosstool-NG)

There is a convenience script for AMD64 Linux machines to download and install them into the `toolchains` directory:
```sh
mkdir -p toolchains
cd toolchains
../tools/setup_toolchains.sh
```

Then source the export script in every terminal where the project is used:
```sh
. ./tools/export_toolchains.sh
```

### Esptool

[Esptool](https://github.com/espressif/esptool/) is needed for ELF file analysis. Install it in a virtual environment:

```sh
python -m venv venv
source venv/bin/activate
pip install esptool
```

Activate the virtual environment in every terminal where the project is used:
```sh
source venv/bin/activate
```

## How to Build

### Build for One Selected Chip Target

```sh
mkdir -p build
cmake . -B build -G Ninja -DTARGET_CHIP=esp32s2   # Replace with your desired chip, e.g. esp32, esp8266
ninja -C build
```

### Build for All Supported Chip Targets

```sh
./tools/build_all_chips.sh
```

## How to Use with Esptool

1. Install esptool in [development mode](https://docs.espressif.com/projects/esptool/en/latest/esp32/contributing.html#development-setup).
2. Obtain the flasher stub binaries as JSON files either from the [releases page](https://github.com/espressif/esp-flasher-stub/releases) or from the artifacts of your pull request.
3. Replace the esptool JSON files in the `esptool/targets/stub_flasher` directory with the obtained JSON files.

    Example copy command (adjust the path to your esptool directory):

    ```sh
    cp build-*/*.json ~/esptool/esptool/targets/stub_flasher/1/
    ```

## How It Works

The flasher stub operates through upload, initialization, handshake (`OHAI` over SLIP), and a command loop that handles flash, memory, register, and SPI operations. For details, see the [Architecture](docs/architecture.md) document.

## Contributing

See the [Contributing](docs/development-guide.md#contributing) section of the Development Guide for code style, pre-commit hooks, copyright headers, and the pull request checklist.

## How to Release (for Maintainers Only)

See the [Releasing](docs/development-guide.md#releasing-maintainers-only) section of the Development Guide.

## License

This document and the attached source code are released as Free Software under either the [Apache License Version 2](LICENSE-APACHE) or [MIT License](LICENSE-MIT) at your option.
