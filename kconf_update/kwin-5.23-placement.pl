#! /usr/bin/perl

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Cascade placement has been removed. This equaled to Placement=Cascade in the config.

use strict;

while (<>)
{
    chomp;
    s/Placement=Cascade//g;
    print "$_\n";
}
