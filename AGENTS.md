# Agent Instructions

Follow the [Contributing](docs/development-guide.md#contributing) section of the Development Guide in every change, commit and description you write. In particular:

- Before you write a bug report, make sure that the problem was reproduced with the latest esptool from the `master` branch on GitHub, as [Before You Start](docs/development-guide.md#before-you-start) requires.
- Before you open a pull request, make sure that the maintainers agreed on the change in an issue, as [Before You Start](docs/development-guide.md#before-you-start) requires. If there is no such issue, write the issue instead of the pull request.
- Before your first build or commit, set up the project as in [Development Setup](docs/development-guide.md#development-setup).
- Before every push, pass the checks in [Code and Tests](docs/development-guide.md#code-and-tests) and [Pre-commit Hooks](docs/development-guide.md#pre-commit-hooks).
- Update the documents that [Documentation](docs/development-guide.md#documentation) names for your change.
- Write commits as in [Commit Messages](docs/development-guide.md#commit-messages), and pull requests as in [Pull Requests](docs/development-guide.md#pull-requests).
- Write an issue with the fields of the matching form at <https://github.com/espressif/esp-flasher-stub/issues/new/choose> and follow the instructions in the form. The forms are defined in [espressif/.github](https://github.com/espressif/.github/tree/main/.github/ISSUE_TEMPLATE). Do not create the issue with `gh issue create` or the GitHub API. They skip the form, so GitHub does not check its required fields or add its label. Give the user the text for each field and the link to the form.
- Write a pull request description with the sections of [.github/pull_request_template.md](.github/pull_request_template.md) and follow the instructions in its comments.
- Do not create an issue or pull request that breaks a rule of the guide, the issue form or the pull request template. Tell the user which rules it breaks and what must change.
