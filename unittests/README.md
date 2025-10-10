# ESP Flasher Stub Unit Tests

This directory contains unit tests for the ESP flasher stub project using the **Unity** test framework and **CMock** for mocking dependencies.

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Running Tests](#running-tests)
- [Adding New Tests](#adding-new-tests)
- [Working with Mocks](#working-with-mocks)
- [Build System](#build-system)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)

## Overview

The unit test suite uses:
- **[Unity](https://github.com/ThrowTheSwitch/Unity)**: C unit testing framework
- **[CMock](https://github.com/ThrowTheSwitch/CMock)**: Mock generation framework
- **CMake**: Build system with Ninja (primary) and Make (fallback) support

## Prerequisites

### Required Tools
- **gcc**: C compiler for host-based testing
- **cmake**: Build system (minimum version 3.28)
- **make**: Fallback build tool (required)
- **ruby**: For CMock mock generation
- **ninja**: Primary build tool (optional but recommended for faster builds)
- **esptool**: For generating binary files from ELF files for target tests

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install gcc cmake make ruby ninja-build
# Use virtual environment for esptool to avoid conflicts
python -m venv venv
source venv/bin/activate
pip install esptool
```

**Arch Linux:**
```bash
sudo pacman -S gcc cmake make ruby ninja
# Use virtual environment for esptool to avoid conflicts
python -m venv venv
source venv/bin/activate
pip install esptool
```

**macOS:**
```bash
brew install gcc cmake make ruby ninja
# Use virtual environment for esptool to avoid conflicts
python -m venv venv
source venv/bin/activate
pip install esptool
```

**Important**: Remember to activate the virtual environment in every terminal session where you run target tests:
```bash
source venv/bin/activate
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
│   ├── TestTargetFlash.c        # Example: Target flash tests
│   └── build/                   # Target test artifacts (generated)
├── Unity/                       # Unity test framework (submodule)
└── CMock/                       # CMock framework (submodule)
```

## Running Tests

### Host Tests (Fast Development Cycle)
```bash
# Run host tests only
cd host
./run-tests.sh
```

### Target Tests (Hardware Validation)
```bash
# Build + Run hardware tests (default behavior)
cd target
./run-tests.sh esp32s3           # Full pipeline: build + hardware test
./run-tests.sh esp32c3 --port /dev/ttyUSB1  # Custom serial port
./run-tests.sh esp32 --timeout 10           # Custom timeout

# Build-only (skip hardware testing)
./run-tests.sh esp32s3 --build-only

# Manual hardware testing (if needed)
python3 load-test.py -c esp32s3 -f build/TestTargetFlash.bin
```

## Adding New Tests

### Step 1: Create Test File

Create a new test file in `host/`:

```c
// host/TestMyModule.c
#include "unity.h"
#include "mock_rom_wrappers.h"  // Include required mocks
#include "my_module.h"          // Include module under test
#include <string.h>

/* Test setup and teardown */
void setUp(void)
{
    // Initialize mocks
    mock_rom_wrappers_Init();
}

void tearDown(void)
{
    // Verify and clean up mocks
    mock_rom_wrappers_Verify();
    mock_rom_wrappers_Destroy();
}

/* Test cases */
void test_my_function_should_return_success(void)
{
    // Arrange
    int expected_result = 0;

    // Act
    int result = my_function();

    // Assert
    TEST_ASSERT_EQUAL(expected_result, result);
}

void test_my_function_with_mock(void)
{
    // Set up mock expectations
    stub_lib_uart_tx_one_char_ExpectAndReturn(0x42, 0);

    // Call function under test
    int result = my_function_that_uses_uart();

    // Assertions happen automatically when mocks are verified
    TEST_ASSERT_EQUAL(0, result);
}

// Test main - Unity will generate this automatically
```

### Step 2: Update CMakeLists.txt

**For Host Tests** (`host/CMakeLists.txt`):
```cmake
# Use the add_host_test helper function
add_host_test(TestMyModule
    SOURCES
        TestMyModule.c
        ../../src/my_module.c  # Relative to host/ directory
)
```

**For Target Tests** (`target/CMakeLists.txt`):
```cmake
# Use the add_target_test helper function
add_target_test(TestMyTargetModule
    SOURCES
        TestMyTargetModule.c
        minimal_system.c  # Usually needed for target tests
        # Add additional source files as needed
)
```

### Step 3: Build and Run

**Host Tests:**
```bash
cd host
./run-tests.sh
```

**Target Tests:**
```bash
cd target
./run-tests.sh esp32    # Build + hardware test
# or
./run-tests.sh esp32 --build-only  # Build only
```

### Hardware Testing Features

Target tests provide comprehensive hardware validation:

- **Automatic binary generation**: ELF → BIN conversion via esptool
- **Unity test execution**: Full test framework on ESP hardware
- **Enhanced failure parsing**: Detailed test names, error messages, file locations
- **Hardware test automation**: Load to RAM + monitor serial output
- **Flexible execution**: Hardware testing by default, build-only optional

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

## Build System
### Build Process

**Host Tests:**
1. **Mock generation** - CMock creates test doubles
2. **CMake + Ninja** - parallel compilation
3. **CTest execution** - automated test running

**Target Tests:**
1. **Cross-compilation** - ESP toolchain builds for target
2. **Binary generation** - esptool creates deployable images
3. **Hardware execution** - load-test.py manages Unity test runs
4. **Result parsing** - detailed failure analysis and reporting

### Manual Build Steps

**Host Tests:**
```bash
# 1. Generate mocks (for host tests) - this is done automatically by CMake
# ./scripts/generate_mocks.sh

# 2. Configure CMake for host tests
cd host
mkdir -p build
cd build
cmake -G Ninja ..

# 3. Build (includes automatic mock generation)
ninja

# 4. Run tests
ctest --verbose
```

**Target Tests:**
```bash
# Automated build + test pipeline
cd target
./run-tests.sh esp32s3

# Manual steps (if needed)
mkdir -p build && cd build
cmake -DESP_TARGET=esp32s3 -G Ninja ..
ninja
# Hardware deployment handled by load-test.py
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

### Common Issues

#### Build Failures

**Issue**: `ninja: command not found`
**Solution**: Install ninja (required for optimized build system)

**Issue**: `ruby: command not found`
**Solution**: Install Ruby for mock generation
```bash
sudo apt install ruby  # Ubuntu/Debian
brew install ruby      # macOS
```

**Issue**: `esptool: command not found`
**Solution**: Install esptool for binary generation using a virtual environment
```bash
python -m venv venv
source venv/bin/activate
pip install esptool
```

**Issue**: Mock generation fails
**Solution**: Check that the header file path is correct in `generate_mocks.sh`

#### Test Failures

**Issue**: Mock expectations not met
```
ERROR: Function mock_function_name called fewer times than expected.
```
**Solution**: Ensure your code actually calls the mocked function, or adjust expectations

**Issue**: Unexpected function calls
```
ERROR: Called mock_function_name with unexpected arguments.
```
**Solution**: Check that your expectations match the actual function calls

#### Runtime Issues

**Issue**: Segmentation fault in tests
**Solution**:
- Check array bounds in test code
- Verify mock setup/teardown is correct
- Use `gdb` to debug: `gdb ./TestName` (from build directory)

**Issue**: Tests hang or timeout
**Solution**:
- Check for infinite loops in test code
- Increase timeout: `./run-tests.sh esp32s3 --timeout 15`
- Check serial port connection for target tests

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

### Getting Help

1. **Check Unity documentation**: `Unity/docs/`
2. **Check CMock documentation**: `CMock/docs/`
3. **View example tests**: Look at `TestSlip.c` for patterns
4. **Enable verbose logging** to see detailed test execution

---

## Example Test Patterns

### Basic Test Structure
```c
void test_function_should_behave_correctly(void)
{
    // Arrange - set up test data
    int input = 42;
    int expected = 84;

    // Act - call function under test
    int result = double_value(input);

    // Assert - verify results
    TEST_ASSERT_EQUAL(expected, result);
}
```

### Testing with Mocks
```c
void test_function_calls_dependency_correctly(void)
{
    // Arrange - set up mock expectations
    dependency_function_ExpectAndReturn(123, 0);  // expect call with param 123, return 0

    // Act - call function that uses the dependency
    int result = function_under_test();

    // Assert - mock verification happens in tearDown()
    TEST_ASSERT_EQUAL(0, result);
}
```

### Testing Error Conditions
```c
void test_function_handles_error_gracefully(void)
{
    // Arrange - make dependency fail
    dependency_function_ExpectAndReturn(123, -1);  // return error code

    // Act & Assert
    int result = function_under_test();
    TEST_ASSERT_EQUAL(-1, result);  // should propagate error
}
```

Happy testing! 🧪✨
