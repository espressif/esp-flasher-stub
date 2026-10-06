# Development Guide

## Contributing

### Before You Start

- Search the [existing issues](https://github.com/espressif/esp-flasher-stub/issues).
- Before you report a bug, check that it still happens with the latest esptool from the `master` branch on GitHub, installed as [Testing the Latest Code](https://docs.espressif.com/projects/esptool/en/latest/esp32/contributing.html#testing-the-latest-code) describes. In the report, give the chip you used and the output of `git describe` run in the `esptool` directory.
- The `master` branch of esptool contains the latest released stub. To test a stub that is not released yet, build it from this repository and install it as [How to Use with Esptool](../README.md#how-to-use-with-esptool) describes.

> [!IMPORTANT]
> Discuss every pull request with the maintainers in an [issue](https://github.com/espressif/esp-flasher-stub/issues/new/choose) first. Open the pull request only after they agree on the change.

### Development Setup

Set up the [Build Dependencies](../README.md#build-dependencies) and install the host test packages in [Prerequisites](../unittests/README.md#prerequisites). Then install the [pre-commit](https://pre-commit.com/) hooks, including the `commit-msg` hook, in the virtual environment:

```sh
source venv/bin/activate
pip install pre-commit
pre-commit install
```

### Code and Tests

- Add or update tests for every change in behaviour, and update the documents that [Documentation](#documentation) names.
- To add a chip, update the `esp-stub-lib` submodule to a commit of [esp-stub-lib](https://github.com/espressif/esp-stub-lib) that supports the chip. Then add the chip to `cmake/esp-targets.cmake`, `tools/build_all_chips.sh` and the [Supported Chips](../README.md#supported-chips) table, and add its linker script to `src/ld/`.
- Run `./run-tests.sh` in `unittests/host`. The host tests must pass. CI does not run them for pull requests from forks.
- Build the firmware for at least one chip as [How to Build](../README.md#how-to-build) describes, and check that the build directory contains `<chip>.json`.

### Pre-commit Hooks

> [!IMPORTANT]
> `pre-commit run --all-files` must pass before you open a pull request and before every push to it.

- The hooks in [.pre-commit-config.yaml](../.pre-commit-config.yaml) enforce the code style and the SPDX copyright headers. They reformat C, Python and YAML files. They add missing copyright headers and update the years in existing ones.
- A hook that changes a file fails. Review the changes, add them to the commit they belong to and run the hooks again.
- Do not bypass the hooks with `git commit --no-verify` or the `SKIP` environment variable. Do not add `# noqa` or `# type: ignore` unless the pull request description explains why.

### Commit Messages

- Write commit messages in the [Conventional Commits](https://www.conventionalcommits.org/) format.
- The `commit-msg` hook checks each message when you commit. `pre-commit run --all-files` does not check commit messages.
- Squash fixup commits, including the fix commit that pre-commit.ci pushes, into the commits they fix.
- Do not edit `CHANGELOG.md`. [commitizen](https://commitizen-tools.github.io/commitizen/) generates it from the commit messages when a release is made.

### Documentation

Update the documents that describe what you change:

- [README.md](../README.md) for supported chips, build dependencies and build steps.
- [Architecture](architecture.md) for the firmware architecture, source files, modules, the build system and linker scripts.
- [Plugin System](plugin-system.md) and the documents in [plugins](plugins/) for plugins.
- [unittests/README.md](../unittests/README.md) for tests.
- This guide for contribution rules, CI workflows, releasing and the scripts in `tools/` that the other documents do not describe.

### Pull Requests

- Open the pull request against `master` and keep it to one logical change.
- Fill in the sections of the [pull request template](https://github.com/espressif/esp-flasher-stub/blob/master/.github/pull_request_template.md), and name the issue in which the maintainers agreed on the change.
- The DangerJS bot comments when the pull request breaks one of its rules, for example on the commit messages, the description or the branch name. Make the changes that the comment asks for.

## CI/CD

| Workflow | Trigger | Purpose |
|---|---|---|
| Build and release | Push, pull request, manual | Builds the stubs for all chips and the npm package. Makes the size report on pull requests and a draft release on tags. |
| Post stub size report | Build and release finished for a pull request | Posts the size report as a comment on the pull request, also for pull requests from forks |
| Host Tests | Push | Runs the host tests |
| DangerJS Pull Request linter | Pull request | Checks the pull request against the DangerJS rules |
| Sync to Jira | New issue, issue comment, hourly, manual | Copies issues, comments and new pull requests to Jira |
| Publish npm package | Release published | Publishes the stub JSON files of the release as the [`esp-flasher-stub`](https://www.npmjs.com/package/esp-flasher-stub) npm package |

The [pre-commit.ci](https://pre-commit.ci/) service runs the pre-commit hooks on pull requests and pushes a commit with the fixes they make.

## Releasing (Maintainers Only)

```sh
source venv/bin/activate
pip install commitizen czespressif
git fetch
git checkout -b update/release_v<version>
git reset --hard origin/master
cz bump
git push -u origin HEAD
git push --tags
```

Open a pull request for the branch. The Build and release workflow creates a draft release for the pushed tag. Edit and publish it on the [releases page](https://github.com/espressif/esp-flasher-stub/releases).

## Utilities

| Script | Description |
|---|---|
| `tools/install_all_chips.sh` | Copies the JSON files from the `build-*` directories to the directory set in `ESPTOOL_STUBS_DIR`, for example `esptool/targets/stub_flasher/2` in an esptool checkout |
| `tools/compare_sizes.py` | Compares the stub segment sizes of two builds for the size report on pull requests |
| `tools/generate_npm_package.mjs` | Assembles the npm package from the stub JSON files |
