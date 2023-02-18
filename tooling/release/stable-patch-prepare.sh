#!/bin/bash

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu
set -o pipefail

function set_cmake_version {
    sed -i "s/\(project(KWinFT VERSION \).*)/\1${1})/g" CMakeLists.txt
}

# We start from master (that has the beta tag).
git checkout master

# Something like: kwinft@5.XX.0-beta.0
LAST_TAG=$(git describe --abbrev=0)
LAST_BETA_VERSION=$(echo $LAST_TAG | sed -e 's,kwinft@\(\),\1,g')

echo "Last beta version was: $LAST_BETA_VERSION"

# We always release from stable branch.
BRANCH_NAME="Plasma/$(echo $LAST_BETA_VERSION | sed -e 's,^\(\w*\.\w*\)\..*,\1,g')"
echo "Stable branch name: $BRANCH_NAME"
git checkout $BRANCH_NAME

# Now get current patch level
LAST_STABLE_TAG=$(git describe --abbrev=0)
LAST_STABLE_VERSION=$(echo $LAST_STABLE_TAG | sed -e 's,kwinft@\(\),\1,g')

echo "Last stable version was: $LAST_STABLE_VERSION"

# This updates patch level
PATCH_VERSION=$(semver -i patch $LAST_STABLE_VERSION)

# The CMake project version is the same as the release version.
CMAKE_VERSION=$PATCH_VERSION

echo "Next stable version: '${PATCH_VERSION}' Corresponding CMake project version: '${PATCH_VERSION}'"

# This creates the changelog.
standard-version -t kwinft\@ --skip.commit true --skip.tag true --preMajor --release-as $PATCH_VERSION

# Set CMake version.
set_cmake_version $CMAKE_VERSION

# Now we have all changes ready.
git add CMakeLists.txt CHANGELOG.md

# Commit and tag it.
git commit -m "build: create patch release ${PATCH_VERSION}" -m "Update changelog and raise CMake project version to ${CMAKE_VERSION}."
git tag -a "kwinft@${PATCH_VERSION}" -m "Create patch release ${PATCH_VERSION}."

# Go back to master branch and update changelog.
git checkout master
git checkout $BRANCH_NAME CHANGELOG.md
git commit -m "docs: update changelog" -m "Update changelog from branch $BRANCH_NAME at patch release ${PATCH_VERSION}."

echo "Changes applied. Check integrity of master and $BRANCH_NAME branches. Then issue 'git push --follow-tags' on both."
