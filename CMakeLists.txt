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

# Apply firmware build configuration
add_compile_options(${ESP_FIRMWARE_C_FLAGS} ${TARGET_FLAGS})
add_link_options(${ESP_FIRMWARE_LINKER_FLAGS} ${TARGET_FLAGS})

# Create executable
set(TARGET_NAME stub-${TARGET_CHIP} CACHE STRING "Target name")
add_executable(${TARGET_NAME})

# Set up linker script
set(LINKER_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/src/ld")
set(CHIP_LINKER_SCRIPT "${LINKER_SCRIPTS_DIR}/${TARGET_CHIP}.ld")

target_link_options(${TARGET_NAME} PRIVATE
    ${ESP_FIRMWARE_LINKER_FLAGS}
    ${TARGET_FLAGS}
    "-T${CHIP_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
)

# Add sources
add_subdirectory(${CMAKE_SOURCE_DIR}/src)

# Add esp-stub-lib
set(ESP_TARGET ${TARGET_CHIP} CACHE INTERNAL "Pass TARGET_CHIP as ESP_TARGET")
add_subdirectory(${CMAKE_SOURCE_DIR}/esp-stub-lib)
target_link_libraries(${TARGET_NAME} PRIVATE esp-stub-lib)

# Add post-build JSON generation
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py ${CMAKE_BINARY_DIR}/${TARGET_NAME}${CMAKE_EXECUTABLE_SUFFIX_C} ${CMAKE_BINARY_DIR}/${TARGET_CHIP}.json
    COMMENT "Running elf2json.py to produce a JSON file output"
)
