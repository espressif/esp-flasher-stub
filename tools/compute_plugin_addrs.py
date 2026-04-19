#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT
"""
Compute plugin load addresses and base stub symbol addresses from a built
base stub ELF.  Writes a CMake-includable file with the results.

Supports multiple plugins via repeated --plugin NAME ELF arguments.  Each
plugin is stacked after the previous one; the first plugin starts immediately
after the base stub.
"""

import argparse
import sys

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
except ImportError:
    raise SystemExit('pyelftools not found. Install with: pip install pyelftools')


def get_symbol_addr(elf, name):
    """Return the VMA of symbol *name*, or None if not found."""
    for section in elf.iter_sections():
        if not isinstance(section, SymbolTableSection):
            continue
        for sym in section.iter_symbols():
            if sym.name == name and sym['st_value'] != 0:
                return sym['st_value']
    return None


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def main():
    parser = argparse.ArgumentParser(description='Compute plugin load addresses from a base stub ELF.')
    parser.add_argument(
        'base_stub_elf',
        help='Path to the base stub ELF file',
    )
    parser.add_argument(
        'output_cmake',
        help='Path to the output CMake file',
    )
    parser.add_argument(
        '--plugin',
        nargs=2,
        metavar=('NAME', 'PLUGIN_ELF'),
        action='append',
        default=[],
        help='Plugin to allocate addresses for: --plugin <name> <plugin_elf>. '
        'May be repeated; plugins are stacked in order.',
    )
    parser.add_argument(
        '--reserve',
        nargs=3,
        metavar=('NAME', 'TEXT_SIZE', 'BSS_SIZE'),
        action='append',
        default=[],
        help='Fallback reservation sizes (hex or decimal) for a plugin whose ELF is not yet built: '
        '--reserve <name> <text_size> <bss_size>. May be repeated.',
    )
    args = parser.parse_args()

    # Parse --reserve entries into a dict keyed by lower-case plugin name
    reserves = {}
    for res_name, res_text, res_bss in args.reserve:
        try:
            reserves[res_name.lower()] = (int(res_text, 0), int(res_bss, 0))
        except ValueError:
            sys.exit(f'ERROR: --reserve {res_name}: TEXT_SIZE and BSS_SIZE must be integers (hex or decimal)')

    with open(args.base_stub_elf, 'rb') as f:
        elf = ELFFile(f)

        text_sec = elf.get_section_by_name('.text')
        data_sec = elf.get_section_by_name('.data')
        bss_sec = elf.get_section_by_name('.bss')

        if text_sec is None:
            sys.exit('ERROR: .text section not found in base stub ELF')
        if data_sec is None:
            sys.exit('ERROR: .data section not found in base stub ELF')
        if bss_sec is None:
            sys.exit('ERROR: .bss section not found in base stub ELF')

        text_end = text_sec['sh_addr'] + text_sec['sh_size']
        data_end = data_sec['sh_addr'] + data_sec['sh_size']
        bss_end = bss_sec['sh_addr'] + bss_sec['sh_size']

        # First plugin starts immediately after the base stub
        next_text_addr = align_up(text_end, 16)
        # Place plugin BSS after both .data and .bss to avoid overlap
        next_bss_addr = align_up(max(data_end, bss_end), 4)

        # Symbols forwarded to the plugin via PROVIDE in the linker script
        slip_syms = [
            'slip_send_frame',
            'slip_recv_reset',
            'slip_is_frame_complete',
            'slip_get_frame_data',
        ]

        sym_addrs = {}
        for name in slip_syms:
            addr = get_symbol_addr(elf, name)
            if addr is None:
                sys.exit(f"ERROR: symbol '{name}' not found in base stub ELF")
            sym_addrs[name] = addr

        # Optional symbols — present only when the corresponding feature is compiled in.
        # Written to the cmake file as <NAME_UPPER>_ADDR if found.
        optional_syms = [
            'stub_logf',  # function pointer in base stub BSS; consumed by diag plugin
        ]
        for name in optional_syms:
            addr = get_symbol_addr(elf, name)
            if addr is not None:
                sym_addrs[name] = addr

    lines = [
        f'set(SLIP_SEND_FRAME_ADDR         0x{sym_addrs["slip_send_frame"]:08X})',
        f'set(SLIP_RECV_RESET_ADDR         0x{sym_addrs["slip_recv_reset"]:08X})',
        f'set(SLIP_IS_FRAME_COMPLETE_ADDR  0x{sym_addrs["slip_is_frame_complete"]:08X})',
        f'set(SLIP_GET_FRAME_DATA_ADDR     0x{sym_addrs["slip_get_frame_data"]:08X})',
    ]
    optional_sym_cmake_names = {
        'stub_logf': 'STUB_LOGF_ADDR',
    }
    for name in optional_syms:
        if name in sym_addrs:
            cmake_var = optional_sym_cmake_names.get(name, name.upper() + '_ADDR')
            lines.append(f'set({cmake_var:<32} 0x{sym_addrs[name]:08X})')

    # Allocate addresses sequentially for each requested plugin.
    # If no --plugin arguments are given (legacy/first-pass invocation), fall
    # back to computing NAND addresses under the old variable names so that
    # existing CMakeLists.txt that includes this file without --plugin still works.
    if not args.plugin:
        lines.insert(0, f'set(NAND_PLUGIN_BSS_ADDR  0x{next_bss_addr:08X})')
        lines.insert(0, f'set(NAND_PLUGIN_TEXT_ADDR 0x{next_text_addr:08X})')
    else:
        for plugin_name, plugin_elf in args.plugin:
            name_upper = plugin_name.upper()
            lines.append(f'set({name_upper}_PLUGIN_TEXT_ADDR 0x{next_text_addr:08X})')
            lines.append(f'set({name_upper}_PLUGIN_BSS_ADDR  0x{next_bss_addr:08X})')

            # Advance pointers past this plugin's sections
            try:
                with open(plugin_elf, 'rb') as pf:
                    p_elf = ELFFile(pf)
                    p_text_sec = p_elf.get_section_by_name('.text')
                    p_bss_sec = p_elf.get_section_by_name('.bss')
                    p_text_size = p_text_sec['sh_size'] if p_text_sec else 0
                    p_bss_size = p_bss_sec['sh_size'] if p_bss_sec else 0
            except FileNotFoundError:
                # Plugin ELF not built yet.  Check for an explicit --reserve entry.
                reserve = reserves.get(plugin_name.lower())
                if reserve is not None:
                    p_text_size, p_bss_size = reserve
                    print(
                        f"WARNING: plugin ELF '{plugin_elf}' not found; "
                        f'using reserved sizes TEXT=0x{p_text_size:X} BSS=0x{p_bss_size:X} for {plugin_name!r}.',
                        file=sys.stderr,
                    )
                else:
                    # No reserve provided.  If there are subsequent plugins their
                    # addresses would overlap — fail fast with an actionable message.
                    remaining = args.plugin[args.plugin.index([plugin_name, plugin_elf]) + 1 :]
                    if remaining:
                        sys.exit(
                            f"ERROR: plugin ELF '{plugin_elf}' not found and no --reserve entry for {plugin_name!r}. "
                            f'Subsequent plugins {[n for n, _ in remaining]} would receive overlapping addresses. '
                            f'Build {plugin_name!r} first, or pass --reserve {plugin_name} <text_size> <bss_size>.'
                        )
                    # Single (last) plugin missing — harmless, emit warning only.
                    print(
                        f"WARNING: plugin ELF '{plugin_elf}' not found; "
                        f'subsequent plugin addresses may be incorrect (first-pass build?)',
                        file=sys.stderr,
                    )
                    p_text_size = 0
                    p_bss_size = 0

            # Align text to 16 bytes (same rule as base→first-plugin gap)
            next_text_addr = align_up(next_text_addr + p_text_size, 16)
            next_bss_addr = align_up(next_bss_addr + p_bss_size, 4)

    with open(args.output_cmake, 'w') as out:
        out.write('# Auto-generated by compute_plugin_addrs.py — do not edit\n')
        for line in lines:
            out.write(line + '\n')

    print(f'Plugin addresses written to {args.output_cmake}')
    for line in lines:
        print(f'  {line}')


if __name__ == '__main__':
    main()
