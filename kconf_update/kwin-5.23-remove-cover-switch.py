#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import fileinput

for line in fileinput.input():
    if not line.startswith("LayoutName="):
        continue
    value = line[len("LayoutName="):].strip()
    if value != "coverswitch":
        continue
    print("# DELETE LayoutName") # will use the default layout
