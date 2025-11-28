# ESP Flasher Stub Host Tests
# Native unit tests with CMock and Unity for fast development

cmake_minimum_required(VERSION 3.28)
project(esp-flasher-stub-host-tests C)

# Host-specific configuration (native build)
set(CMAKE_C_FLAGS "-std=gnu17 -Wall -Wextra -Werror -g -O0")

# Test framework paths
set(UNITY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../Unity)
set(CMOCK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../CMock)

# Include directories
include_directories(
    ${UNITY_ROOT}/src
    ${CMOCK_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../esp-stub-lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../esp-stub-lib/include/esp-stub-lib
    ${CMAKE_CURRENT_BINARY_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}  # For soc/ directory
)

# Test framework sources
set(UNITY_SOURCES ${UNITY_ROOT}/src/unity.c)
set(CMOCK_SOURCES ${CMOCK_ROOT}/src/cmock.c)

# Mock generation
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.h
           ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.c
           ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_uart.h
           ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_uart.c
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/generate_mocks.sh
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../esp-stub-lib/include/esp-stub-lib/rom_wrappers.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../esp-stub-lib/include/esp-stub-lib/uart.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/generate_mocks.sh
        ${CMOCK_ROOT}/scripts/create_mock.rb
        ${CMOCK_ROOT}/lib/cmock.rb
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating CMock files"
)

add_custom_target(generate_mocks
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.h
            ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.c
            ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_uart.h
            ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_uart.c
)

# Helper function to add host tests
function(add_host_test TEST_NAME)
    set(oneValueArgs TIMEOUT)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(HOST_TEST "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Create test executable
    add_executable(${TEST_NAME}
        ${HOST_TEST_SOURCES}
        ${UNITY_SOURCES}
        ${CMOCK_SOURCES}
        ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_rom_wrappers.c
        ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_uart.c
    )

    # Depend on mock generation
    add_dependencies(${TEST_NAME} generate_mocks)

    # Add to CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    # Set timeout
    set(TEST_TIMEOUT ${HOST_TEST_TIMEOUT})
    if(NOT TEST_TIMEOUT)
        set(TEST_TIMEOUT 10)
    endif()

    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT ${TEST_TIMEOUT}
        LABELS "host_test;unit_test"
    )

    message(STATUS "Added host test: ${TEST_NAME} (timeout: ${TEST_TIMEOUT}s)")
endfunction()

# Enable testing
enable_testing()

# Add tests
add_host_test(TestSlip
    SOURCES
        TestSlip.c
        ../../src/slip.c
)

# Configuration summary
message(STATUS "Host tests configuration:")
message(STATUS "  Unity: ${UNITY_ROOT}")
message(STATUS "  CMock: ${CMOCK_ROOT}")
message(STATUS "  Mock generation: Enabled")
