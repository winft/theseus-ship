#! /usr/bin/env bash

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

$EXTRACTRC `find . -name \*.ui` >> rc.cpp || exit 11
$XGETTEXT `find . -name \*.cpp` -o $podir/kwin_clients.pot
rm -f rc.cpp
