add_executable(${TARGET_NAME}
    "main.c"
    "slip.c"
    "command_handler.c"
    "transport.c"
)

target_link_options(${TARGET_NAME} PRIVATE
    -L${LINKER_SCRIPTS_DIR}
    "-T${CHIP_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
)

target_link_libraries(${TARGET_NAME} PRIVATE esp-stub-lib)

# Post-build: generate base JSON (no plugin yet)
set(BASE_ELF ${CMAKE_BINARY_DIR}/src/${TARGET_NAME}${CMAKE_EXECUTABLE_SUFFIX_C})
set(BASE_JSON ${CMAKE_BINARY_DIR}/${TARGET_CHIP}.json)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py ${BASE_ELF} ${BASE_JSON}
    COMMENT "Generating base stub JSON"
)

# ---- Two-pass NAND plugin build (all chips except ESP8266 and ESP32) -------
#
# Plugin addresses depend on the base stub's .text/.data sizes, which are only
# known after the first build.  On a fresh checkout:
#   1. cmake + ninja  →  builds base stub, creates plugin_addrs.cmake
#   2. cmake + ninja  →  configures and builds the plugin, regenerates JSON
#      with plugin data embedded
#
if(NOT TARGET_CHIP STREQUAL "esp8266" AND NOT TARGET_CHIP STREQUAL "esp32")
    set(PLUGIN_ADDRS_FILE ${CMAKE_BINARY_DIR}/plugin_addrs.cmake)
    set(ESP_STUB_LIB_DIR  ${CMAKE_SOURCE_DIR}/esp-stub-lib)

    # Only build the NAND plugin for chips that provide nand.c
    set(_NAND_C     ${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/src/nand.c)

    if(NOT EXISTS "${_NAND_C}")
        message(STATUS "No NAND support for ${TARGET_CHIP}, skipping plugin build.")
    else()
        # If the base ELF already exists (re-configure after first build), compute
        # plugin addresses immediately so the plugin target is created this run.
        if(EXISTS "${BASE_ELF}")
            execute_process(
                COMMAND ${CMAKE_SOURCE_DIR}/tools/compute_plugin_addrs.py
                        ${BASE_ELF}
                        ${PLUGIN_ADDRS_FILE}
                RESULT_VARIABLE _ret
            )
            if(NOT _ret EQUAL 0)
                message(WARNING "compute_plugin_addrs.py failed; plugin not built this run")
            endif()
        endif()

        include(${PLUGIN_ADDRS_FILE} OPTIONAL RESULT_VARIABLE _have_plugin_addrs)

        if(_have_plugin_addrs)
            # ---- Pass 2: NAND plugin binary ---------------------------------
            set(NAND_PLUGIN_NAME stub-${TARGET_CHIP}-nand-plugin)
            set(NAND_PLUGIN_ELF  ${CMAKE_BINARY_DIR}/src/${NAND_PLUGIN_NAME}${CMAKE_EXECUTABLE_SUFFIX_C})
            set(PLUGIN_LD        ${LINKER_SCRIPTS_DIR}/nand_plugin.ld)

            # Build the plugin by compiling only the required source files
            # directly. Do NOT link against esp-stub-lib to avoid inheriting
            # its -Wl,--whole-archive flag, which would drag every object from
            # every static library into the tiny plugin binary.
            add_executable(${NAND_PLUGIN_NAME}
                nand_plugin.c
                ${_NAND_C}
                ${ESP_STUB_LIB_DIR}/src/md5.c
                ${ESP_STUB_LIB_DIR}/src/target/common/src/md5.c
                ${ESP_STUB_LIB_DIR}/src/rom_wrappers.c
            )

            target_include_directories(${NAND_PLUGIN_NAME} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${ESP_STUB_LIB_DIR}/include
                ${ESP_STUB_LIB_DIR}/include/esp-stub-lib
                ${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/include
                ${ESP_STUB_LIB_DIR}/src/target/base/include
            )

            # Collect all ROM LD scripts for this chip (symbol resolution only)
            file(GLOB _rom_ld_scripts
                "${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/ld/*.rom.ld"
                "${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/ld/*.rom.*.ld"
            )
            set(_rom_ld_flags "")
            foreach(_s ${_rom_ld_scripts})
                list(APPEND _rom_ld_flags "-T${_s}")
            endforeach()

            target_link_options(${NAND_PLUGIN_NAME} PRIVATE
                # The global add_link_options sets --gc-sections which crashes
                # the xtensa linker when linking a position-dependent plugin binary.
                -Wl,--no-gc-sections
                -L${LINKER_SCRIPTS_DIR}
                "-T${PLUGIN_LD}"
                -Wl,--defsym,PLUGIN_TEXT_ADDR=${NAND_PLUGIN_TEXT_ADDR}
                -Wl,--defsym,PLUGIN_BSS_ADDR=${NAND_PLUGIN_BSS_ADDR}
                -Wl,--defsym,BASE_SLIP_SEND_FRAME=${SLIP_SEND_FRAME_ADDR}
                -Wl,--defsym,BASE_SLIP_RECV_RESET=${SLIP_RECV_RESET_ADDR}
                -Wl,--defsym,BASE_SLIP_IS_FRAME_COMPLETE=${SLIP_IS_FRAME_COMPLETE_ADDR}
                -Wl,--defsym,BASE_SLIP_GET_FRAME_DATA=${SLIP_GET_FRAME_DATA_ADDR}
                -Wl,-Map=${CMAKE_BINARY_DIR}/${NAND_PLUGIN_NAME}.map
                ${_rom_ld_flags}
            )

            # Post-build: regenerate JSON with plugin data embedded
            add_custom_command(TARGET ${NAND_PLUGIN_NAME} POST_BUILD
                COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py
                        ${BASE_ELF}
                        ${BASE_JSON}
                        --plugin nand ${NAND_PLUGIN_ELF}
                COMMENT "Generating combined stub+nand-plugin JSON"
            )
        else()
            message(STATUS
                "plugin_addrs.cmake not found — build once to generate base stub ELF, "
                "then re-run cmake to configure the NAND plugin.")
        endif()
    endif()
endif()
