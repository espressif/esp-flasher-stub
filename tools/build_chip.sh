#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -ne 1 ]; then
    echo "Usage: $(basename "$0") <chip>"
    echo "  e.g. $(basename "$0") esp32c6"
    exit 1
fi

chip="$1"

cd "$PROJECT_ROOT"

DIR=build-$chip

echo "========================================="
echo "Building for $chip"
echo "========================================="

# Pass 1: build base stub ELF
mkdir -p "$DIR"
cmake -G Ninja -B "$DIR" -DTARGET_CHIP=$chip --fresh -Wno-dev
ninja -C "$DIR" "stub-$chip"

# Pass 2: re-configure so CMake runs compute_plugin_addrs.py against the
# freshly built base ELF, then build the plugin(s) and regenerate the JSON.
if [ "$chip" != "esp8266" ] && [ "$chip" != "esp32" ]; then
    cmake -B "$DIR" -DTARGET_CHIP=$chip -Wno-dev
    ninja -C "$DIR"
fi
