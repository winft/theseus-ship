/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../utils/flags.h"

namespace KWin::win
{

enum class position {
    center = 0x0,
    left = 0x1,
    right = 0x2,
    top = 0x4,
    bottom = 0x8,
    top_left = left | top,
    top_right = right | top,
    bottom_left = left | bottom,
    bottom_right = right | bottom,
};

enum class size_mode {
    any,
    fixed_width,  ///< Try not to affect width
    fixed_height, ///< Try not to affect height
    max,          ///< Try not to make it larger in either direction
};

/**
 * Values are written to config files. Do not change.
 */
enum class maximize_mode {
    restore = 0x0,    ///< The window is not maximized in any direction.
    vertical = 0x1,   ///< The window is maximized vertically.
    horizontal = 0x2, ///< The window is maximized horizontally.
    full = vertical | horizontal,
};

enum class same_client_check {
    relaxed_for_active = 0x1,
    allow_cross_process = 0x2,
};

enum class layer {
    unknown = -1,
    first = 0,
    desktop = first,
    below,
    normal,
    dock,
    above,
    notification,          // layer for windows of type notification
    active,                // active fullscreen, or active dialog
    critical_notification, // layer for notifications that should be shown even on top of fullscreen
    on_screen_display,     // layer for On Screen Display windows such as volume feedback
    unmanaged,             // layer for override redirect windows.
    count                  // number of layers, must be last
};

enum class pending_geometry {
    none,
    normal,
    forced,
};

/**
 * Placement policies. How workspace decides the way windows get positioned
 * on the screen. The better the policy, the heavier the resource use.
 * Normally you don't have to worry. What the WM adds to the startup time
 * is nil compared to the creation of the window itself in the memory
 */
enum class placement {
    no_placement,   // not really a placement
    global_default, // special, means to use the global default
    unknown,        // special, means the function should use its default
    random,
    smart,
    centered,
    zero_cornered,
    under_mouse,    // special
    on_main_window, // special
    maximizing,     //
    count,          // Number of placement policies, must be last.
};

enum class strut_area {
    invalid = 0,
    top = 1 << 0,
    right = 1 << 1,
    bottom = 1 << 2,
    left = 1 << 3,
    all = top | right | bottom | left,
};

enum class quicktiles {
    none = 0,
    left = 0x1,
    right = 0x2,
    top = 0x4,
    bottom = 0x8,
    horizontal = left | right,
    vertical = top | bottom,
    maximize = left | right | top | bottom,
};

}

ENUM_FLAGS(KWin::win::position)
ENUM_FLAGS(KWin::win::maximize_mode)
ENUM_FLAGS(KWin::win::same_client_check)
ENUM_FLAGS(KWin::win::strut_area)
ENUM_FLAGS(KWin::win::quicktiles)
