# ESP Flasher Stub Target Tests
# Cross-compiled tests for ESP hardware validation

cmake_minimum_required(VERSION 3.28)

# Target configuration (must be set before project())
if(NOT DEFINED ESP_TARGET)
    set(ESP_TARGET "esp32" CACHE STRING "Target ESP chip")
endif()

# Set ESP_TARGET as cache variable to make it available to toolchain file
set(ESP_TARGET ${ESP_TARGET} CACHE STRING "Target ESP chip" FORCE)

# Configure ESP toolchain
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake)
include(esp-targets)
configure_esp_toolchain(${ESP_TARGET})

project(esp-flasher-stub-target-tests C)

# Configure ESP test build
get_compiler_flags_for_target(${ESP_TARGET} TARGET_COMPILER_FLAGS)

# Test build flags (more debug-friendly than firmware, allows libc)
set(ESP_TEST_C_FLAGS
    -std=gnu17
    -Wall -Wextra -Werror -Wshadow -Wundef -Wconversion
    -fno-common -ffunction-sections
    -g -O0                  # Debug build for tests
)

set(ESP_TEST_LINKER_FLAGS
    -Wl,-static -Wl,--gc-sections
)

# Apply test build configuration
add_compile_options(${ESP_TEST_C_FLAGS} ${TARGET_COMPILER_FLAGS})
add_link_options(${ESP_TEST_LINKER_FLAGS})

# Check for Python and esptool module
find_program(PYTHON3 python3)
if(NOT PYTHON3)
    find_program(PYTHON3 python)
endif()

if(NOT PYTHON3)
    message(FATAL_ERROR "Python not found. Please install Python and ensure it's in your PATH.")
endif()

set(ESPTOOL ${PYTHON3} -m esptool)

# Test framework paths
set(UNITY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../Unity)

# Include directories
include_directories(
    ${UNITY_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../esp-stub-lib/include/esp-stub-lib
)

# Helper function to add target tests
function(add_target_test TEST_NAME)
    set(oneValueArgs TIMEOUT)
    set(multiValueArgs SOURCES LINKER_SCRIPTS)
    cmake_parse_arguments(TARGET_TEST "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Auto-generate Unity test runner
    set(RUNNER_FILE ${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME}_Runner.c)

    # Get the first source file (test file) for runner generation
    list(GET TARGET_TEST_SOURCES 0 TEST_SOURCE_FILE)

    add_custom_command(
        OUTPUT ${RUNNER_FILE}
        COMMAND ruby ${UNITY_ROOT}/auto/generate_test_runner.rb ${TEST_SOURCE_FILE} ${RUNNER_FILE}
        DEPENDS ${TEST_SOURCE_FILE}
        COMMENT "Generating Unity test runner for ${TEST_NAME}"
    )

    # Create test executable
    add_executable(${TEST_NAME}
        ${TARGET_TEST_SOURCES}
        ${UNITY_ROOT}/src/unity.c
        ${RUNNER_FILE}
    )

    # Add linker scripts
    set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/ld/${ESP_TARGET}.ld)
    target_link_options(${TEST_NAME} PRIVATE -T${LINKER_SCRIPT})

    # Generate map file
    target_link_options(${TEST_NAME} PRIVATE
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME}.map
    )

    # Link against libc for target tests (enables malloc, free, standard functions)
    # Note: Unlike firmware build, tests can use libc for convenience
    target_link_options(${TEST_NAME} PRIVATE -lc -lm)

    # Add esp-stub-lib (re-enabled for libc-based tests)
    target_link_libraries(${TEST_NAME} PRIVATE esp-stub-lib)

    # Generate binary file using esptool elf2image for load_ram usage
    set(BIN_FILE ${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME}.bin)
    add_custom_command(
        OUTPUT ${BIN_FILE}
        COMMAND ${ESPTOOL} --chip ${ESP_TARGET} elf2image -o ${BIN_FILE} $<TARGET_FILE:${TEST_NAME}>
        DEPENDS ${TEST_NAME}
        COMMENT "Generating binary file for ${TEST_NAME} using esptool elf2image"
    )

    # Custom target for binary generation
    add_custom_target(${TEST_NAME}_bin ALL
        DEPENDS ${BIN_FILE}
        COMMENT "Binary file ready: ${BIN_FILE}"
    )

    set_target_properties(${TEST_NAME} PROPERTIES
        LABELS "target_test;${ESP_TARGET}"
        TARGET_CHIP ${ESP_TARGET}
    )

    message(STATUS "Added target test: ${TEST_NAME} for ${ESP_TARGET}")
endfunction()

# Add esp-stub-lib (re-enabled for libc-based tests)
set(ESP_TARGET ${ESP_TARGET} CACHE INTERNAL "ESP target for stub lib")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../esp-stub-lib esp-stub-lib)

# Add target tests
add_target_test(TestTargetExample
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/TestTargetExample.c
        ${CMAKE_CURRENT_SOURCE_DIR}/minimal_system.c
)

# Configuration summary
message(STATUS "Target tests configuration:")
message(STATUS "  Target chip: ${ESP_TARGET}")
message(STATUS "  Toolchain: ${CMAKE_C_COMPILER}")
message(STATUS "  Unity: ${UNITY_ROOT}")
message(STATUS "  Linker script: ${LINKER_SCRIPT_INFO}")
