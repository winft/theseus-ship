#! /usr/bin/env bash

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

$XGETTEXT `find . -name \*.h -o -name \*.cpp` -o $podir/kwin_scripting.pot
