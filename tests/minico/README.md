<!--
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
-->

# Minico - The Mini Wayland Compositor
Minico is a "minimum viable product" Wayland compositor based on the KWinFT libraries.
It is intended to serve as an example for creating a compositor using KWinFT libraries with as few lines of code as possible.
In this regard, it shares similarities with wlroot's [TinyWL](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl).
However, while TinyWL is more of a demo unit, Minico can be used in production as it includes all of KWinFT's windowing logic.

## Building Minico
o build Minico, configure the build using CMake.
Then, build Minico with the following command: `cmake --build <build-dir> --target minico`.

## Running Minico
After building, you can find the Minico binary in the build directory at `bin/minico`.
Once it's started, you can connect with clients by setting the `WAYLAND_DISPLAY` environment variable to the value claimed by Minico.
This is typically `wayland-0` or `wayland-1`, depending on whether other Wayland compositors are running.
