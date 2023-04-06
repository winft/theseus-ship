#! /usr/bin/env bash

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

$EXTRACTRC `find . -not -path "./kcms/*" \( -name \*.kcfg -o -name \*.ui \)` >> rc.cpp || exit 11
$XGETTEXT `find . -not -path "./kcms/*" \( -name \*.cpp -o -name \*.qml \)` -o $podir/kwin.pot
rm -f rc.cpp
