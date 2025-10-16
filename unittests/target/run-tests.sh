#!/bin/bash

# =========================================================================
#   ESP Flasher Stub Target Tests Runner
#   Cross-compiles and runs target tests on ESP hardware (by default)
#   Optional build-only mode with --build-only flag
# =========================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Print usage information
usage() {
    echo "Usage: $0 [TARGET_CHIP] [OPTIONS]"
    echo ""
    echo "TARGET_CHIP: ESP chip to build and test"
    echo "Valid targets: $VALID_TARGETS"
    echo ""
    echo "DESCRIPTION:"
    echo "  Builds and runs Unity tests on ESP hardware (HARDWARE TESTING by default)"
    echo "  Build-only mode is optional and requires --build-only flag"
    echo ""
    echo "OPTIONS:"
    echo "  --build-only          OPTIONAL: Build tests only, skip hardware testing"
    echo "  --port PORT           Serial port for hardware testing (default: /dev/ttyUSB0)"
    echo "  --timeout SECONDS     Test timeout in seconds (default: 10)"
    echo "  --help, -h            Show this help message"
    echo ""
    echo "EXAMPLES:"
    echo "  BUILD + HARDWARE TESTING (default behavior):"
    echo "    $0 esp32s3                         # Build + run tests on /dev/ttyUSB0"
    echo "    $0 esp32c3 --port /dev/ttyUSB1     # Build + run tests on specific port"
    echo "    $0 esp32 --timeout 15              # Build + run with custom timeout"
    echo ""
    echo "  BUILD ONLY (skip hardware testing):"
    echo "    $0 esp32s3 --build-only           # Build Unity tests for ESP32-S3"
    echo "    $0 esp32c3 --build-only           # Build Unity tests for ESP32-C3"
}

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

print_status "ESP Flasher Stub Target Tests Runner"
print_status "Working directory: $(pwd)"

# Parse command line arguments
TARGET_CHIP=""
BUILD_ONLY=""
TEST_PORT="/dev/ttyUSB0"
TEST_TIMEOUT="5"

while [[ $# -gt 0 ]]; do
    case $1 in
        --build-only)
            BUILD_ONLY="yes"
            shift
            ;;
        --port)
            if [[ $# -gt 1 ]]; then
                TEST_PORT="$2"
                shift 2
            else
                print_error "--port requires a value"
                exit 1
            fi
            ;;
        --timeout)
            if [[ $# -gt 1 ]]; then
                TEST_TIMEOUT="$2"
                shift 2
            else
                print_error "--timeout requires a value"
                exit 1
            fi
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            if [[ -z "$TARGET_CHIP" ]]; then
                TARGET_CHIP="$1"
            else
                print_error "Multiple target chips specified: $TARGET_CHIP and $1"
                exit 1
            fi
            shift
            ;;
    esac
done

# Set default if no target specified
if [[ -z "$TARGET_CHIP" ]]; then
    TARGET_CHIP="unknown"
fi

# Validate target chip
VALID_TARGETS="esp8266 esp32 esp32s2 esp32s3 esp32c2 esp32c3 esp32c5 esp32c6 esp32c61 esp32h2 esp32h21 esp32h4 esp32p4"
if [[ ! " $VALID_TARGETS " =~ " $TARGET_CHIP " ]]; then
    if [[ "$TARGET_CHIP" == "unknown" ]]; then
        print_error "No target chip specified."
        echo ""
        usage
        exit 1
    else
        print_error "Invalid target chip: $TARGET_CHIP"
        print_error "Valid targets: $VALID_TARGETS"
        exit 1
    fi
fi

print_status "Target chip: $TARGET_CHIP"
if [ -n "$BUILD_ONLY" ]; then
    print_status "Mode: BUILD-ONLY (hardware testing disabled)"
else
    print_status "Mode: BUILD + HARDWARE TESTING (port: $TEST_PORT, timeout: ${TEST_TIMEOUT}s)"
fi

# Build target tests
build_target_tests() {
    print_status "Building target tests for $TARGET_CHIP..."

    local build_dir="build"
    mkdir -p "$build_dir" || { print_error "Failed to create build directory"; exit 1; }
    cd "$build_dir" || { print_error "Failed to enter build directory"; exit 1; }

    # Configure CMake
    print_status "Configuring CMake for $TARGET_CHIP..."
    local cmake_cmd="cmake -G \"Ninja\" -DESP_TARGET=\"$TARGET_CHIP\""
    [ -n "$CMAKE_ARGS" ] && cmake_cmd="$cmake_cmd $CMAKE_ARGS"
    cmake_cmd="$cmake_cmd .."

    print_status "Running: $cmake_cmd"
    if ! eval "$cmake_cmd"; then
        print_error "CMake configuration failed - check ESP toolchain installation"
        cd .. && exit 1
    fi

    # Build
    print_status "Building with ninja..."
    if ! ninja ; then
        print_error "ninja build failed for $TARGET_CHIP"
        cd .. && exit 1
    fi

    cd ..
    print_status "Target tests build completed successfully for $TARGET_CHIP"
}


# Run target tests on actual hardware using load-test.py
run_target_tests_hardware() {
    print_status "Running hardware tests on $TARGET_CHIP via $TEST_PORT..."

    if [ ! -e "$TEST_PORT" ]; then
        print_warning "Serial port $TEST_PORT not found"
        print_warning "Please connect ESP device or specify correct port with --port /dev/ttyUSBX"
        return 1
    fi

    local test_bins=$(find "build" -name "Test*.bin" -type f 2>/dev/null)
    if [ -z "$test_bins" ]; then
        print_error "No test binary files found - build may have failed"
        return 1
    fi

    local overall_success=true
    local test_count=0
    local passed_count=0

    for bin in $test_bins; do
        [ ! -f "$bin" ] && continue

        local test_name=$(basename "$bin" .bin)
        print_status "Running hardware test: $test_name"
        print_status "Command: python3 load-test.py -c $TARGET_CHIP -f $bin -p $TEST_PORT -t $TEST_TIMEOUT"

        test_count=$((test_count + 1))

        if python3 load-test.py -c "$TARGET_CHIP" -f "$bin" -p "$TEST_PORT" -t "$TEST_TIMEOUT"; then
            print_status "✅ Hardware test PASSED: $test_name"
            passed_count=$((passed_count + 1))
        else
            print_error "❌ Hardware test FAILED: $test_name"
            overall_success=false
        fi
        echo ""
    done

    echo ""
    print_status "Hardware Test Summary:"
    print_status "  Total tests: $test_count"
    print_status "  Passed: $passed_count"
    print_status "  Failed: $((test_count - passed_count))"

    if [ "$overall_success" = true ]; then
        print_status "🎉 All hardware tests PASSED!"
    else
        print_error "⚠️  Some hardware tests FAILED!"
        return 1
    fi
}


# Main execution
main() {
    build_target_tests

    # Run hardware tests unless build-only mode is requested
    if [ -z "$BUILD_ONLY" ]; then
        echo ""
        print_status "🔧 RUNNING HARDWARE TESTS (default behavior)..."
        print_status "Note: Hardware testing requires ESP device connected to $TEST_PORT"
        print_status "Use --build-only to skip hardware testing"
        if run_target_tests_hardware; then
            print_status "✅ Hardware tests completed successfully!"
        else
            print_error "❌ Hardware tests failed!"
            exit 1
        fi
        print_status "🎉 BUILD + HARDWARE TESTS completed successfully for $TARGET_CHIP!"
    else
        print_status "✅ BUILD-ONLY completed successfully for $TARGET_CHIP!"
    fi
}

# Run main function
main "$@"
