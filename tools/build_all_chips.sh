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
    "esp32h4"
    "esp32p4-rev1"
    "esp32p4"
    "esp32s2"
    "esp32s3"
    "esp8266"
)

for chip in "${ALL_CHIPS[@]}"; do
    echo "========================================="
    echo "Building for $chip"
    echo "========================================="
    DIR=build-$chip

    # Pass 1: build base stub ELF
    mkdir -p "$DIR"
    cmake -G Ninja -B "$DIR" -DTARGET_CHIP=$chip --fresh -Wno-dev
    ninja -C "$DIR" "stub-$chip"

    # Pass 2: re-configure (CMake runs compute_plugin_addrs.py to generate plugin_addrs.cmake
    # now that the base ELF exists), then build the plugin and regenerate the JSON
    if [ "$chip" != "esp8266" ] && [ "$chip" != "esp32" ]; then
        cmake -B "$DIR" -DTARGET_CHIP=$chip -Wno-dev
        ninja -C "$DIR"
    fi
done
