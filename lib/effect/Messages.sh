#! /usr/bin/env bash

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

$XGETTEXT `find . -name \*.cpp` -o $podir/libkwineffects.pot
