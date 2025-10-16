# ESP Flasher Stub Unit Tests

This directory contains unit tests for the ESP flasher stub project using the **Unity** test framework and **CMock** for mocking dependencies.

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Running Tests](#running-tests)
- [Working with Mocks](#working-with-mocks)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)

## Overview

The unit test suite uses:
- **[Unity](https://github.com/ThrowTheSwitch/Unity)**: C unit testing framework
- **[CMock](https://github.com/ThrowTheSwitch/CMock)**: Mock generation
## Prerequisites

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install gcc cmake make ruby ninja-build
```

**Arch Linux:**
```bash
sudo pacman -S gcc cmake make ruby ninja
```

**macOS:**
```bash
brew install gcc cmake make ruby ninja
```

## Quick Start

### Host Tests (Recommended for Development)

Host tests run natively on your development machine with full mocking support:

1. **Initialize submodules** (if not already done):
   ```bash
   cd /path/to/esp-flasher-stub
   git submodule update --init --recursive
   ```

2. **Run host tests**:
   ```bash
   cd unittests/host
   ./run-tests.sh
   ```

### Target Tests (Cross-Compiled)

Target tests run Unity tests on actual ESP hardware with enhanced failure parsing and automatic test result analysis.

1. **Install ESP toolchain** for your target chip (see [Prerequisites](#prerequisites))

2. **Run hardware tests** (default behavior):
   ```bash
   cd unittests/target
   ./run-tests.sh esp32s3    # Builds + runs tests on hardware
   ```

3. **Build-only mode** (skip hardware testing):
   ```bash
   ./run-tests.sh esp32s3 --build-only
   ```

## Project Structure

```
unittests/
├── README.md                    # This file
├── scripts/                     # Support scripts
│   └── generate_mocks.sh        # Mock generation script
├── host/                        # Host-based unit tests
│   ├── CMakeLists.txt           # Host test-specific CMake config
│   ├── run-tests.sh             # Host test runner
│   ├── cmock_config.yml         # CMock configuration
│   └── TestSlip.c               # Example: SLIP protocol tests
├── target/                      # Target cross-compiled tests
│   ├── CMakeLists.txt           # Target test-specific CMake config
│   ├── run-tests.sh             # Target test runner (optimized)
│   ├── load-test.py             # Hardware test loader with Unity parsing
│   ├── minimal_system.c         # Minimal system for target tests
│   ├── TestTargetExample.c      # Example: Target example tests
│   └── build/                   # Target test artifacts (generated)
├── Unity/                       # Unity test framework (submodule)
└── CMock/                       # CMock framework (submodule)
```

## Working with Mocks

### Understanding Mocks

Mocks are automatically generated from header files and allow you to:
- **Control return values** of external functions
- **Verify function calls** were made with expected parameters
- **Simulate error conditions** for testing error handling

### Mock Expectations

```c
// Expect function to be called once with specific parameters and return value
my_function_ExpectAndReturn(param1, param2, return_value);

// Expect function to be called, ignore parameters
my_function_Expect();

// Expect function to be called with specific parameter, ignore others
my_function_ExpectWithArray(param1, array_ptr, array_length);

// Set return value for any call
my_function_IgnoreAndReturn(return_value);
```

### Adding New Mocks

Currently, the mock generation script only generates mocks for `rom_wrappers.h`. To add new mocks:

1. **Update the mock generation script** (`scripts/generate_mocks.sh`):
   ```bash
   # Add new mock generation after the existing rom_wrappers mock
   print_status "Generating mock for your_header..."
   ruby CMock/scripts/create_mock.rb ../path/to/your_header.h
   ```

2. **Update host/CMakeLists.txt** to include the new mock:
   ```cmake
   # Update the mock generation command OUTPUT
   add_custom_command(
       OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.h
              ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.c
              ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_your_header.h  # Add this
              ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_your_header.c  # Add this
       # ... rest of command
   )

   # Update the add_host_test function to include the new mock
   add_executable(${TEST_NAME}
       ${HOST_TEST_SOURCES}
       ${UNITY_SOURCES}
       ${CMOCK_SOURCES}
       ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.c
       ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_your_header.c  # Add this
   )
   ```

3. **Include in test files**:
   ```c
   #include "mock_your_header.h"
   ```

## Configuration

### CMock Configuration

Edit `host/cmock_config.yml` to customize mock generation:

```yaml
:cmock:
  :verbosity: 2                    # 0=errors, 1=warnings, 2=normal, 3=verbose
  :mock_path: './build/mocks'      # Where to generate mocks (relative to build directory)
  :fail_on_unexpected_calls: true # Fail on unexpected function calls
  :when_ptr: :compare_data         # How to handle pointer arguments
```

### Unity Configuration

Unity behavior can be configured by defining macros in your test files or CMakeLists.txt:

```c
#define UNITY_INCLUDE_DOUBLE        // Enable double precision support
#define UNITY_EXCLUDE_FLOAT         // Disable float support
#define UNITY_OUTPUT_COLOR          // Enable colored output
```

## Troubleshooting

### Debug Mode

Enable verbose output for debugging:

```bash
# Verbose CMock
export CMOCK_VERBOSITY=3

# Verbose CTest
cd host/build  # or target/build for target tests
ctest --verbose --debug

# Manual test execution with debug
./TestSlip --verbose
```
