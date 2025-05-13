#!/usr/bin/env bash

ALL_CHIPS=(
    "esp32"
    "esp32c2"
    "esp32c3"
    "esp32c5"
    "esp32c6"
    "esp32c61"
    "esp32h2"
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
