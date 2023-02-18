#! /usr/bin/env bash

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

$EXTRACTRC *.kcfg *.ui >> rc.cpp
$XGETTEXT *.h *.cpp colorcorrection/*.cpp helpers/killer/*.cpp plugins/scenes/opengl/*.cpp tabbox/*.cpp scripting/*.cpp -o $podir/kwin.pot
