cmake_minimum_required(VERSION 3.28)

project(flasher-stub CXX)

option(TARGET_CHIP "Target ESP chip" "OFF")

if("${TARGET_CHIP}" STREQUAL "OFF")
    message(FATAL_ERROR "Please set target chip via -DTARGET_CHIP.")
endif()

# CXX standard set based on the GCC version of the toolchain for ESP8266 (oldest one)
set(CUSTOM_FLAGS --std=gnu++17 -Wall -Wextra -Werror -Wshadow -Wundef -Wconversion -Os -fno-common -nostdlib -fno-builtin -Wl,-static -g -ffunction-sections -Wl,--gc-sections)
set(EXTRA_RISCV_FLAGS -march=rv32imc -mabi=ilp32 -msmall-data-limit=0)
set(EXTRA_XTENSA_FLAGS -mtext-section-literals -mlongcalls)

set(ESP8266_FLAGS "-DESP8266" ${EXTRA_XTENSA_FLAGS})
set(XTENSA_FLAGS "-DXTENSA" ${EXTRA_XTENSA_FLAGS})
set(RISCV_FLAGS "-DRISCV" ${EXTRA_RISCV_FLAGS})

message(STATUS "Building for ${TARGET_CHIP}")
add_executable(${TARGET_CHIP})

set(ESP32_XTENSA_CHIPS esp32 esp32s2 esp32s3)

if(TARGET_CHIP IN_LIST ESP32_XTENSA_CHIPS)
    set(TOOLCHAIN_EXECUTABLE_PREFIX "xtensa-${TARGET_CHIP}-elf-")
    set(CHIP_FLAGS "${XTENSA_FLAGS}")
elseif(TARGET_CHIP STREQUAL "esp8266")
    set(TOOLCHAIN_EXECUTABLE_PREFIX "xtensa-lx106-elf-")
    set(CHIP_FLAGS "${ESP8266_FLAGS}")
    # The lx106 toolchain is of version 8.4 which doesn't support the --dependency-file linker option.
    set(CMAKE_LINK_DEPENDS_USE_LINKER FALSE)  # requires CMake 3.27 or higher
else()
    set(TOOLCHAIN_EXECUTABLE_PREFIX "riscv32-esp-elf-")
    set(CHIP_FLAGS "${RISCV_FLAGS}")
endif()

set(LINKER_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/src/ld")
set(CHIP_LINKER_SCRIPT "${LINKER_SCRIPTS_DIR}/${TARGET_CHIP}.ld")

target_compile_options(${TARGET_CHIP} PRIVATE
    ${CUSTOM_FLAGS}
    ${CHIP_FLAGS}
)

target_link_options(${TARGET_CHIP} PRIVATE
    ${CUSTOM_FLAGS}
    ${CHIP_FLAGS}
    "-T${CHIP_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_CHIP}.map
)

set(CMAKE_CXX_COMPILER ${TOOLCHAIN_EXECUTABLE_PREFIX}g++)
set(CMAKE_LINKER ${TOOLCHAIN_EXECUTABLE_PREFIX}g++)
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

add_subdirectory(${CMAKE_SOURCE_DIR}/src)
