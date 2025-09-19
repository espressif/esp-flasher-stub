cmake_minimum_required(VERSION 3.28)

option(TARGET_CHIP "Target ESP chip" "OFF")

if("${TARGET_CHIP}" STREQUAL "OFF")
    message(FATAL_ERROR "Please set target chip via -DTARGET_CHIP.")
endif()

# Detect toolchain prefix and flags early
set(EXTRA_RISCV_FLAGS -march=rv32imc -mabi=ilp32 -msmall-data-limit=0)
set(EXTRA_XTENSA_FLAGS -mtext-section-literals -mlongcalls)

set(ESP8266_FLAGS "-DESP8266" ${EXTRA_XTENSA_FLAGS})
set(XTENSA_FLAGS "-DXTENSA" ${EXTRA_XTENSA_FLAGS})
set(RISCV_FLAGS "-DRISCV" ${EXTRA_RISCV_FLAGS})

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

# Set compilers BEFORE project() to avoid host detection (e.g., -arch arm64 or -isysroot)
set(CMAKE_SYSTEM_NAME Generic)
# CMAKE_C_COMPILER is for esp-stub-lib
set(CMAKE_C_COMPILER ${TOOLCHAIN_EXECUTABLE_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_EXECUTABLE_PREFIX}g++)
set(CMAKE_LINKER ${TOOLCHAIN_EXECUTABLE_PREFIX}g++)
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

project(flasher-stub CXX C)

set(COMMON_FLAGS -Wall -Wextra -Werror -Wshadow -Wundef -Wconversion -Os -fno-common -nostdlib -fno-builtin -ffunction-sections)

add_compile_options(${COMMON_FLAGS})
add_compile_options(${CHIP_FLAGS})

# CXX standard set based on the GCC version of the toolchain for ESP8266 (oldest one)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-std=gnu17>)

set(LINKER_FLAGS -Wl,-static -Wl,--gc-sections)
set(TARGET_NAME stub-${TARGET_CHIP} CACHE STRING "Target name")
message(STATUS "Building for ${TARGET_CHIP}")
add_executable(${TARGET_NAME})

set(LINKER_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/src/ld")
set(CHIP_LINKER_SCRIPT "${LINKER_SCRIPTS_DIR}/${TARGET_CHIP}.ld")

target_link_options(${TARGET_NAME} PRIVATE
    ${LINKER_FLAGS}
    ${CHIP_FLAGS}
    "-T${CHIP_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
)

add_subdirectory(${CMAKE_SOURCE_DIR}/src)

set(ESP_TARGET ${TARGET_CHIP} CACHE INTERNAL "Pass TARGET_CHIP as ESP_TARGET")
add_subdirectory(${CMAKE_SOURCE_DIR}/esp-stub-lib)
target_link_libraries(${TARGET_NAME} PRIVATE esp-stub-lib)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py ${CMAKE_BINARY_DIR}/${TARGET_NAME}${CMAKE_EXECUTABLE_SUFFIX_CXX} ${CMAKE_BINARY_DIR}/${TARGET_CHIP}.json
    COMMENT "Running elf2json.py to produce a JSON file output"
)
