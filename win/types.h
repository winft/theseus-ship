/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_TYPES_H
#define KWIN_WIN_TYPES_H

#include "flags.h"

namespace KWin::win
{

enum class activation {
    none = 0x0,
    focus = 0x1,               ///< Focus the window
    focus_force = 0x2 | focus, ///< Focus even if Dock etc.
    raise = 0x4,               ///< Raise the window
};
ENUM_FLAGS(activation)

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
ENUM_FLAGS(position)

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
ENUM_FLAGS(maximize_mode)

enum class same_client_check {
    relaxed_for_active = 0x1,
    allow_cross_process = 0x2,
};
ENUM_FLAGS(same_client_check)

enum class force_geometry {
    no,
    yes, ///< Try not to make it larger in either direction
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
ENUM_FLAGS(quicktiles)

enum class shade {
    none,      /// Not shaded.
    normal,    /// Normally shaded - win::shaded() is true only here.
    hover,     /// Shaded but visible due to hover unshade.
    activated, /// Shaded but visible due to alt+tab to the window.
};

}

#endif
