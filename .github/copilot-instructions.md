# ESP Flasher Stub - Copilot Coding Agent Instructions

## Repository Overview

**esp-flasher-stub** is an embedded firmware project that builds flasher stub binaries for Espressif ESP chips. These stubs are small firmware programs that run on ESP devices to facilitate flash programming via esptool.

**Project Status**: Experimental - not yet ready for production use. This project aims to replace the [legacy flasher stub in esptool](https://github.com/espressif/esptool-legacy-flasher-stub/) with a modern, maintainable implementation using CMake and the esp-stub-lib library.

**Project Type**: Embedded C firmware with CMake build system
**Languages**: C (firmware), Python (build tools, tests)
**Size**: Small-medium (~20 C source files, ~1000 lines main codebase)
**Target Chips**: esp32, esp32s2, esp32s3, esp32c2, esp32c3, esp32c5, esp32c6, esp32c61, esp32h2, esp32p4, esp8266
**Build Time**: ~5-10 seconds per chip, ~2-3 minutes for all chips

## Critical Setup Steps (ALWAYS Follow This Order)

### 1. Initialize Submodules (REQUIRED - Do This First)

**ALWAYS** run this before any build operation:
```bash
git submodule update --init --recursive
```

This initializes three submodules:
- `esp-stub-lib/` - Core library for ESP stub functionality (REQUIRED for build)
- `unittests/CMock/` - Mocking framework for tests
- `unittests/Unity/` - Unit testing framework

**Build will fail** without submodules initialized.

### 2. Set Up Python Virtual Environment (REQUIRED)

**ALWAYS** use a virtual environment to avoid dependency conflicts:
```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
pip install esptool
```

**CRITICAL**: The `esptool` package is **required** for the build process (used by `tools/elf2json.py` to convert ELF to JSON). The build will fail at the post-build step without it.

**ALWAYS** activate the venv in every terminal session:
```bash
source venv/bin/activate
```

### 3. Install ESP Toolchains (For Firmware Build Only)

For AMD64 Linux machines, use the convenience script:
```bash
mkdir -p toolchains
cd toolchains
../tools/setup_toolchains.sh
cd ..
```

This downloads and extracts three toolchains (takes ~2-5 minutes):
1. `xtensa-esp-elf-15.1.0_20250607` - For esp32, esp32s2, esp32s3 (~120MB download)
2. `xtensa-lx106-elf-gcc8_4_0-esp-2020r3` - For esp8266 (~100MB download)
3. `riscv32-esp-elf-15.1.0_20250607` - For esp32c2, esp32c3, esp32c5, esp32c6, esp32c61, esp32h2, esp32p4 (~255MB download)

**Note**: Network issues may cause partial downloads. If esp8266 toolchain fails, you can still build other chips.

**ALWAYS** export toolchains before building firmware:
```bash
source ./tools/export_toolchains.sh
```

This script adds toolchain bin directories to PATH. **Must be run in every new terminal session** before building.

## Build Commands

### Host Unit Tests (No Toolchains Required)

**Recommended first step** to validate setup without needing ESP toolchains:
```bash
cd unittests/host
./run-tests.sh
```

**Build Time**: ~10-20 seconds (includes CMake config, mock generation, ninja build, CTest run)
**Dependencies**: gcc, cmake, ninja-build, ruby

This runs native unit tests with CMock/Unity frameworks and validates core functionality (SLIP protocol, etc.).

### Build Firmware for Single Chip

**ALWAYS** ensure venv is activated and toolchains exported first:
```bash
source venv/bin/activate
source ./tools/export_toolchains.sh
mkdir -p build
cmake . -B build -G Ninja -DTARGET_CHIP=esp32s2  # Replace with desired chip
ninja -C build
```

**Build Time**: ~5-10 seconds per chip
**Output Files**:
- `build/src/stub-{chip}.elf` - ELF binary
- `build/{chip}.json` - JSON file with stub data (used by esptool)

**Common Chips**: esp32, esp32s2, esp32s3, esp32c3, esp32c6, esp8266

**Note**: The `--fresh` flag in `tools/build_all_chips.sh` ensures clean builds. When building single chips manually, delete the build directory if switching between chips.

### Build Firmware for All Chips

**ALWAYS** ensure venv is activated and toolchains exported first:
```bash
source venv/bin/activate
source ./tools/export_toolchains.sh
./tools/build_all_chips.sh
```

**Build Time**: ~2-3 minutes for all 11 chips
**Output**: Creates `build-{chip}/` directories for each chip with ELF and JSON files

This script:
1. Iterates through all supported chips
2. Creates separate build directories for each chip
3. Runs CMake with `--fresh` flag for clean builds
4. Builds with Ninja
5. Runs `tools/elf2json.py` post-build to generate JSON

## Pre-commit Hooks and Validation

### Install Pre-commit (One-time Setup)

```bash
source venv/bin/activate
pip install pre-commit
pre-commit install -t pre-commit -t commit-msg
```

### Pre-commit Checks (Run Automatically on Commit)

The `.pre-commit-config.yaml` configures these checks:
1. **codespell** - Spell checking
2. **check-copyright** - Validates copyright headers (Apache-2.0 OR MIT)
3. **trailing-whitespace** - Removes trailing whitespace
4. **end-of-file-fixer** - Ensures files end with newline
5. **check-executables-have-shebangs** - Validates shell scripts
6. **mixed-line-ending** - Enforces LF line endings
7. **double-quote-string-fixer** - Fixes string quotes
8. **ruff** - Python linter and formatter
9. **mypy** - Python type checking
10. **yamlfix** - YAML formatting
11. **conventional-precommit-linter** - Commit message format validation
12. **astyle_py** - C code formatting (astyle version 3.4.7)

### Manual Pre-commit Run

To run pre-commit on all files:
```bash
source venv/bin/activate
pre-commit run --all-files
```

**Note**: Astyle formatting is configured in `.astyle-rules.yml`. Submodules and libraries (like `src/miniz.*`) are excluded from checks.

### Copyright Headers

**ALWAYS** include this header in new C files:
```c
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
```

For Python files:
```python
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT
```

## CI/CD Workflows

### GitHub Actions Workflows

1. **Build and Release** (`.github/workflows/build_and_release.yml`)
   - Runs on: push, pull_request
   - Steps:
     1. Checkout with recursive submodules
     2. Set up Python 3.13
     3. Install esptool via pip
     4. Install toolchains via `tools/setup_toolchains.sh`
     5. Export toolchains and build all chips via `tools/build_all_chips.sh`
     6. Upload JSON artifacts
     7. Create GitHub release (on tags only)

2. **Host Tests** (`.github/workflows/host_tests.yml`)
   - Runs on: push
   - Installs: build-essential, cmake, ninja-build, ruby
   - Runs: `unittests/host/run-tests.sh`
   - Uses CMake build caching

3. **DangerJS** (`.github/workflows/dangerjs.yml`)
   - Runs on: pull_request_target
   - Validates PR style and conventions

4. **Jira Integration** (`.github/workflows/jira.yml`)
   - Syncs with Jira issues

### Pre-commit.ci

The repository uses pre-commit.ci for automated PR checks. It runs all pre-commit hooks on PRs and auto-fixes issues when possible.

## Project Layout and Architecture

### Root Directory Files

- `CMakeLists.txt` - Main CMake configuration (requires TARGET_CHIP parameter)
- `pyproject.toml` - Python tool configuration (ruff, mypy, yamlfix, commitizen, codespell)
- `.pre-commit-config.yaml` - Pre-commit hook configuration
- `.astyle-rules.yml` - C code style configuration
- `.check_copyright_config.yaml` - Copyright header validation config
- `.gitignore` - Excludes: build*/, toolchains/, venv/, __pycache__
- `README.md` - Main documentation
- `CHANGELOG.md` - Release notes

### Source Code Structure

**`src/`** - Main firmware source
- `main.c` - Entry point (`esp_main()` function), BSS initialization, flash init, SLIP protocol loop
- `slip.c/h` - SLIP protocol implementation for framing data over UART
- `command_handler.c/h` - Command parsing and dispatch
- `commands.h` - Command ID definitions
- `transport.c/h` - UART/USB-JTAG transport layer
- `miniz.c/h` - ZIP compression library (esp8266 only, third-party library with upstream TODOs)
- `ld/` - Linker scripts for each chip (e.g., `esp32s2.ld`, `esp8266.ld`)

**`cmake/`**
- `esp-targets.cmake` - ESP chip definitions, toolchain configuration functions, target-specific compiler flags

**`tools/`**
- `build_all_chips.sh` - Builds firmware for all supported chips
- `setup_toolchains.sh` - Downloads and extracts toolchains
- `export_toolchains.sh` - Adds toolchains to PATH (must be sourced)
- `elf2json.py` - Converts ELF binaries to JSON format for esptool
- `install_all_chips.sh` - (Not commonly used)

**`esp-stub-lib/`** (submodule)
- Core library providing flash operations, UART, security, memory utilities
- Chip-specific implementations in `src/target/{chip}/`
- Common functionality in `src/target/common/`

**`unittests/`**
- `host/` - Native unit tests (run on build machine with mocks)
  - `run-tests.sh` - Test runner script
  - `TestSlip.c` - SLIP protocol tests
  - `cmock_config.yml` - CMock configuration
  - `CMakeLists.txt` - Host test build configuration
- `target/` - Cross-compiled tests (run on actual hardware)
- `Unity/` (submodule) - Unit testing framework
- `CMock/` (submodule) - Mocking framework
- `README.md` - Detailed testing documentation

### Key Dependencies and Relationships

- **esp-stub-lib dependency**: Main source depends on esp-stub-lib for flash, UART, and chip-specific operations
- **CMake target configuration**: `cmake/esp-targets.cmake` determines toolchain and compiler flags based on TARGET_CHIP
- **Linker scripts**: Each chip has a specific linker script in `src/ld/{chip}.ld`
- **Post-build processing**: `tools/elf2json.py` requires esptool and is called automatically after build
- **Chip-specific code**: esp8266 includes miniz library for compression; other chips don't need it

## Common Issues and Workarounds

### Issue: Build fails with "TARGET_CHIP not set"
**Solution**: Always specify `-DTARGET_CHIP={chip}` when running cmake
```bash
cmake . -B build -G Ninja -DTARGET_CHIP=esp32s2
```

### Issue: Build fails with "esptool not found"
**Solution**: Activate venv and install esptool
```bash
source venv/bin/activate
pip install esptool
```

### Issue: Toolchain compiler not found
**Solution**: Export toolchains before building
```bash
source ./tools/export_toolchains.sh
```

### Issue: Submodule directories empty
**Solution**: Initialize submodules
```bash
git submodule update --init --recursive
```

### Issue: CMake cache from previous chip build
**Solution**: Use `--fresh` flag or delete build directory when switching chips
```bash
cmake . -B build -G Ninja -DTARGET_CHIP=esp32s3 --fresh
# OR
rm -rf build && cmake . -B build -G Ninja -DTARGET_CHIP=esp32s3
```

### Issue: Host tests fail to build
**Solution**: Ensure gcc, cmake, ninja-build, and ruby are installed
```bash
sudo apt-get install build-essential cmake ninja-build ruby  # Ubuntu/Debian
```

### Issue: Pre-commit astyle check fails
**Solution**: Run astyle with correct configuration
```bash
source venv/bin/activate
pre-commit run astyle_py --all-files
```

### Known TODOs in Codebase

The following TODOs exist in the codebase:

**src/command_handler.c** (lines 47, 54, 71, 104, 110):
- Flash initialization improvements needed
- Cleanup procedures for flash operations
- Reboot command implementation
- Delay consideration for flash operations
- WDT reset implementation for system reset

**src/transport.c** (line 40):
- Proper fix needed for zero-length packet handling

**src/miniz.c** (multiple locations):
- Various compression optimizations and improvements
- **Note**: This is a third-party library - these are upstream issues and should NOT be modified locally

When working on these areas, consider whether the TODO is actionable or requires broader design decisions.

## Validation Steps Before PR Submission

1. **Run host tests**:
   ```bash
   cd unittests/host && ./run-tests.sh && cd ../..
   ```

2. **Build firmware for target chip**:
   ```bash
   source venv/bin/activate
   source ./tools/export_toolchains.sh
   cmake . -B build -G Ninja -DTARGET_CHIP=esp32s2 --fresh
   ninja -C build
   ```

3. **Run pre-commit checks**:
   ```bash
   source venv/bin/activate
   pre-commit run --all-files
   ```

4. **Verify JSON output exists**:
   ```bash
   ls -la build/*.json
   ```

## Tips for Efficient Development

1. **Trust these instructions**: They are validated and tested. Only search for additional information if something fails or is unclear.

2. **Always activate venv first**: Most commands require esptool or other Python packages from the venv.

3. **Use build_all_chips.sh for comprehensive testing**: Before submitting PRs with firmware changes, run `./tools/build_all_chips.sh` to ensure all chips build.

4. **Host tests don't need toolchains**: Run `unittests/host/run-tests.sh` first to validate logic changes without setting up toolchains.

5. **Check CI workflow logs**: If GitHub Actions fail, check `.github/workflows/build_and_release.yml` and `host_tests.yml` to understand what steps the CI runs.

6. **Linker script changes are chip-specific**: Each chip has its own linker script in `src/ld/`. Changes to one chip's linker script don't affect others.

7. **The `--fresh` flag matters**: When building different chips, use `--fresh` or separate build directories to avoid CMake cache issues.

8. **Separate build directories for each chip**: The `build_all_chips.sh` script creates `build-{chip}/` directories. When building manually, use the same pattern or clean between builds.

9. **Virtual environment prevents build issues**: **ALWAYS** use a virtual environment. System-wide Python packages can cause version conflicts that break the build.

10. **Pre-commit.ci runs automatically**: PRs will have pre-commit checks run by pre-commit.ci. Install and run pre-commit locally to catch issues before pushing.
