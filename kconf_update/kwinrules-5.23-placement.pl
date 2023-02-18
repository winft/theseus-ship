#! /usr/bin/perl

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Cascade placement has been removed. This equaled to placement=5 in the config.

use strict;

while (<>)
{
    chomp;
    s/placement=5/placement=1/;
    s/placement=6/placement=5/;
    s/placement=7/placement=6/;
    s/placement=8/placement=7/;
    s/placement=9/placement=8/;
    s/placement=10/placement=9/;
    print "$_\n";
}
