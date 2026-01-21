add_executable(${TARGET_NAME}
    "main.c"
    "slip.c"
    "command_handler.c"
    "transport.c"
    "nand.c"
    "spi2_nand.c"
)

target_link_options(${TARGET_NAME} PRIVATE
    -L${LINKER_SCRIPTS_DIR}
    "-T${CHIP_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
)

target_link_libraries(${TARGET_NAME} PRIVATE esp-stub-lib)

# Add post-build JSON generation
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py ${CMAKE_BINARY_DIR}/src/${TARGET_NAME}${CMAKE_EXECUTABLE_SUFFIX_C} ${CMAKE_BINARY_DIR}/${TARGET_CHIP}.json
    COMMENT "Running elf2json.py to produce a JSON file output"
)
