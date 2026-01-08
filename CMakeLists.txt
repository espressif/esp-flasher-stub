cmake_minimum_required(VERSION 3.28)

# Validate TARGET_CHIP parameter
option(TARGET_CHIP "Target ESP chip" "OFF")
if("${TARGET_CHIP}" STREQUAL "OFF")
    message(FATAL_ERROR "Please set target chip via -DTARGET_CHIP.")
endif()

# esp-stub-lib uses ESP_TARGET as the base chip directory name (no -rev suffix),
# and an optional ESP_TARGET_REV to select the correct ROM/behavior.
# Example:
# - TARGET_CHIP=esp32p4-rev1  -> ESP_TARGET=esp32p4, ESP_TARGET_REV=rev1
# - TARGET_CHIP=esp32p4       -> ESP_TARGET=esp32p4, ESP_TARGET_REV=""
set(ESP_TARGET_BASE "${TARGET_CHIP}")
set(ESP_TARGET_REV "" CACHE STRING "Target ESP chip revision (e.g. rev1)" FORCE)
if(TARGET_CHIP MATCHES "^(.*)-rev([0-9]+)$")
    set(ESP_TARGET_BASE "${CMAKE_MATCH_1}")
    set(ESP_TARGET_REV "rev${CMAKE_MATCH_2}" CACHE STRING "Target ESP chip revision (e.g. rev1)" FORCE)
endif()

# Set up ESP_TARGET for toolchain and build system
set(ESP_TARGET ${ESP_TARGET_BASE} CACHE STRING "Target ESP chip" FORCE)

# Configure ESP toolchain
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(esp-targets)
configure_esp_toolchain(${TARGET_CHIP})

project(flasher-stub C)

# Get target-specific compiler flags
get_compiler_flags_for_target(${TARGET_CHIP} TARGET_COMPILER_FLAGS)

# Create executable
set(TARGET_NAME stub-${TARGET_CHIP} CACHE STRING "Target name")

# Set up linker script
set(LINKER_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/src/ld")
set(CHIP_LINKER_SCRIPT "${LINKER_SCRIPTS_DIR}/${TARGET_CHIP}.ld")

# Add compiler and linker flags
add_compile_options(${TARGET_COMPILER_FLAGS})
add_link_options(${COMMON_LINKER_FLAGS})

add_subdirectory(${CMAKE_SOURCE_DIR}/esp-stub-lib)

# Add stub target
add_subdirectory(${CMAKE_SOURCE_DIR}/src)
