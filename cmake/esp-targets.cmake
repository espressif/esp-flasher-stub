# ESP Target Definitions and Utilities
# Shared across the entire esp-flasher-stub project

# Define supported ESP targets
set(ESP8266_TARGET esp8266)
set(XTENSA_TARGETS esp32 esp32s2 esp32s3)
set(RISCV_TARGETS esp32c2 esp32c3 esp32c5 esp32c6 esp32c61 esp32h2 esp32h21 esp32h4 esp32p4)
set(ALL_ESP_TARGETS ${ESP8266_TARGET} ${XTENSA_TARGETS} ${RISCV_TARGETS})

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

# Function to get target-specific compile flags
function(get_esp_target_flags TARGET_CHIP OUTPUT_VAR)
    validate_esp_target(${TARGET_CHIP})

    # Architecture-specific flags
    set(EXTRA_RISCV_FLAGS -march=rv32imc -mabi=ilp32 -msmall-data-limit=0)
    set(EXTRA_XTENSA_FLAGS -mtext-section-literals -mlongcalls)

    if(TARGET_CHIP IN_LIST XTENSA_TARGETS)
        set(${OUTPUT_VAR} "-DXTENSA" ${EXTRA_XTENSA_FLAGS} PARENT_SCOPE)
    elseif(TARGET_CHIP STREQUAL "esp8266")
        set(${OUTPUT_VAR} "-DESP8266" ${EXTRA_XTENSA_FLAGS} PARENT_SCOPE)
    else()
        set(${OUTPUT_VAR} "-DRISCV" ${EXTRA_RISCV_FLAGS} PARENT_SCOPE)
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
