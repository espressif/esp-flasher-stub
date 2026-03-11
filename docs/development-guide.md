# Development Guide

For project overview, prerequisites, and build instructions, see the [README](../README.md). This guide covers architecture, testing, contributing guidelines, and CI/CD details.

## Architecture

For a detailed description of the firmware architecture, source code structure, modules, build system, and linker scripts, see the [Architecture](architecture.md) document.

## Testing

See [unittests/README.md](../unittests/README.md) for details on running and adding tests.

## Contributing

### Code Style

C code is formatted with [Artistic Style (astyle)](https://astyle.sourceforge.net/) version 3.4.7, configured in `.astyle-rules.yml`. Python code follows [ruff](https://docs.astral.sh/ruff/) formatting and linting rules defined in `pyproject.toml`.

### Pre-commit Hooks

Install and activate the [pre-commit](https://pre-commit.com/) hooks:

```sh
source venv/bin/activate
pip install pre-commit
pre-commit install -t pre-commit -t commit-msg
```

Run all checks manually:

```sh
pre-commit run --all-files
```

The hooks enforce:

- C code formatting (astyle)
- Python linting and formatting (ruff, mypy)
- Copyright header validation (Apache-2.0 OR MIT)
- Trailing whitespace, line endings, and YAML formatting
- [Conventional Commits](https://www.conventionalcommits.org/) message format

### Copyright Headers

All source files must include an SPDX copyright header. The copyright year range is automatically managed by the check-copyright tool.

**C files:**

```c
/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
```

**Python files:**

```python
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT
```

### Commit Messages

This project uses [Conventional Commits](https://www.conventionalcommits.org/). Releases are managed with [commitizen](https://commitizen-tools.github.io/commitizen/).

### Pull Request Checklist

Before submitting a pull request:

1. Run host tests:

   ```sh
   cd unittests/host && ./run-tests.sh && cd ../..
   ```

2. Build firmware for at least one chip:

   ```sh
   source venv/bin/activate
   source ./tools/export_toolchains.sh
   cmake . -B build -G Ninja -DTARGET_CHIP=esp32s2 --fresh
   ninja -C build
   ```

3. Run pre-commit hooks:

   ```sh
   pre-commit run --all-files
   ```

4. Verify the JSON output was generated:

   ```sh
   ls -la build/*.json
   ```

## CI/CD

### GitHub Actions Workflows

| Workflow | Trigger | Purpose |
|---|---|---|
| Build and Release | Push, PR | Build firmware for all chips; create releases on tags |
| Host Tests | Push | Run native unit tests |
| DangerJS | PR | Validate PR style and conventions |
| Jira | PR | Sync with Jira issue tracker |

Pre-commit.ci also runs automatically on pull requests to verify code style.

## Releasing (Maintainers Only)

```sh
python -m venv venv
source venv/bin/activate
pip install commitizen czespressif
git fetch
git checkout -b update/release_v<version>
git reset --hard origin/master
cz bump
git push -u
git push --tags
```

Create a pull request and edit the automatically created draft release on the [releases page](https://github.com/espressif/esp-flasher-stub/releases).

## Utilities

| Script | Description |
|---|---|
| `tools/build_all_chips.sh` | Build firmware for all supported chips |
| `tools/setup_toolchains.sh` | Download and extract cross-compilation toolchains |
| `tools/export_toolchains.sh` | Add toolchain directories to `PATH` (must be sourced) |
| `tools/elf2json.py` | Convert ELF binary to JSON format for esptool |
| `tools/install_all_chips.sh` | Copy built JSON files into an esptool installation |
