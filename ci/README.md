# KWinFT CI tooling
## Overview
`TODO`

## Linters
### Commit messages
Uses [commitlint][commitlint] with the [conventionalcommits preset][commitlint-preset] and
additional configuration in `ci/compliance/commitlint.config.js`.

### Clang linter scripts
* clang-format wrapper [Credit][clang-format-wrapper], [MIT][mit-license].

These scripts can be used locally too, what is simplified through the `ci/compliance/clang-*.sh`
scripts.

> :information_source: For clang-format the project must have been compiled with the
> `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` CMake option. And you must launch clang-format, respectively
> its wrapper script, from your build directory.

## Release
There is a *beta-prepare* and a *stable-prepare* script. At the moment both are to be executed
locally and the new commits then pushed with tags to the server. In both cases the current working
directory must be the source root directory when executing the script.

GitLab release notes must be added afterwards manually. At the moment we just reuse the changelog.

[commitlint]: https://github.com/conventional-changelog/commitlint
[commitlint-preset]: https://github.com/conventional-changelog/conventional-changelog/tree/master/packages/conventional-changelog-conventionalcommits
[clang-format-wrapper]: https://github.com/Sarcasm/run-clang-format
[mit-license]: https://opensource.org/licenses/MIT
