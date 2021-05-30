#! /usr/bin/perl

# Cascade placement has been removed. This equaled to Placement=Cascade in the config.

use strict;

while (<>)
{
    chomp;
    s/Placement=Cascade//g;
    print "$_\n";
}
