#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import fileinput

for line in fileinput.input():
    if not line.startswith("Backend="):
        continue
    value = line[len("Backend="):].strip()
    if value != "XRender":
        continue
    print("# DELETE Backend") # will use the default compositing type
