add_executable(${TARGET_NAME}
    "main.c"
    "slip.c"
    "command_handler.c"
)

target_compile_options(${TARGET_NAME} PRIVATE
    ${ESP_FIRMWARE_C_FLAGS}
    ${TARGET_FLAGS}
)

target_link_options(${TARGET_NAME} PRIVATE
    ${ESP_FIRMWARE_LINKER_FLAGS}
    ${TARGET_FLAGS}
    "-T${CHIP_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
)

target_link_libraries(${TARGET_NAME} PRIVATE esp-stub-lib)

# Add post-build JSON generation
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py ${CMAKE_BINARY_DIR}/src/${TARGET_NAME}${CMAKE_EXECUTABLE_SUFFIX_C} ${CMAKE_BINARY_DIR}/${TARGET_CHIP}.json
    COMMENT "Running elf2json.py to produce a JSON file output"
)

if(${TARGET_CHIP} STREQUAL "esp8266")
    # zlib library will be available in esp-stub-lib, so miniz probably won't be needed.
    add_library(miniz_obj OBJECT miniz.c)

    target_compile_options(miniz_obj PRIVATE
        # Disable specific warnings that miniz cannot satisfy
        -Wno-conversion         # Disable implicit conversion warnings
        -Wno-sign-conversion    # Disable signed/unsigned conversion warnings
    )

    target_sources(${TARGET_NAME} PRIVATE $<TARGET_OBJECTS:miniz_obj>)
    message(STATUS "Including miniz with custom safe flags for ${TARGET_CHIP}")
endif()
