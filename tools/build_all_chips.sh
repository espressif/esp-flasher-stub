#!/usr/bin/env bash
set -e

# Get the script's directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Change to project root
cd "$PROJECT_ROOT"

ALL_CHIPS=(
    "esp32"
    "esp32c2"
    "esp32c3"
    "esp32c5"
    "esp32c6"
    "esp32c61"
    "esp32h2"
    "esp32h21"
    "esp32h4"
    "esp32p4-rev1"
    "esp32p4"
    "esp32s2"
    "esp32s3"
    "esp32s31"
    "esp8266"
)

SPI_CHIPS=(
    "esp32c2"
    "esp32c3"
    "esp32p4-rev1"
    "esp32p4"
    "esp32s2"
    "esp32s3"
)

CHIPS=("${ALL_CHIPS[@]}")
BUILD_DIR_PREFIX="build-"
CMAKE_ARGS=()

for arg in "$@"; do
    if [[ "$arg" == "--spi" ]]; then
        CHIPS=("${SPI_CHIPS[@]}")
        BUILD_DIR_PREFIX="spi-build-"
        CMAKE_ARGS+=("-DSTUB_SPI_ENABLED=ON")
        break
    fi
done

for chip in "${CHIPS[@]}"; do
    echo "========================================="
    echo "Building for $chip"
    echo "========================================="
    DIR="${BUILD_DIR_PREFIX}${chip}"

    # Single pass: CMake wires the base stub, plugin address computation, plugin
    # ELF(s), and JSON as one dependency graph, so a plain ninja builds it all.
    cmake -G Ninja -B "$DIR" -DTARGET_CHIP="$chip" "${CMAKE_ARGS[@]}" --fresh -Wno-dev
    ninja -C "$DIR"
done
