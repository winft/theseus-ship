<!--
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
-->

# Theseus' Ship Tooling
## Analysis
Uses the analysis functionality from the [tooling repo][kwinft-tooling].

The provided scripts can be run locally, too.
Additional tools are downloaded temporarily from the tooling repo.

## Documentation
Uses the commit message lint functionality from the [tooling repo][kwinft-tooling].
See there for information on how the commitlint tool can be run locally too.

## Release
There are a *beta-prepare* and a *stable-prepare* script.
At the moment both are to be executed locally
and the new commits then pushed with tags to the server.
In both cases the current working directory
must be the source root directory when executing the script.

GitLab release notes must be added afterwards manually.
At the moment we just reuse the changelog.

[kwinft-tooling]: https://gitlab.com/kwinft/tooling
