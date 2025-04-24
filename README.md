[![pre-commit.ci status](https://results.pre-commit.ci/badge/github/espressif/esp-flasher-stub/master.svg)](https://results.pre-commit.ci/latest/github/espressif/esp-flasher-stub/master)

# esp-flasher-stub

This project is experimental and not yet ready for production use.

The project's goal is to replace the legacy [flasher stub of esptool](https://github.com/espressif/esptool-legacy-flasher-stub/) in the near future.

# Build Dependencies

### Submodules

The project depends on [esp-stub-lib](https://github.com/espressif/esp-stub-lib/) in the form of git submodule. Don't forget to get/update the submodule as well before building:

```sh
git submodule update --init --recursive
```

### Toolchains

You will need the following toolchains set up and available in your PATH:
1. [`xtensa-lx106-elf-*`](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html#setup-toolchain)
2. [`xtensa-*-elf-*`](https://github.com/espressif/crosstool-NG)
3. [`riscv32-esp-elf-*`](https://github.com/espressif/crosstool-NG)

There is a convenience script for AMD64 Linux machines to download and install them into the `toolchains` directory:
```sh
mkdir -p toolchains
cd toolchains
../tools/setup_toolchains.sh
```

Then run the following export script in every terminal where the project is used:
```sh
. ./tools/export_toolchains.sh
```

### Esptool

[Esptool](https://github.com/espressif/esptool/) is needed for ELF file analysis. Run the following commands in order to install it:
```sh
python -m venv venv
source venv/bin/activate
pip install esptool
```

Run the following command in every terminal where the project is used:
```sh
source venv/bin/activate
```

# How to Build

### Build for one selected chip target

```sh
mkdir -p build
cmake . -B build -G Ninja -DTARGET_CHIP=esp32s2   # Replace with your desired chip, e.g. esp32, esp8266
ninja -C build
```

### Build for all supported chip targets

```sh
./tools/build_all.sh
```

# How To Use With Esptool

1. Install esptool in [development mode](https://docs.espressif.com/projects/esptool/en/latest/esp32/contributing.html#development-setup).
2. Obtain the flasher stub binaries as JSON files either from the [releases page](https://github.com/espressif/esp-flasher-stub) or from the artifacts of your pull request.
3. Replace the esptool's JSONs files in the `esptool/targets/stub_flasher` directory with the obtained JSON files.

# Contributing

Please install the [pre-commit](https://pre-commit.com/) hooks to ensure that your commits are properly formatted:

```bash
pip install pre-commit
pre-commit install -t pre-commit -t commit-msg
```

# How To Release (For Maintainers Only)

```bash
pip install commitizen
git fetch
git checkout -b update/release_v1.1.0
git reset --hard origin/master
cz bump
git push -u
git push --tags
```
Create a pull request and edit the automatically created draft [release notes](https://github.com/espressif/esp-flasher-stub/releases).

# License

This document and the attached source code are released as Free Software under either the [Apache License Version 2](LICENSE-APACHE) or [MIT License](LICENSE-MIT) at your option.
