cmake_minimum_required(VERSION 3.28)

project(flasher_stub CXX)

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

set(DEFAULT_CHIP "esp32")
option(TARGET_CHIP "Target ESP chip" ${DEFAULT_CHIP})

set(LINKER_SCRIPTS_DIR "${SRC_DIR}/ld")

# CXX standard set based on the GCC version of the toolchain for ESP8266 (oldest one)
set(CUSTOM_FLAGS --std=gnu++17 -Wall -Wextra -Werror -Wshadow -Wundef -Wconversion -Os -fno-common -mtext-section-literals -mlongcalls -nostdlib -fno-builtin -Wl,-static -g -ffunction-sections -Wl,--gc-sections)
set(EXTRA_RISCV_FLAGS -march=rv32imc -mabi=ilp32 -msmall-data-limit=0)

set(ESP8266_FLAGS "-DESP8266")
set(XTENSA_FLAGS "-DXTENSA")
set(RISCV_FLAGS "-DRISCV" ${EXTRA_RISCV_FLAGS})
set(ESP32_XTENSA_CHIPS esp32 esp32-s2 esp32-s3)

include(FetchContent)

function(download_toolchain TOOLCHAIN_NAME TOOLCHAIN_URL TOOLCHAIN_HASH)
    FetchContent_Declare(${TOOLCHAIN_NAME}
        URL ${TOOLCHAIN_URL}
        URL_HASH SHA256=${TOOLCHAIN_HASH}
    )

    message(STATUS "Downloading ${TOOLCHAIN_NAME} toolchain from ${TOOLCHAIN_URL}. This may take a while...")

    FetchContent_MakeAvailable(${TOOLCHAIN_NAME})
endfunction()

# TODO don't do this way
file(GLOB_RECURSE SOURCES "${SRC_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE
    ${CUSTOM_FLAGS}  # Custom user-defined flags
    ${CHIP_FLAGS}    # Architecture-specific flags
)

target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR})

if(TARGET_CHIP IN_LIST ESP32_XTENSA_CHIPS)
    message(STATUS "Building for ${TARGET_CHIP}")
    set(CHIP_TOOLCHAIN_URL "https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.gz")
    set(TOOLCHAIN_NAME "xtensa_toolchain")
    set(TOOLCHAIN_EXECUTABLE_PREFIX "xtensa-${TARGET_CHIP}-elf-")
    download_toolchain(${TOOLCHAIN_NAME} ${CHIP_TOOLCHAIN_URL} "b1859df334a85541ae746e1b86439f59180d87f8cf1cc04c2e770fadf9f006e9")
    set(CHIP_FLAGS "${XTENSA_FLAGS}")
elseif(TARGET_CHIP STREQUAL "esp8266")
    message(STATUS "Building for ${TARGET_CHIP}")
    set(CHIP_TOOLCHAIN_URL "https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz")
    set(TOOLCHAIN_NAME "esp8266_toolchain")
    set(TOOLCHAIN_EXECUTABLE_PREFIX "xtensa-lx106-elf-")
    download_toolchain(${TOOLCHAIN_NAME} ${CHIP_TOOLCHAIN_URL} )
    set(CHIP_FLAGS "${ESP8266_FLAGS}")
else()
    message(STATUS "Building for ${TARGET_CHIP} (RISC-V)")
    set(CHIP_TOOLCHAIN_URL "https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/riscv32-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.gz")
    set(TOOLCHAIN_NAME "riscv_toolchain")
    set(TOOLCHAIN_EXECUTABLE_PREFIX "riscv32-esp-elf-")
    download_toolchain(${TOOLCHAIN_NAME} ${CHIP_TOOLCHAIN_URL} )
    set(CHIP_FLAGS "${RISCV_FLAGS}")
endif()

set(CHIP_LINKER_SCRIPT "${LINKER_SCRIPTS_DIR}/${TARGET_CHIP}.ld")
list(APPEND CMAKE_CXX_FLAGS "-T${CHIP_LINKER_SCRIPT}")

set(TOOLCHAIN_SEARCH_PATH "${CMAKE_BINARY_DIR}/_deps/${TOOLCHAIN_NAME}-src/bin")
find_program(COMPILER_PATH NAMES ${TOOLCHAIN_EXECUTABLE_PREFIX}g++ PATHS "${TOOLCHAIN_SEARCH_PATH}" REQUIRED NO_DEFAULT_PATH)
set(CMAKE_CXX_COMPILER ${COMPILER_PATH})
find_program(LINKER_PATH NAMES ${TOOLCHAIN_EXECUTABLE_PREFIX}ld PATHS "${TOOLCHAIN_SEARCH_PATH}" REQUIRED NO_DEFAULT_PATH)
set(CMAKE_LINKER ${LINKER_PATH})
