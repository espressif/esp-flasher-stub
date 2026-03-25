#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Compare flasher stub segment sizes between two builds.

Reads JSON files produced by elf2json.py from two directory trees (typically
the latest master build and the current PR build) and prints a Markdown table
comparing the sizes of each segment (text, data, plugin text) per chip.
"""

import argparse
import base64
import json
import re
import sys
from pathlib import Path

# Only allow safe characters in names used in Markdown output
_SAFE_NAME_RE = re.compile(r'^[a-zA-Z0-9][a-zA-Z0-9._\- ]*$')


def find_json_files(directory: Path) -> dict[str, Path]:
    """Find all chip JSON files in *directory*, excluding ``*.base.json``.

    File stems that contain unexpected characters are skipped to prevent
    Markdown injection via crafted artifact names.
    """
    results: dict[str, Path] = {}
    for p in sorted(directory.rglob('*.json')):
        if p.name.endswith('.base.json'):
            continue
        if not _SAFE_NAME_RE.match(p.stem):
            print('WARNING: skipping file with unexpected name', file=sys.stderr)
            continue
        results[p.stem] = p
    return results


def get_segment_sizes(json_path: Path) -> dict[str, int]:
    """Return a mapping of segment name to size in bytes."""
    with open(json_path) as f:
        data = json.load(f)

    sizes: dict[str, int] = {}

    if 'text' in data:
        sizes['text'] = len(base64.b64decode(data['text']))
    if 'data' in data:
        sizes['data'] = len(base64.b64decode(data['data']))

    for plugin_name, plugin_data in data.get('plugins', {}).items():
        if not _SAFE_NAME_RE.match(plugin_name):
            print('WARNING: skipping plugin with unexpected name', file=sys.stderr)
            continue
        if 'text' in plugin_data:
            sizes[f'{plugin_name} plugin'] = len(base64.b64decode(plugin_data['text']))

    return sizes


def _fmt_diff(old: int | None, new: int | None) -> tuple[str, str, str, str]:
    """Return (old_str, new_str, diff_str, pct_str) for a single segment."""
    if old is not None and new is not None:
        diff = new - old
        sign = '+' if diff > 0 else ''
        if old == 0:
            pct_str = 'N/A' if diff != 0 else '+0.00%'
        else:
            pct_str = f'{diff / old * 100:+.2f}%'
        return (
            f'{old:,}',
            f'{new:,}',
            f'{sign}{diff:,}',
            pct_str,
        )
    if old is None and new is not None:
        return ('-', f'{new:,}', 'new', 'N/A')
    if old is not None and new is None:
        return (f'{old:,}', '-', 'removed', 'N/A')
    return ('-', '-', '-', '-')


def _segment_sort_key(name: str) -> tuple[int, str]:
    """Sort segments: text first, data second, then plugins alphabetically."""
    if name == 'text':
        return (0, name)
    if name == 'data':
        return (1, name)
    return (2, name)


def generate_report(master_dir: Path | None, pr_dir: Path) -> str:
    """Generate a Markdown size comparison report with a separate table per chip."""
    master_files = find_json_files(master_dir) if master_dir and master_dir.is_dir() else {}
    pr_files = find_json_files(pr_dir) if pr_dir.is_dir() else {}
    all_chips = sorted(set(list(master_files) + list(pr_files)))

    if not all_chips:
        return '## 📊 Stub Size Report\n\nNo stub JSON files found to compare.\n'

    lines: list[str] = ['## 📊 Stub Size Report\n']

    if not master_files:
        lines.append('> **Note:** No master build found for comparison. Showing current PR sizes only.\n')

    total_master = 0
    total_pr = 0
    any_change = False

    for chip in all_chips:
        m_sizes = get_segment_sizes(master_files[chip]) if chip in master_files else {}
        p_sizes = get_segment_sizes(pr_files[chip]) if chip in pr_files else {}
        all_segments = sorted(set(list(m_sizes) + list(p_sizes)), key=_segment_sort_key)

        lines.append(f'### {chip}\n')
        lines.extend(
            [
                '| Segment | master (bytes) | PR (bytes) | Diff (bytes) | Diff (%) |',
                '|---------|---------------:|-----------:|------------:|---------:|',
            ]
        )

        chip_master = 0
        chip_pr = 0
        for seg in all_segments:
            ms = m_sizes.get(seg)
            ps = p_sizes.get(seg)
            m_str, p_str, d_str, pct_str = _fmt_diff(ms, ps)
            lines.append(f'| {seg} | {m_str} | {p_str} | {d_str} | {pct_str} |')

            if ms is not None:
                chip_master += ms
                total_master += ms
            if ps is not None:
                chip_pr += ps
                total_pr += ps
            if ms != ps:
                any_change = True

        # Per-chip total row (only when both sides have data for this chip)
        if chip in master_files and chip in pr_files and len(all_segments) > 1:
            _, _, cd_str, cp_str = _fmt_diff(chip_master, chip_pr)
            lines.append(f'| **Total** | **{chip_master:,}** | **{chip_pr:,}** | **{cd_str}** | **{cp_str}** |')

        lines.append('')

    if master_files and not any_change:
        lines.append('✅ No size changes detected.\n')

    lines.append('')
    return '\n'.join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Compare flasher stub segment sizes between master and PR builds',
    )
    parser.add_argument(
        '--master-dir',
        type=Path,
        default=None,
        help='Directory tree containing master build JSON files',
    )
    parser.add_argument(
        '--pr-dir',
        type=Path,
        required=True,
        help='Directory tree containing PR build JSON files',
    )
    args = parser.parse_args()

    report = generate_report(args.master_dir, args.pr_dir)
    sys.stdout.write(report)


if __name__ == '__main__':
    main()
