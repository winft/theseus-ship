/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/flags.h"

namespace KWin::win
{

enum class direction {
    north,
    east,
    south,
    west,
};

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
    popup,                 // layer for popups, xdg shell popups, etc
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

enum class focus_chain_change {
    make_first,
    make_last,
    update,
};

/// Focus Stealing Prevention Levels
enum class fsp_level {
    none = 0,
    low,
    medium,
    high,
    extreme,
};

enum class window_type {
    unknown = -1,
    normal = 0,
    desktop = 1,
    dock = 2,
    toolbar = 3,
    menu = 4,
    dialog = 5,
    /**
           @deprecated has unclear meaning and is KDE-only
    **/
    override = 6, // NON STANDARD
    top_menu = 7, // NON STANDARD
    utility = 8,
    splash = 9,
    dropdown_menu = 10,
    popup_menu = 11,
    tooltip = 12,
    notification = 13,
    combo_box = 14,
    dnd_icon = 15,
    on_screen_display = 16,     // NON STANDARD
    critical_notification = 17, // NON STANDARD
    applet_popup = 18,          // NON STANDARD
};

enum class window_type_mask : unsigned int {
    normal = 1u << 0,
    desktop = 1u << 1,
    dock = 1u << 2,
    toolbar = 1u << 3,
    menu = 1u << 4,
    dialog = 1u << 5,
    override = 1u << 6,
    top_menu = 1u << 7,
    utility = 1u << 8,
    splash = 1u << 9,
    dropdown_menu = 1u << 10,
    popup_menu = 1u << 11,
    tooltip = 1u << 12,
    notification = 1u << 13,
    combo_box = 1u << 14,
    dnd_icon = 1u << 15,
    on_screen_display = 1u << 16,
    critical_notification = 1u << 17,
    applet_popup = 1u << 18,
    all_types = 0xffffffff,
};

}

ENUM_FLAGS(KWin::win::position)
ENUM_FLAGS(KWin::win::maximize_mode)
ENUM_FLAGS(KWin::win::same_client_check)
ENUM_FLAGS(KWin::win::strut_area)
ENUM_FLAGS(KWin::win::quicktiles)
ENUM_FLAGS(KWin::win::window_type_mask)
