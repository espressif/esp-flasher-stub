cmake_minimum_required(VERSION 3.28)

# Validate TARGET_CHIP parameter
option(TARGET_CHIP "Target ESP chip" "OFF")
if("${TARGET_CHIP}" STREQUAL "OFF")
    message(FATAL_ERROR "Please set target chip via -DTARGET_CHIP.")
endif()

# Set up ESP_TARGET for toolchain and build system
set(ESP_TARGET ${TARGET_CHIP} CACHE STRING "Target ESP chip" FORCE)

# Configure ESP toolchain
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(esp-targets)
configure_esp_toolchain(${TARGET_CHIP})

project(flasher-stub C)

# Configure ESP firmware build
get_esp_target_flags(${TARGET_CHIP} TARGET_FLAGS)

# C standard set based on the GCC version of the toolchain for ESP8266 (oldest one)
# Firmware build flags
set(ESP_FIRMWARE_C_FLAGS
    -std=gnu17
    -Wall -Wextra -Werror -Wshadow -Wundef -Wconversion
    -fno-common -ffunction-sections
    -Os                     # Optimize for size
    -nostdlib -fno-builtin  # No standard library
)

set(ESP_FIRMWARE_LINKER_FLAGS
    -Wl,-static -Wl,--gc-sections
    -nostdlib                   # No standard library linking
)

# Create executable
set(TARGET_NAME stub-${TARGET_CHIP} CACHE STRING "Target name")

# Set up linker script
set(LINKER_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/src/ld")
set(CHIP_LINKER_SCRIPT "${LINKER_SCRIPTS_DIR}/${TARGET_CHIP}.ld")

# Add esp-stub-lib
add_compile_options(${ESP_FIRMWARE_C_FLAGS})
add_compile_options(${TARGET_FLAGS})

add_subdirectory(${CMAKE_SOURCE_DIR}/esp-stub-lib)

# Add stub target
add_subdirectory(${CMAKE_SOURCE_DIR}/src)
