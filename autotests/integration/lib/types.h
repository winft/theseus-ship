/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/flags.h"

namespace KWin::Test
{

enum class global_selection {
    seat = 1 << 0,
    xdg_decoration = 1 << 1,
    plasma_shell = 1 << 2,
    window_management = 1 << 3,
    pointer_constraints = 1 << 4,
    pointer_gestures = 1 << 4,
    idle_inhibition = 1 << 5,
    appmenu = 1 << 6,
    shadow = 1 << 7,
    xdg_activation = 1 << 8,
    input_method_v2 = 1 << 9,
    text_input_manager_v3 = 1 << 10,
    virtual_keyboard_manager_v1 = 1 << 11,
};

}

ENUM_FLAGS(KWin::Test::global_selection)
