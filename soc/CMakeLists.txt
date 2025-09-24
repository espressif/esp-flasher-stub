cmake_minimum_required(VERSION 3.28)

add_library(soc STATIC)

target_sources(soc PRIVATE
    common/soc.c
    common/watchdog_common.c
)

target_sources(soc PRIVATE targets/${TARGET_CHIP}.c)

# Include directories
target_include_directories(soc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
