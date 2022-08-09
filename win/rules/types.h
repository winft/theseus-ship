/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/flags.h"

#include <cstdint>

namespace KWin::win::rules
{

enum class type : uint32_t {
    position = 1 << 0,
    size = 1 << 1,
    desktop = 1 << 2,
    maximize_vert = 1 << 3,
    maximize_horiz = 1 << 4,
    minimize = 1 << 5,
    shade = 1 << 6, // Deprecated
    skip_taskbar = 1 << 7,
    skip_pager = 1 << 8,
    skip_switcher = 1 << 9,
    above = 1 << 10,
    below = 1 << 11,
    fullscreen = 1 << 12,
    no_border = 1 << 13,
    opacity_active = 1 << 14,
    opacity_inactive = 1 << 15,
    activity = 1 << 16, // Deprecated
    screen = 1 << 17,
    desktop_file = 1 << 18,
    all = 0xffffffff,
};

// All these values are saved to the cfg file, and are also used in kstart!
enum class action {
    unused = 0,
    dont_affect,       // use the default value
    force,             // force the given value
    apply,             // apply only after initial mapping
    remember,          // like apply, and remember the value when the window is withdrawn
    apply_now,         // apply immediatelly, then forget the setting
    force_temporarily, // apply and force until the window is withdrawn
};

enum class name_match {
    first,
    unimportant = first,
    exact,
    substring,
    regex,
    last = regex,
};

}

ENUM_FLAGS(KWin::win::rules::type)
