#!/bin/bash

set -eu
set -o pipefail

function set_cmake_version {
    sed -i "s/\(project(KWinFT VERSION \).*)/\1${1})/g" CMakeLists.txt
}

# We always start the beta branching from master.
git checkout master

# Something like: kwinft@5.XX.0-beta.0
LAST_TAG=$(git describe --abbrev=0)
LAST_BETA_VERSION=$(echo $LAST_TAG | sed -e 's,kwinft@\(\),\1,g')

echo "Last beta version was: $LAST_BETA_VERSION"

# This updates version number from beta to its release
NEXT_VERSION=$(semver -i minor $LAST_BETA_VERSION)
# Needs to be done a second time to get the next minor version.
NEXT_VERSION=$(semver -i minor $NEXT_VERSION)

# The next tag is suffixed according to SemVer.
BETA_VERSION="${NEXT_VERSION}-beta.0"

# The CMake project version needs the *90 hack since CMake does not support SemVer.
CMAKE_VERSION=$(semver -i patch $LAST_BETA_VERSION)
CMAKE_VERSION=$(echo $CMAKE_VERSION | sed -e 's,\w$,90,g')

echo "Next beta version: '${BETA_VERSION}' Corresponding CMake project version: '${CMAKE_VERSION}'"

# This creates the changelog.
standard-version -t kwinft\@ --skip.commit true --skip.tag true --preMajor --release-as $BETA_VERSION

# Set CMake version.
set_cmake_version $CMAKE_VERSION

# Now we have all changes ready.
git add CMakeLists.txt CHANGELOG.md

# Commit and tag it.
git commit -m "build: create beta release ${BETA_VERSION}" -m "Update changelog and raise CMake project version to ${CMAKE_VERSION}."
git tag -a "kwinft@${BETA_VERSION}" -m "Create beta release ${BETA_VERSION}."

# Now checkout the next stable branch.
BRANCH_NAME="Plasma/$(echo $NEXT_VERSION | sed -e 's,^\(\w*\.\w*\)\..*,\1,g')"
echo "New stable branch name: $BRANCH_NAME"
git checkout -b $BRANCH_NAME

# Adapt the CI now using the stable image.
sed -i "s/IMAGE_VERSION\: master/IMAGE_VERSION: stable/g" .gitlab-ci.yml
git add .gitlab-ci.yml
git commit -m "ci: switch to stable image" -m "For running CI against stable branch $BRANCH_NAME as target."

# Go back to master branch and update version to next release.
git checkout master
MASTER_CMAKE_VERSION=$(semver -i minor $CMAKE_VERSION)
MASTER_CMAKE_VERSION=$(echo $MASTER_CMAKE_VERSION | sed -e 's,\w$,80,g')


# Set CMake version
set_cmake_version $MASTER_CMAKE_VERSION

# And commit the updated version.
git add CMakeLists.txt
git commit -m "build: raise CMake project version to ${MASTER_CMAKE_VERSION}" -m "For development on master branch."

echo "Changes applied. Check integrity of master and $BRANCH_NAME branches. Then issue 'git push --follow-tags' on both."
