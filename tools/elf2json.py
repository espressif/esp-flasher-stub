#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT

import argparse
import base64
import json
import sys

from typing import Dict, Optional, Union

try:
    import esptool
except ImportError:
    raise SystemExit('Esptool not found. Please check the README for installation instructions.')

try:
    from elftools.elf.elffile import ELFFile as PyELFFile
    from elftools.elf.sections import SymbolTableSection
except ImportError:
    raise SystemExit('pyelftools not found. Install with: pip install pyelftools')


# Registry of plugin handler symbols.
# Add a new entry here when adding a new plugin.
PLUGIN_HANDLER_SYMBOLS: Dict[str, Dict[str, str]] = {
    'nand': {
        '0xD5': 'nand_plugin_attach',
        '0xD6': 'nand_plugin_read_bbm',
        '0xD7': 'nand_plugin_write_bbm',
        '0xD8': 'nand_plugin_read_flash',
        '0xD9': 'nand_plugin_write_flash_begin',
        '0xDA': 'nand_plugin_write_flash_data',
        '0xDB': 'nand_plugin_erase_flash',
        '0xDC': 'nand_plugin_erase_region',
        '0xDD': 'nand_plugin_read_page_debug',
    },
}

PLUGIN_FIRST_OPCODE = 0xD5
PLUGIN_TABLE_ENTRIES = 27  # Must match PLUGIN_TABLE_SIZE in src/plugin_table.h


class ByteEncoder(json.JSONEncoder):
    """Custom JSON encoder to handle bytes."""

    def default(self, obj):
        if isinstance(obj, bytes):
            return base64.b64encode(obj).decode('ascii')
        return super().default(obj)


def _get_symbol_addr(elf: PyELFFile, name: str) -> Optional[int]:
    """Return the VMA of symbol *name* in a pyelftools ELFFile, or None."""
    for section in elf.iter_sections():
        if not isinstance(section, SymbolTableSection):
            continue
        for sym in section.iter_symbols():
            if sym.name == name and sym['st_value'] != 0:
                return int(sym['st_value'])
    return None


def get_stub_sections(
    elf_file: str, plugin_name: Optional[str] = None, plugin_elf_file: Optional[str] = None
) -> Dict[str, Union[int, bytes]]:
    elf = esptool.bin_image.ELFFile(elf_file)

    t = elf.get_section('.text')
    stub = {
        'entry': elf.entrypoint,
        'text': t.data,
        'text_start': t.addr,
    }
    stub_text_len = len(t.data)

    data_addr = None
    try:
        d = elf.get_section('.data')
        stub['data'] = d.data
        stub['data_start'] = d.addr
        data_addr = d.addr
    except ValueError:
        print(f'WARNING: {elf_file} does not contain a .data section.', file=sys.stderr)

    for s in elf.nobits_sections:
        if s.name == '.bss':
            stub['bss_start'] = s.addr
            break
    else:
        print(f'WARNING: {elf_file} does not contain a .bss section.', file=sys.stderr)

    # Align .text to 4-byte boundary
    bytes_overreach = stub_text_len % 4
    if bytes_overreach != 0:
        stub['text'] += b'\x00' * (4 - bytes_overreach)

    # ---- Plugin support -----------------------------------------------------
    if plugin_name is not None and plugin_elf_file is not None:
        if data_addr is None:
            print('WARNING: Cannot add plugin metadata — no .data section in base stub.', file=sys.stderr)
        else:
            # Find plugin_table symbol to compute FPT offset
            plugin_table_offset = None
            with open(elf_file, 'rb') as f:
                py_elf = PyELFFile(f)
                addr = _get_symbol_addr(py_elf, 'plugin_table')
                if addr is not None:
                    plugin_table_offset = addr - data_addr

            if plugin_table_offset is None:
                print(
                    'ERROR: plugin_table symbol not found in base stub — cannot generate plugin metadata.',
                    file=sys.stderr,
                )
                raise SystemExit(1)

            stub['plugin_first_opcode'] = PLUGIN_FIRST_OPCODE
            stub['plugin_table_offset'] = plugin_table_offset
            stub['plugin_table_entries'] = PLUGIN_TABLE_ENTRIES

            # Extract plugin text, bss, and handler offsets
            with open(plugin_elf_file, 'rb') as pf:
                p_elf = PyELFFile(pf)
                p_text_sec = p_elf.get_section_by_name('.text')
                p_bss_sec = p_elf.get_section_by_name('.bss')

                if p_text_sec is None:
                    raise SystemExit(f'ERROR: .text not found in plugin ELF {plugin_elf_file}')

                p_text_data = bytes(p_text_sec.data())
                p_text_start = p_text_sec['sh_addr']
                p_bss_size = p_bss_sec['sh_size'] if p_bss_sec else 0

                # Align plugin .text to 4-byte boundary: Xtensa IRAM only supports
                # 32-bit-width stores; the ROM memcpy tail-byte loop would fault.
                overreach = len(p_text_data) % 4
                if overreach != 0:
                    p_text_data += b'\x00' * (4 - overreach)

                handler_map = PLUGIN_HANDLER_SYMBOLS.get(plugin_name, {})
                handlers: Dict[str, int] = {}
                for opcode_str, sym_name in handler_map.items():
                    sym_addr = _get_symbol_addr(p_elf, sym_name)
                    if sym_addr is None:
                        print(f'WARNING: handler symbol {sym_name} not found in plugin ELF', file=sys.stderr)
                        continue
                    handlers[opcode_str] = sym_addr - p_text_start

            stub['plugins'] = {
                plugin_name: {
                    'text': p_text_data,
                    'text_start': p_text_start,
                    'bss_size': p_bss_size,
                    'handlers': handlers,
                }
            }

    return stub


def main():
    parser = argparse.ArgumentParser(description='Convert flasher stub ELF to JSON')
    parser.add_argument('elf_file', help='Base stub ELF file input')
    parser.add_argument('json_file', help='JSON file output')
    parser.add_argument(
        '--plugin', nargs=2, metavar=('NAME', 'PLUGIN_ELF'), help='Embed plugin data: --plugin <name> <plugin_elf>'
    )
    args = parser.parse_args()

    plugin_name = None
    plugin_elf = None
    if args.plugin:
        plugin_name, plugin_elf = args.plugin

    stub = get_stub_sections(args.elf_file, plugin_name, plugin_elf)

    with open(args.json_file, 'w') as json_file:
        json.dump(stub, json_file, cls=ByteEncoder, indent=4)

    if plugin_name:
        print(f'Written {args.json_file} with {plugin_name} plugin embedded.')


if __name__ == '__main__':
    main()
