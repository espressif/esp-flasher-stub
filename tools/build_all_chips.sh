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
    "esp32p4-rev1"
    "esp32p4"
    "esp32s2"
    "esp32s3"
    "esp8266"
)

for chip in "${ALL_CHIPS[@]}"; do
    echo "Building for $chip"
    DIR=build-$chip
    mkdir -p "$DIR"
    cmake -G Ninja -B "$DIR" -DTARGET_CHIP=$chip --fresh
    ninja -C "$DIR"
done
