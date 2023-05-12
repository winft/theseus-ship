/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/flags.h"

namespace KWin::render
{

enum class image_filter_type {
    fast,
    good,
};

enum class paint_type {
    none = 0,

    // Window (or at least part of it) will be painted opaque.
    window_opaque = 1 << 0,

    // Window (or at least part of it) will be painted translucent.
    window_translucent = 1 << 1,

    // Window will be painted with transformed geometry.
    window_transformed = 1 << 2,

    // Paint only a region of the screen (can be optimized, cannot
    // be used together with TRANSFORMED flags).
    screen_region = 1 << 3,

    // Whole screen will be painted with transformed geometry.
    screen_transformed = 1 << 4,

    // At least one window will be painted with transformed geometry.
    screen_with_transformed_windows = 1 << 5,

    // Clear whole background as the very first step, without optimizing it
    screen_background_first = 1 << 6,

    // decoration_only = 1 << 7 has been removed

    // Window will be painted with a lanczos filter.
    window_lanczos = 1 << 8

    // screen_with_transformed_windows_without_full_repaints = 1 << 9 has been removed
};

enum class window_paint_disable_type {
    none = 0,

    // Window will not be painted for an unspecified reason
    unspecified = 1 << 0,

    // Window will not be painted because it is deleted
    by_delete = 1 << 1,

    // Window will not be painted because of which desktop it's on
    by_desktop = 1 << 2,

    // Window will not be painted because it is minimized
    by_minimize = 1 << 3,

    // Window will not be painted because it's not on the current activity
    by_activity = 1 << 5, /// Deprecated
};

enum class shadow_element {
    top,
    top_right,
    right,
    bottom_right,
    bottom,
    bottom_left,
    left,
    top_left,
    count,
};

enum class state {
    on = 0,
    off,
    starting,
    stopping,
};

enum class animation_curve {
    linear,
    quadratic,
    cubic,
    quartic,
    sine,
};

/// Flags defining how a Loader should load an Effect.
enum class load_effect_flags {
    load = 1 << 0,                   /// Effect should be loaded
    check_default_function = 1 << 2, /// Invoke check default function if the effect provides it
};

enum night_color_mode {
    /// Color temperature based on current sun position. Location computed by external means.
    automatic,
    /// Color temperature based on current sun position. Location manually set by user.
    location,
    /// Color temperature based on current time. Sunrise/-set times manually set by user.
    timings,
    /// Color temperature is constant thoughout the day.
    constant,
};

}

ENUM_FLAGS(KWin::render::paint_type)
ENUM_FLAGS(KWin::render::window_paint_disable_type)
ENUM_FLAGS(KWin::render::load_effect_flags)
