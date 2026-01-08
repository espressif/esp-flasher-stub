# ESP Target Definitions and Utilities
# Shared across the entire esp-flasher-stub project

# Define supported ESP targets
set(ESP8266_TARGET esp8266)
set(XTENSA_TARGETS esp32 esp32s2 esp32s3)
set(RISCV_TARGETS esp32c2 esp32c3 esp32c5 esp32c6 esp32c61 esp32h2 esp32h21 esp32h4 esp32p4-rev1 esp32p4)
set(ALL_ESP_TARGETS ${ESP8266_TARGET} ${XTENSA_TARGETS} ${RISCV_TARGETS})

# =============================================================================
# Compiler and Linker Flags
# =============================================================================

# Common compiler flags for all targets
set(COMMON_COMPILER_FLAGS
    -Wall
    -Werror
    -Wextra
    -Wshadow
    -Wundef
    -Wconversion
    -Os
    -nostdlib
    -fno-builtin
    -fno-common
    -ffunction-sections
    -std=gnu17
)

# Xtensa-specific compiler flags (ESP32, ESP32-S2, ESP32-S3)
set(XTENSA_COMPILER_FLAGS
    ${COMMON_COMPILER_FLAGS}
    -DXTENSA
    -mlongcalls
    -mtext-section-literals
)

# ESP8266-specific compiler flags
set(ESP8266_COMPILER_FLAGS
    ${COMMON_COMPILER_FLAGS}
    -DESP8266
    -mlongcalls
    -mtext-section-literals
)

# RISC-V specific compiler flags
set(RISCV_COMPILER_FLAGS
    ${COMMON_COMPILER_FLAGS}
    -DRISCV
    -march=rv32imc
    -mabi=ilp32
    -msmall-data-limit=0
)

# Common linker flags for all targets
set(COMMON_LINKER_FLAGS
    "-nostdlib"
    "-Wl,-static"
    "-Wl,--gc-sections"
)

# =============================================================================
# Functions
# =============================================================================

# Function to validate ESP target
function(validate_esp_target TARGET_CHIP)
    if(NOT TARGET_CHIP IN_LIST ALL_ESP_TARGETS)
        message(FATAL_ERROR "Invalid TARGET_CHIP '${TARGET_CHIP}'. Must be one of: ${ALL_ESP_TARGETS}")
    endif()
endfunction()

# Function to get toolchain prefix for target
function(get_esp_toolchain_prefix TARGET_CHIP OUTPUT_VAR)
    validate_esp_target(${TARGET_CHIP})

    if(TARGET_CHIP IN_LIST XTENSA_TARGETS)
        set(${OUTPUT_VAR} "xtensa-${TARGET_CHIP}-elf-" PARENT_SCOPE)
    elseif(TARGET_CHIP STREQUAL "esp8266")
        set(${OUTPUT_VAR} "xtensa-lx106-elf-" PARENT_SCOPE)
    else()
        set(${OUTPUT_VAR} "riscv32-esp-elf-" PARENT_SCOPE)
    endif()
endfunction()

# Function to get all compiler flags for target
function(get_compiler_flags_for_target TARGET_CHIP OUTPUT_VAR)
    validate_esp_target(${TARGET_CHIP})

    if(TARGET_CHIP STREQUAL "esp8266")
        set(${OUTPUT_VAR} ${ESP8266_COMPILER_FLAGS} PARENT_SCOPE)
    elseif(TARGET_CHIP IN_LIST XTENSA_TARGETS)
        set(${OUTPUT_VAR} ${XTENSA_COMPILER_FLAGS} PARENT_SCOPE)
    elseif(TARGET_CHIP IN_LIST RISCV_TARGETS)
        set(${OUTPUT_VAR} ${RISCV_COMPILER_FLAGS} PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unknown target chip: ${TARGET_CHIP}")
    endif()
endfunction()

# Function to configure ESP toolchain (must be called before project())
function(configure_esp_toolchain TARGET_CHIP)
    validate_esp_target(${TARGET_CHIP})
    get_esp_toolchain_prefix(${TARGET_CHIP} TOOLCHAIN_PREFIX)

    # Set system and compiler before project() to avoid host detection
    set(CMAKE_SYSTEM_NAME Generic PARENT_SCOPE)
    set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc PARENT_SCOPE)
    set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}gcc PARENT_SCOPE)
    set(CMAKE_EXECUTABLE_SUFFIX_C ".elf" PARENT_SCOPE)

    # ESP8266 specific handling
    if(TARGET_CHIP STREQUAL "esp8266")
        set(CMAKE_LINK_DEPENDS_USE_LINKER FALSE PARENT_SCOPE)
    endif()

    message(STATUS "ESP toolchain configured for ${TARGET_CHIP}")
endfunction()

message(STATUS "ESP targets loaded: ${ALL_ESP_TARGETS}")
