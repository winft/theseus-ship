#!/bin/bash

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -eux
set -o pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
SOURCE_DIR=$(dirname $(dirname ${SCRIPT_DIR}))
RUN_SCRIPT_URL="https://gitlab.com/kwinft/tooling/-/raw/master/analysis/run-clang-format.py"

python <(curl -s $RUN_SCRIPT_URL) -r \
    ${SOURCE_DIR}/autotests \
    ${SOURCE_DIR}/base \
    ${SOURCE_DIR}/cmake \
    ${SOURCE_DIR}/data \
    ${SOURCE_DIR}/debug \
    ${SOURCE_DIR}/desktop \
    ${SOURCE_DIR}/input \
    ${SOURCE_DIR}/lib \
    ${SOURCE_DIR}/plugins \
    ${SOURCE_DIR}/win \
    ${SOURCE_DIR}/render \
    ${SOURCE_DIR}/scripting \
    ${SOURCE_DIR}/utils \
    ${SOURCE_DIR}/xwl
