#!/bin/bash

# =========================================================================
#   ESP Flasher Stub Host Tests Runner
#   Runs native unit tests on the build machine
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

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

print_status "Starting ESP Flasher Stub HOST Tests"
print_status "Working directory: $(pwd)"

# Build host tests
build_host_tests() {
    print_status "Building host tests..."

    # Create build directory
    if ! mkdir -p build; then
        print_error "Failed to create build directory"
        exit 1
    fi

    cd build

    # Configure CMake for host tests
    print_status "Configuring CMake..."
    if ! cmake -G "Ninja" ..; then
        print_error "CMake configuration failed"
        print_error "Please check that all dependencies are installed and paths are correct"
        cd ..
        exit 1
    fi

    # Build
    print_status "Building with ninja..."
    if ! ninja ; then
        print_error "Ninja build failed"
        cd ..
        exit 1
    fi

    cd ..
    print_status "Host tests build completed successfully"
}

# Run host tests
run_host_tests() {
    print_status "Running host tests..."

    if ! cd build; then
        print_error "Failed to enter build directory"
        exit 1
    fi

    # Run tests with CTest
    if [ -f "CTestTestfile.cmake" ]; then
        print_status "Running tests with CTest..."
        if ! ctest --verbose --label-regex "host_test"; then
            print_error "One or more tests failed"
            print_error "Check the test output above for details"
            cd ..
            exit 1
        fi
        print_status "All tests passed successfully!"
    else
        print_error "CTest configuration not found"
        print_error "Build may have failed or CMake configuration is incomplete"
        cd ..
        exit 1
    fi

    cd ..
    print_status "Host tests completed successfully"
}

# Main execution
main() {
    build_host_tests
    run_host_tests

    print_status "Host tests completed successfully!"
}

# Run main function
main "$@"
