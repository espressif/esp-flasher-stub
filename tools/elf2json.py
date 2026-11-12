#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT

import argparse
import base64
import json
import sys

from typing import Dict, List, Tuple, Union

try:
    from elftools.elf.elffile import ELFFile as PyELFFile
except ImportError:
    raise SystemExit('pyelftools not found. Install with: pip install pyelftools')


def _pad4(data: bytes) -> bytes:
    """Pad to a multiple of 4 bytes: Xtensa IRAM only supports 32-bit stores,
    and the ROM memcpy tail-byte loop would fault."""
    return data + b'\x00' * (-len(data) % 4)


class ByteEncoder(json.JSONEncoder):
    """Custom JSON encoder to handle bytes."""

    def default(self, o):
        if isinstance(o, bytes):
            return base64.b64encode(o).decode('ascii')
        return super().default(o)


def _get_symbol(elf: PyELFFile, name: str):
    """Return symbol *name* of a pyelftools ELFFile, or None."""
    symtab = elf.get_section_by_name('.symtab')
    syms = symtab.get_symbol_by_name(name) if symtab is not None else None
    return syms[0] if syms else None


def _get_plugin(name: str, elf_file: str) -> Dict[str, Union[int, bytes]]:
    """A plugin's JSON entry. All addresses are absolute.

    The host uploads text and data with the base stub, but not the BSS, which
    the plugin zeroes itself, and writes entry into the base plugin_start.
    """
    with open(elf_file, 'rb') as f:
        elf = PyELFFile(f)
        text = elf.get_section_by_name('.text')
        if text is None:
            raise SystemExit(f'ERROR: .text not found in plugin ELF {elf_file}')
        plugin: Dict[str, Union[int, bytes]] = {
            'text': _pad4(bytes(text.data())),
            'text_start': text['sh_addr'],
        }

        data = elf.get_section_by_name('.data')
        if data is not None and data['sh_size'] > 0:
            plugin['data'] = _pad4(bytes(data.data()))
            plugin['data_start'] = data['sh_addr']

        # common.ld places .rodata in .data; a separate .rodata would not be uploaded.
        rodata = elf.get_section_by_name('.rodata')
        if rodata is not None and rodata['sh_size'] > 0:
            raise SystemExit(
                f"ERROR: Plugin '{name}' has a .rodata section outside .data ({rodata['sh_size']} bytes). "
                f'Its linker script must INCLUDE common.ld.'
            )

        plugin['entry'] = elf.header.e_entry
    return plugin


def get_stub_sections(elf_file: str, plugins: List[Tuple[str, str]]) -> Dict[str, object]:
    with open(elf_file, 'rb') as f:
        elf = PyELFFile(f)

        t = elf.get_section_by_name('.text')
        if t is None:
            raise SystemExit(f'ERROR: .text section not found in {elf_file}')
        stub: Dict[str, object] = {
            'entry': elf.header.e_entry,
            'text': _pad4(bytes(t.data())),
            'text_start': t['sh_addr'],
        }

        d = elf.get_section_by_name('.data')
        if d is not None:
            stub['data'] = _pad4(bytes(d.data()))
            stub['data_start'] = d['sh_addr']
        else:
            print(f'WARNING: {elf_file} does not contain a .data section.', file=sys.stderr)

        bss = elf.get_section_by_name('.bss')
        if bss is not None:
            stub['bss_start'] = bss['sh_addr']
        else:
            print(f'WARNING: {elf_file} does not contain a .bss section.', file=sys.stderr)

        plugin_start = _get_symbol(elf, 'plugin_start')

    if not plugins:
        return stub

    if d is None or plugin_start is None:
        raise SystemExit('ERROR: Plugins need the plugin_start symbol in the base stub .data.')
    offset = plugin_start['st_value'] - d['sh_addr']
    if not 0 <= offset <= d['sh_size'] - plugin_start['st_size']:
        raise SystemExit(f'ERROR: plugin_start is outside the base stub .data (size {d["sh_size"]}).')
    stub['plugin_start_offset'] = offset
    stub['plugin_start_slots'] = plugin_start['st_size'] // 4
    stub['plugins'] = {name: _get_plugin(name, path) for name, path in plugins}
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

    stub = get_stub_sections(args.elf_file, args.plugin)

    with open(args.json_file, 'w') as json_file:
        json.dump(stub, json_file, cls=ByteEncoder, indent=4)

    if args.plugin:
        names = ', '.join(p[0] for p in args.plugin)
        print(f'Written {args.json_file} with plugin(s): {names}.')


if __name__ == '__main__':
    main()
