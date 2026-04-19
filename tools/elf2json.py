#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT

import argparse
import base64
import json
import sys
from pathlib import Path

from typing import Dict, Optional, Union

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
        '0xDE': 'nand_plugin_write_flash_end',
        '0xDB': 'nand_plugin_erase_flash',
        '0xDC': 'nand_plugin_erase_region',
        '0xDD': 'nand_plugin_read_page_debug',
    },
    'diag': {
        '0xDF': 'diag_plugin_log_read',
    },
}

PLUGIN_FIRST_OPCODE = 0xD5
PLUGIN_LAST_OPCODE = 0xEF  # Must match last reserved opcode in src/plugin_table.h
PLUGIN_TABLE_ENTRIES = (
    PLUGIN_LAST_OPCODE - PLUGIN_FIRST_OPCODE + 1
)  # Must match PLUGIN_TABLE_SIZE in src/plugin_table.h


class ByteEncoder(json.JSONEncoder):
    """Custom JSON encoder to handle bytes."""

    def default(self, o):
        if isinstance(o, bytes):
            return base64.b64encode(o).decode('ascii')
        return super().default(o)


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
    elf_file: str,
    plugin_name: Optional[str] = None,
    plugin_elf_file: Optional[str] = None,
    plugins: Optional[list] = None,
) -> Dict[str, Union[int, bytes]]:
    with open(elf_file, 'rb') as f:
        elf = PyELFFile(f)

        t = elf.get_section_by_name('.text')
        if t is None:
            raise SystemExit(f'ERROR: .text section not found in {elf_file}')
        stub = {
            'entry': elf.header.e_entry,
            'text': bytes(t.data()),
            'text_start': t['sh_addr'],
        }
        stub_text_len = len(stub['text'])

        data_addr = None
        d = elf.get_section_by_name('.data')
        if d is not None:
            data_bytes = bytes(d.data())
            overreach = len(data_bytes) % 4
            if overreach:
                data_bytes += b'\x00' * (4 - overreach)
            stub['data'] = data_bytes
            stub['data_start'] = d['sh_addr']
            data_addr = d['sh_addr']
        else:
            print(f'WARNING: {elf_file} does not contain a .data section.', file=sys.stderr)

        bss = elf.get_section_by_name('.bss')
        if bss is not None:
            stub['bss_start'] = bss['sh_addr']
        else:
            print(f'WARNING: {elf_file} does not contain a .bss section.', file=sys.stderr)

    # Align .text to 4-byte boundary
    bytes_overreach = stub_text_len % 4
    if bytes_overreach != 0:
        stub['text'] += b'\x00' * (4 - bytes_overreach)

    # ---- Plugin support -----------------------------------------------------
    # Normalise: accept either the legacy (plugin_name, plugin_elf_file) pair
    # or the new list-of-pairs form via the `plugins` argument.
    if plugins is None:
        plugins = []
    if plugin_name is not None and plugin_elf_file is not None:
        plugins = [(plugin_name, plugin_elf_file)] + [p for p in plugins if p != (plugin_name, plugin_elf_file)]

    if plugins:
        if data_addr is None:
            print('WARNING: Cannot add plugin metadata — no .data section in base stub.', file=sys.stderr)
        else:
            # Find plugin_table symbol to compute FPT offset (done once for all plugins)
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

            # Validate that plugin_table resides within the base stub .data section.
            data_len = len(stub['data'])
            if not (0 <= plugin_table_offset < data_len):
                print(
                    f'ERROR: plugin_table offset {plugin_table_offset} is outside base stub .data '
                    f'section (size {data_len}). Cannot generate plugin metadata.',
                    file=sys.stderr,
                )
                raise SystemExit(1)
            stub['plugin_first_opcode'] = PLUGIN_FIRST_OPCODE
            stub['plugin_table_offset'] = plugin_table_offset
            stub['plugin_table_entries'] = PLUGIN_TABLE_ENTRIES
            stub['plugins'] = {}

            for p_name, p_elf_file in plugins:
                # Extract plugin text, bss, and handler offsets
                if not Path(p_elf_file).is_file():
                    raise SystemExit(f'ERROR: Plugin ELF not found: {p_elf_file}')
                with open(p_elf_file, 'rb') as pf:
                    p_elf = PyELFFile(pf)
                    p_text_sec = p_elf.get_section_by_name('.text')
                    p_bss_sec = p_elf.get_section_by_name('.bss')

                    if p_text_sec is None:
                        raise SystemExit(f'ERROR: .text not found in plugin ELF {p_elf_file}')

                    p_text_data = bytes(p_text_sec.data())
                    p_text_start = p_text_sec['sh_addr']
                    p_bss_size = p_bss_sec['sh_size'] if p_bss_sec else 0

                    # Guard against silent data loss: plugin .data/.rodata are never
                    # uploaded, so any initialized globals would be silently missing.
                    p_data_sec = p_elf.get_section_by_name('.data')
                    if p_data_sec is not None and p_data_sec['sh_size'] > 0:
                        raise SystemExit(
                            f"ERROR: Plugin '{p_name}' has a non-empty .data section "
                            f'({p_data_sec["sh_size"]} bytes).\n'
                            f'Plugin .data/.rodata are not uploaded. '
                            f'Use BSS or constants-in-text instead.'
                        )

                    p_rodata_sec = p_elf.get_section_by_name('.rodata')
                    if p_rodata_sec is not None and p_rodata_sec['sh_size'] > 0:
                        raise SystemExit(
                            f"ERROR: Plugin '{p_name}' has a non-empty .rodata section "
                            f'({p_rodata_sec["sh_size"]} bytes).\n'
                            f'Plugin .data/.rodata are not uploaded. '
                            f'Use BSS or constants-in-text instead.'
                        )

                    # Align plugin .text to 4-byte boundary: Xtensa IRAM only supports
                    # 32-bit-width stores; the ROM memcpy tail-byte loop would fault.
                    overreach = len(p_text_data) % 4
                    if overreach != 0:
                        p_text_data += b'\x00' * (4 - overreach)

                    handler_map = PLUGIN_HANDLER_SYMBOLS.get(p_name, {})
                    if not handler_map:
                        print(
                            f"WARNING: unknown plugin '{p_name}' — no handler symbols defined",
                            file=sys.stderr,
                        )
                    handlers: Dict[str, int] = {}
                    missing_symbols = []
                    for opcode_str, sym_name in handler_map.items():
                        sym_addr = _get_symbol_addr(p_elf, sym_name)
                        if sym_addr is None:
                            missing_symbols.append(sym_name)
                            continue
                        handlers[opcode_str] = sym_addr - p_text_start

                    if missing_symbols:
                        raise SystemExit(
                            f"ERROR: Plugin '{p_name}': missing handler symbol(s) in {p_elf_file}: "
                            + ', '.join(missing_symbols)
                        )

                stub['plugins'][p_name] = {
                    'text': p_text_data,
                    'text_start': p_text_start,
                    'bss_size': p_bss_size,
                    'handlers': handlers,
                }

    return stub


def main():
    parser = argparse.ArgumentParser(description='Convert flasher stub ELF to JSON')
    parser.add_argument('elf_file', help='Base stub ELF file input')
    parser.add_argument('json_file', help='JSON file output')
    parser.add_argument(
        '--plugin',
        nargs=2,
        metavar=('NAME', 'PLUGIN_ELF'),
        action='append',
        default=[],
        help='Embed plugin data: --plugin <name> <plugin_elf>. May be repeated for multiple plugins.',
    )
    args = parser.parse_args()

    stub = get_stub_sections(args.elf_file, plugins=args.plugin)

    with open(args.json_file, 'w') as json_file:
        json.dump(stub, json_file, cls=ByteEncoder, indent=4)

    if args.plugin:
        names = ', '.join(p[0] for p in args.plugin)
        print(f'Written {args.json_file} with plugin(s): {names}.')


if __name__ == '__main__':
    main()
