#!/usr/bin/env python
#
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT

import argparse
import base64
import json
import sys

from typing import Dict, Union

try:
    import esptool
except ImportError:
    raise SystemExit('Esptool not found. Please check the README for installation instructions.')


class ByteEncoder(json.JSONEncoder):
    """Custom JSON encoder to handle bytes."""

    def default(self, obj):
        if isinstance(obj, bytes):
            return base64.b64encode(obj).decode('ascii')
        return super().default(obj)


def get_stub_sections(elf_file: str) -> Dict[str, Union[int, bytes]]:
    elf = esptool.bin_image.ELFFile(elf_file)

    t = elf.get_section('.text')
    stub = {
        'entry': elf.entrypoint,
        'text': t.data,
        'text_start': t.addr,
    }
    stub_text_len = len(t.data)

    try:
        d = elf.get_section('.data')
        stub['data'] = d.data
        stub['data_start'] = d.addr
    except ValueError:
        print(f'WARNING: {elf_file} does not contain a .data section.', file=sys.stderr)
        pass

    for s in elf.nobits_sections:
        if s.name == '.bss':
            stub['bss_start'] = s.addr
            break
    else:
        print(f'WARNING: {elf_file} does not contain a .bss section.', file=sys.stderr)

    # If the length of the .text section is not a multiple of 4, padding is added to align it to a 4-byte boundary
    bytes_overreach = stub_text_len % 4
    if bytes_overreach != 0:
        stub['text'] += b'\x00' * (4 - bytes_overreach)

    return stub


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('elf_file', help='ELF file input')
    parser.add_argument('json_file', help='JSON file output')
    args = parser.parse_args()

    with open(args.json_file, 'w') as json_file:
        json.dump(get_stub_sections(args.elf_file), json_file, cls=ByteEncoder, indent=4)


if __name__ == '__main__':
    main()
