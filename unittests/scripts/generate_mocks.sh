#!/bin/bash

# =========================================================================
#   ESP Flasher Stub Mock Generation Script
#   Generates CMock files for HOST testing only
#   Note: Target tests don't use mocks - they test real hardware
# =========================================================================

set -e  # Exit on any error

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

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/.."
cd "${PROJECT_DIR}"

print_status "Starting mock generation for ESP Flasher Stub"

# Check if required tools are available
check_dependencies() {
    print_status "Checking dependencies..."

    if ! command -v ruby &> /dev/null; then
        print_error "ruby is not installed"
        exit 1
    fi

    if [ ! -d "CMock" ]; then
        print_error "CMock directory not found. Please ensure CMock is cloned (submodule). git clone https://github.com/ThrowTheSwitch/CMock.git"
        exit 1
    fi

    if [ ! -d "Unity" ]; then
        print_error "Unity directory not found. Please ensure Unity is cloned (submodule). git clone https://github.com/ThrowTheSwitch/Unity.git"
        exit 1
    fi

    print_status "All dependencies found"
}

# Generate mocks
generate_mocks() {
    print_status "Generating CMock files..."

    # Determine mock output directory based on build mode and location
    if [ -d "host/build" ]; then
        MOCK_OUT_DIR="${PROJECT_DIR}/host/build/mocks"
    elif [ -d "target/build" ]; then
        MOCK_OUT_DIR="${PROJECT_DIR}/target/build/mocks"
    elif [ -d "build-host" ]; then
        MOCK_OUT_DIR="${PROJECT_DIR}/build-host/mocks"
    elif [ -d "build-target-${TARGET_CHIP:-esp32}" ]; then
        MOCK_OUT_DIR="${PROJECT_DIR}/build-target-${TARGET_CHIP:-esp32}/mocks"
    else
        # Default to host/build for new structure
        MOCK_OUT_DIR="${PROJECT_DIR}/host/build/mocks"
    fi

    # Set environment variables
    export CMOCK_DIR="${PROJECT_DIR}/CMock"
    export UNITY_DIR="${PROJECT_DIR}/Unity"
    export MOCK_OUT="$MOCK_OUT_DIR"

    # Dynamically add Ruby gem bin directory to PATH
    GEM_BIN_DIR=$(ruby -e "puts Gem.bindir" 2>/dev/null)
    if [ -n "$GEM_BIN_DIR" ] && [ -d "$GEM_BIN_DIR" ]; then
        export PATH="$PATH:$GEM_BIN_DIR"
        print_status "Added Ruby gem bin directory to PATH: $GEM_BIN_DIR"
    else
        print_warning "Could not determine Ruby gem bin directory, using default PATH"
    fi

    # Create mocks directory
    mkdir -p "$MOCK_OUT_DIR"
    print_status "Using mock output directory: $MOCK_OUT_DIR"

    # Generate mock for rom_wrappers
    print_status "Generating mock for rom_wrappers..."
    ruby CMock/scripts/create_mock.rb ../esp-stub-lib/include/esp-stub-lib/rom_wrappers.h

    # Generate mock for uart
    print_status "Generating mock for uart..."
    ruby CMock/scripts/create_mock.rb ../esp-stub-lib/include/esp-stub-lib/uart.h

    print_status "Mock generation completed successfully!"
}

# Main execution
main() {
    check_dependencies
    generate_mocks

    print_status "All mocks generated successfully!"
}

# Run main function
main "$@"
