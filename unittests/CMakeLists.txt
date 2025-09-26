cmake_minimum_required(VERSION 4.0)
set (CMAKE_CXX_STANDARD 17)

project(test)
enable_testing()

find_package(GTest REQUIRED)

set(ESP_IDF_COMPONENTS "/home/store/embedded/esp32/esp-idf/components")

set(MAIN_PROJECT_DIR ${PROJECT_SOURCE_DIR}/..)

INCLUDE_DIRECTORIES(
    ${PROJECT_SOURCE_DIR}/mocks
    ${PROJECT_SOURCE_DIR}/support
    ${GTEST_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MAIN_PROJECT_DIR}/esp-stub-lib/include
    ${MAIN_PROJECT_DIR}/src
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -DUNIT_TESTS")

# ========================================================
set(TEST_NAME test_slip)
add_executable(${TEST_NAME}
    ${TEST_NAME}.cpp
    ${MAIN_PROJECT_DIR}/src/slip.c
    ${PROJECT_SOURCE_DIR}/mocks/rom_functions.cpp

)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
target_link_libraries(${TEST_NAME} ${GTEST_BOTH_LIBRARIES})
