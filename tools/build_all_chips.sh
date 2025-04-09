#!/usr/bin/env bash

ALL_CHIPS=(
    "esp32"
    "esp32c3"
    "esp8266"
)

for chip in "${ALL_CHIPS[@]}"; do
    echo "Building for $chip"
    DIR=build-$chip
    mkdir -p "$DIR"
    cmake -G Ninja -B "$DIR" -DTARGET_CHIP=$chip --fresh
    ninja -C "$DIR"
done
