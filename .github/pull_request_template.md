<!-- Follow the contribution guide: https://github.com/espressif/esp-flasher-stub/blob/master/docs/development-guide.md#contributing
Write outside the comment markers.
Do not list changed files or functions, and do not restate the diff.
State only facts you verified. -->

## Related Issue

<!-- The issue in which the maintainers agreed on this change, for example "Closes #123".
Every pull request must first be discussed in an issue. -->

## Motivation

<!-- The problem this pull request solves and why it needs solving. -->

## Cause

<!-- A bug fix must state why the bug happens. If you have not confirmed the cause, say that it is a guess.
For other changes, delete the Cause heading and this comment. -->

## Goal

<!-- The behaviour after this change. If the approach is not obvious from the diff, explain why you chose it. -->

## Testing

<!-- The commands you ran, for example `./run-tests.sh` in `unittests/host`, and the chips you built the firmware for.
If you tested the stub with esptool, give the esptool command, the operating system, and the chip and board you used.
List only tests that ran. Paste commands and any output as text, not as screenshots. -->

## Checklist

- [ ] The maintainers agreed on this change in the issue named above.
- [ ] The pre-commit hooks are installed and `pre-commit run --all-files` passes.
- [ ] The host tests pass: `./run-tests.sh` in `unittests/host`.
- [ ] The firmware builds for at least one chip.
