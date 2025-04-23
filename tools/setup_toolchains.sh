#!/usr/bin/env bash

TO_DOWNLOAD=(
    "https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz"
    "https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz"
    "https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/riscv32-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz"
)

for url in "${TO_DOWNLOAD[@]}"; do
    filename=$(basename "$url")
    if [ ! -f "$filename" ]; then
        echo "Downloading $filename..."
        wget "$url"
    else
        echo "$filename already exists. Skipping download."
    fi

    if [[ $filename == *.tar.xz ]]; then
        tar -xf "$filename"
    elif [[ $filename == *.tar.gz ]]; then
        tar -xzf "$filename"
    fi
done
