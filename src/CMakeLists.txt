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

# For plugin-capable chips the base POST_BUILD writes to .base.json; the
# combined POST_BUILD (attached to the last plugin target) then writes the
# final .json.  For esp8266/esp32 (no plugin support) we write directly to
# .json because the combined step never runs for those chips.
if(TARGET_CHIP STREQUAL "esp8266" OR TARGET_CHIP STREQUAL "esp32")
    set(BASE_ONLY_JSON ${BASE_JSON})
else()
    set(BASE_ONLY_JSON ${CMAKE_BINARY_DIR}/${TARGET_CHIP}.base.json)
endif()

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py ${BASE_ELF} ${BASE_ONLY_JSON}
    COMMENT "Generating base stub JSON"
)

# ---- Two-pass plugin build (all chips except ESP8266 and ESP32) -----------
#
# Plugin addresses depend on the base stub's .text/.data sizes, which are only
# known after the first build.  On a fresh checkout:
#   1. cmake + ninja  →  builds base stub, creates plugin_addrs.cmake
#   2. cmake + ninja  →  configures and builds the plugin(s), regenerates JSON
#      with all plugin data embedded
#
# To add a new plugin, define it below following the NAND pattern:
#   1. Set _<NAME>_C to the target HAL source file path.
#   2. Call stub_add_plugin() with the plugin parameters.
#
if(NOT TARGET_CHIP STREQUAL "esp8266" AND NOT TARGET_CHIP STREQUAL "esp32")
    set(PLUGIN_ADDRS_FILE ${CMAKE_BINARY_DIR}/plugin_addrs.cmake)
    set(ESP_STUB_LIB_DIR  ${CMAKE_SOURCE_DIR}/esp-stub-lib)

    # Accumulates "--plugin name elf" pairs for the final elf2json.py invocation
    set(_PLUGIN_ELF2JSON_ARGS "")
    # Accumulates plugin ELF targets so the base-stub JSON command can depend on them
    set(_PLUGIN_TARGETS "")

    # ---- Reusable macro: stub_add_plugin ------------------------------------
    # Usage:
    #   stub_add_plugin(
    #       NAME        <plugin_name>          # lowercase, e.g. "nand"
    #       SOURCES     <src1> [<src2> ...]    # plugin-specific C sources
    #       LINKER_SCRIPT <path/to/plugin.ld>
    #       TEXT_ADDR   <cmake_var_name>       # e.g. NAND_PLUGIN_TEXT_ADDR
    #       BSS_ADDR    <cmake_var_name>       # e.g. NAND_PLUGIN_BSS_ADDR
    #   )
    macro(stub_add_plugin)
        cmake_parse_arguments(_PA "" "NAME;LINKER_SCRIPT;TEXT_ADDR;BSS_ADDR" "SOURCES" ${ARGN})

        string(TOUPPER "${_PA_NAME}" _PA_NAME_UPPER)
        set(_plugin_target stub-${TARGET_CHIP}-${_PA_NAME}-plugin)
        set(_plugin_elf    ${CMAKE_BINARY_DIR}/src/${_plugin_target}${CMAKE_EXECUTABLE_SUFFIX_C})

        # Build the plugin by compiling only the required source files
        # directly. Do NOT link against esp-stub-lib to avoid inheriting
        # its -Wl,--whole-archive flag, which would drag every object from
        # every static library into the tiny plugin binary.
        add_executable(${_plugin_target}
            ${_PA_SOURCES}
        )

        target_include_directories(${_plugin_target} PRIVATE
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

        target_link_options(${_plugin_target} PRIVATE
            # The global add_link_options sets --gc-sections which crashes
            # the xtensa linker when linking a position-dependent plugin binary.
            -Wl,--no-gc-sections
            -L${LINKER_SCRIPTS_DIR}
            "-T${_PA_LINKER_SCRIPT}"
            -Wl,--defsym,PLUGIN_TEXT_ADDR=${${_PA_TEXT_ADDR}}
            -Wl,--defsym,PLUGIN_BSS_ADDR=${${_PA_BSS_ADDR}}
            -Wl,--defsym,BASE_SLIP_SEND_FRAME=${SLIP_SEND_FRAME_ADDR}
            -Wl,--defsym,BASE_SLIP_RECV_RESET=${SLIP_RECV_RESET_ADDR}
            -Wl,--defsym,BASE_SLIP_IS_FRAME_COMPLETE=${SLIP_IS_FRAME_COMPLETE_ADDR}
            -Wl,--defsym,BASE_SLIP_GET_FRAME_DATA=${SLIP_GET_FRAME_DATA_ADDR}
            -Wl,-Map=${CMAKE_BINARY_DIR}/${_plugin_target}.map
            ${_rom_ld_flags}
        )

        # Accumulate this plugin's --plugin argument for elf2json.py
        list(APPEND _PLUGIN_ELF2JSON_ARGS "--plugin" "${_PA_NAME}" "${_plugin_elf}")
        list(APPEND _PLUGIN_TARGETS "${_plugin_target}")
    endmacro()

    # ---- Discover which plugins apply to this chip --------------------------

    set(_NAND_C ${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/src/nand.c)

    if(NOT EXISTS "${_NAND_C}")
        message(STATUS "No NAND support for ${TARGET_CHIP}, skipping NAND plugin build.")
    endif()

    # ---- Compute plugin addresses (requires base ELF to exist) --------------
    #
    # Pass all known plugin ELF paths to compute_plugin_addrs.py so that it
    # stacks addresses correctly even on chips where some plugins don't apply.
    # Missing ELFs are handled gracefully by the script (first-pass warning).

    set(_compute_plugin_args "")
    if(EXISTS "${_NAND_C}")
        list(APPEND _compute_plugin_args
            "--plugin" "nand"
            "${CMAKE_BINARY_DIR}/src/stub-${TARGET_CHIP}-nand-plugin${CMAKE_EXECUTABLE_SUFFIX_C}")
    endif()
    list(APPEND _compute_plugin_args
        "--plugin" "diag"
        "${CMAKE_BINARY_DIR}/src/stub-${TARGET_CHIP}-diag-plugin${CMAKE_EXECUTABLE_SUFFIX_C}")

    if(EXISTS "${BASE_ELF}")
        execute_process(
            COMMAND ${CMAKE_SOURCE_DIR}/tools/compute_plugin_addrs.py
                    ${BASE_ELF}
                    ${PLUGIN_ADDRS_FILE}
                    ${_compute_plugin_args}
            RESULT_VARIABLE _ret
        )
        if(NOT _ret EQUAL 0)
            message(WARNING "compute_plugin_addrs.py failed; plugin(s) not built this run")
        endif()
    endif()

    include(${PLUGIN_ADDRS_FILE} OPTIONAL RESULT_VARIABLE _have_plugin_addrs)

    if(_have_plugin_addrs)
        # ---- Pass 2: build each plugin binary -------------------------------

        if(EXISTS "${_NAND_C}")
            stub_add_plugin(
                NAME          nand
                SOURCES
                    nand_plugin.c
                    ${_NAND_C}
                    ${ESP_STUB_LIB_DIR}/src/md5.c
                    ${ESP_STUB_LIB_DIR}/src/target/common/src/md5.c
                    ${ESP_STUB_LIB_DIR}/src/rom_wrappers.c
                LINKER_SCRIPT ${LINKER_SCRIPTS_DIR}/nand_plugin.ld
                TEXT_ADDR     NAND_PLUGIN_TEXT_ADDR
                BSS_ADDR      NAND_PLUGIN_BSS_ADDR
            )
        endif()

        if(DEFINED STUB_LOGF_ADDR)
            stub_add_plugin(
                NAME          diag
                SOURCES
                    diag_plugin.c
                LINKER_SCRIPT ${LINKER_SCRIPTS_DIR}/diag_plugin.ld
                TEXT_ADDR     DIAG_PLUGIN_TEXT_ADDR
                BSS_ADDR      DIAG_PLUGIN_BSS_ADDR
            )
            # Wire the base stub's stub_logf pointer address into the plugin.
            target_link_options(stub-${TARGET_CHIP}-diag-plugin PRIVATE
                -Wl,--defsym,BASE_STUB_LOGF_PTR=${STUB_LOGF_ADDR}
            )
        else()
            message(STATUS "stub_logf symbol not found in base stub — diag plugin not built this pass")
        endif()

        # ---- Post-build: regenerate JSON with all plugin data embedded ------
        if(_PLUGIN_TARGETS)
            # Rebuild JSON after the last plugin ELF is produced.
            # We attach the command to the last plugin target so it runs once
            # all plugins are linked.
            # The base POST_BUILD already wrote .base.json; the combined step
            # writes the final .json — so exactly one writer per output file.
            list(GET _PLUGIN_TARGETS -1 _last_plugin_target)
            add_custom_command(TARGET ${_last_plugin_target} POST_BUILD
                COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py
                        ${BASE_ELF}
                        ${BASE_JSON}
                        ${_PLUGIN_ELF2JSON_ARGS}
                COMMENT "Generating combined stub+plugin(s) JSON"
            )
        else()
            # Plugin-capable chip but no plugins apply (e.g. NAND C source absent).
            # The base POST_BUILD wrote .base.json; promote it to .json so that
            # the output filename is consistent regardless of whether plugins built.
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy ${BASE_ONLY_JSON} ${BASE_JSON}
                COMMENT "Promoting base.json → ${TARGET_CHIP}.json (no plugins)"
            )
        endif()
    else()
        # plugin_addrs.cmake not yet generated (first cmake+build pass).
        # The base POST_BUILD already wrote .base.json; promote it so that
        # esptool can use the stub even before plugins are configured.
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${BASE_ONLY_JSON} ${BASE_JSON}
            COMMENT "Promoting base.json → ${TARGET_CHIP}.json (first pass)"
        )
        message(STATUS
            "plugin_addrs.cmake not found — build once to generate base stub ELF, "
            "then re-run cmake to configure the plugin(s).")
    endif()
endif()
