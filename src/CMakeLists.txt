add_executable(${TARGET_NAME}
    "main.c"
    "frame_buffer.c"
    "slip.c"
    "command_handler.c"
    "transport.c"
)

# The UART clock/APB boost is verified safe only on ESP32 (its stub_target_clock_init sets DBIAS;
# the ESP32-S3 UART path is unstable without it). Define ESP32 (same style as the existing ESP8266
# chip macro) so main.c enables the boost for UART on esp32 only; other targets keep upstream
# USB-only clock behaviour. The conditional erase and larger flash block are chip-independent.
if(TARGET_CHIP STREQUAL "esp32")
    target_compile_definitions(${TARGET_NAME} PRIVATE ESP32)
endif()

target_link_options(${TARGET_NAME} PRIVATE
    -L${LINKER_SCRIPTS_DIR}
    "-T${CHIP_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
)

target_link_libraries(${TARGET_NAME} PRIVATE esp-stub-lib)

# Base stub ELF — referenced as a dependency by the plugin-address and JSON
# custom commands below.
set(BASE_ELF $<TARGET_FILE:${TARGET_NAME}>)
set(STUB_JSON ${CMAKE_BINARY_DIR}/${TARGET_CHIP}.json)

# ---------------------------------------------------------------------------
# Plugins
#
# A plugin must be linked at a fixed address that sits right after the base
# stub (and after any earlier plugins).  Those addresses are only known once
# the base stub is linked, so they are computed at build time:
#
#   stub ELF ─► compute_plugin_addrs.py ─► <name>_plugin_addrs.ld ─► plugin ELF ─► JSON
#
# compute_plugin_addrs.py emits a linker fragment (PLUGIN_TEXT_ADDR /
# PLUGIN_BSS_ADDR) that the plugin's linker script pulls in via -T.  The plugin
# link gains a LINK_DEPENDS on that fragment, so it relinks whenever the base
# stub's size changes.  Plugins stack: each plugin's fragment depends on the
# base stub plus every preceding plugin ELF (passed via --after).
#
# To add a plugin: drop in its sources + linker script and call
# stub_add_plugin() following the NAND example below.
# ---------------------------------------------------------------------------

# Populated by stub_add_plugin(); empty when no plugin applies, which collapses
# the JSON step (below) to the same plain `elf2json.py <stub> <chip>.json` used
# by esp8266/esp32.
#   _plugin_json_args : --plugin <name> <elf> ... fed to elf2json.py
#   _plugin_elfs      : plugin ELF files in load order — read inside the macro
#                       (= earlier plugins, for --after) and after all calls
#                       (= every plugin, for the JSON dependency)
set(_plugin_json_args "")
set(_plugin_elfs      "")

if(NOT TARGET_CHIP STREQUAL "esp8266" AND NOT TARGET_CHIP STREQUAL "esp32")
    set(ESP_STUB_LIB_DIR ${CMAKE_SOURCE_DIR}/esp-stub-lib)

    # ---- Reusable macro: stub_add_plugin ------------------------------------
    # Usage:
    #   stub_add_plugin(
    #       NAME          <plugin_name>          # lowercase, e.g. "nand"
    #       SOURCES       <src1> [<src2> ...]    # plugin-specific C sources
    #       LINKER_SCRIPT <path/to/plugin.ld>
    #   )
    macro(stub_add_plugin)
        cmake_parse_arguments(_PA "" "NAME;LINKER_SCRIPT" "SOURCES" ${ARGN})

        set(_plugin_target   stub-${TARGET_CHIP}-${_PA_NAME}-plugin)
        set(_plugin_elf      $<TARGET_FILE:${_plugin_target}>)
        set(_plugin_addrs_ld ${CMAKE_BINARY_DIR}/${_PA_NAME}_plugin_addrs.ld)

        # Build-time address computation: stack this plugin after the base stub
        # and any earlier plugins.  _plugin_elfs holds only those earlier plugins
        # here — this plugin's own ELF is appended below, after it is read.
        set(_after_args "")
        foreach(_prev ${_plugin_elfs})
            list(APPEND _after_args --after ${_prev})
        endforeach()

        add_custom_command(
            OUTPUT  ${_plugin_addrs_ld}
            COMMAND ${CMAKE_SOURCE_DIR}/tools/compute_plugin_addrs.py
                    ${BASE_ELF} ${_plugin_addrs_ld} ${_after_args}
            DEPENDS ${BASE_ELF} ${_plugin_elfs}
                    ${CMAKE_SOURCE_DIR}/tools/compute_plugin_addrs.py
            COMMENT "Computing ${_PA_NAME} plugin load address"
            VERBATIM
        )
        add_custom_target(${_plugin_target}-addrs DEPENDS ${_plugin_addrs_ld})

        # Build the plugin from its own sources only.  Do NOT link esp-stub-lib:
        # its -Wl,--whole-archive would drag every object from every static
        # library into the tiny plugin binary.
        add_executable(${_plugin_target} ${_PA_SOURCES})

        target_include_directories(${_plugin_target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${ESP_STUB_LIB_DIR}/include
            ${ESP_STUB_LIB_DIR}/include/esp-stub-lib
            ${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/include
            ${ESP_STUB_LIB_DIR}/src/target/base/include
        )

        # Collect ROM LD scripts for this chip (symbol resolution only).
        file(GLOB _rom_ld_scripts
            "${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/ld/*.rom.ld"
            "${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/ld/*.rom.*.ld"
        )
        set(_rom_ld_flags "")
        foreach(_s ${_rom_ld_scripts})
            list(APPEND _rom_ld_flags "-T${_s}")
        endforeach()

        # Links with the global --gc-sections; handlers reachable only through
        # the FPT are rooted by EXTERN() in the plugin linker script.
        target_link_options(${_plugin_target} PRIVATE
            -L${LINKER_SCRIPTS_DIR}
            # Generated fragment first: defines PLUGIN_TEXT_ADDR / PLUGIN_BSS_ADDR
            # used by the plugin linker script's MEMORY block.
            "-T${_plugin_addrs_ld}"
            "-T${_PA_LINKER_SCRIPT}"
            -Wl,-Map=${CMAKE_BINARY_DIR}/${_plugin_target}.map
            ${_rom_ld_flags}
        )

        # Build after the base stub (its load address comes from the linked base
        # stub, and with LTO the base must be built first) and after the address
        # fragment; relink whenever that fragment changes.
        add_dependencies(${_plugin_target} ${TARGET_NAME} ${_plugin_target}-addrs)
        set_target_properties(${_plugin_target} PROPERTIES LINK_DEPENDS ${_plugin_addrs_ld})

        # Record this plugin: its --plugin arg for the JSON step, and its ELF
        # (which the JSON depends on, and which later plugins stack after).
        list(APPEND _plugin_json_args --plugin ${_PA_NAME} ${_plugin_elf})
        list(APPEND _plugin_elfs      ${_plugin_elf})
    endmacro()

    # ---- Plugins available for this chip ------------------------------------
    set(_NAND_C ${ESP_STUB_LIB_DIR}/src/target/${ESP_TARGET}/src/nand.c)
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
        )
    else()
        message(STATUS "No NAND support for ${TARGET_CHIP}, skipping NAND plugin build.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Stub JSON — one writer for every chip.  With no plugins the arg/dep lists are
# empty and this is just `elf2json.py <stub.elf> <chip>.json`; with plugins it
# also embeds each plugin's .text/.bss and handler offsets.
# ---------------------------------------------------------------------------
add_custom_command(
    OUTPUT  ${STUB_JSON}
    COMMAND ${CMAKE_SOURCE_DIR}/tools/elf2json.py ${BASE_ELF} ${STUB_JSON} ${_plugin_json_args}
    DEPENDS ${BASE_ELF} ${_plugin_elfs} ${CMAKE_SOURCE_DIR}/tools/elf2json.py
    COMMENT "Generating ${TARGET_CHIP}.json"
    VERBATIM
)
add_custom_target(${TARGET_NAME}-json ALL DEPENDS ${STUB_JSON})
