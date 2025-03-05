[![pre-commit.ci status](https://results.pre-commit.ci/badge/github/espressif/esp-flasher-stub/master.svg)](https://results.pre-commit.ci/latest/github/espressif/esp-flasher-stub/master)

# esp-flasher-stub

This project is experimental and not yet ready for production use.

The project's goal is to replace the legacy [flasher stub of esptool](https://github.com/espressif/esptool-legacy-flasher-stub/) in the near future.

# How To Use

1. Install esptool in [development mode](https://docs.espressif.com/projects/esptool/en/latest/esp32/contributing.html#development-setup).
2. Obtain the flasher stub binaries as JSON files either from the [releases page](https://github.com/espressif/esp-flasher-stub) or from the artifacts of your pull request.
3. Replace the esptool's JSONs files in the `esptool/targets/stub_flasher` directory with the obtained JSON files.

## Contributing

Please install the [pre-commit](https://pre-commit.com/) hooks to ensure that your commits are properly formatted:

```bash
pip install pre-commit
pre-commit install -t pre-commit -t commit-msg
```

# How To Release (For Maintainers Only)

```bash
pip install commitizen
git fetch
git checkout -b update/release_v1.1.0
git reset --hard origin/master
cz bump
git push -u
git push --tags
```
Create a pull request and edit the automatically created draft [release notes](https://github.com/espressif/esp-flasher-stub/releases).

## License

This document and the attached source code are released as Free Software under Apache License Version 2. See the accompanying [LICENSE file](https://github.com/espressif/esp-flasher-stub/blob/master/LICENSE) for a copy.
