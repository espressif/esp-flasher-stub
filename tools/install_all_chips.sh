#!/usr/bin/env bash
set -e

# Get the script's directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ESPTOOL_STUBS_DIR must be specified
if [[ -z "$ESPTOOL_STUBS_DIR" ]]; then
    echo "Error: ESPTOOL_STUBS_DIR environment variable must be set" >&2
    echo "Example: export ESPTOOL_STUBS_DIR=/path/to/esptool/esptool/targets/stub_flasher/2" >&2
    exit 1
fi

# Change to project root
cd "$PROJECT_ROOT"

echo "Installing JSON stub files to: $ESPTOOL_STUBS_DIR"

# Find all JSON files in build-* directories
FOUND=0
for json_file in build-*/*.json; do
    if [[ -f "$json_file" ]]; then
        echo "Installing $json_file"
        cp "$json_file" "$ESPTOOL_STUBS_DIR/"
        FOUND=1
    fi
done

if [[ $FOUND -eq 0 ]]; then
    echo "Warning: No JSON files found in build-* directories" >&2
    exit 1
fi

echo "Installation complete!"
