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
    idle_inhibition = 1 << 5,
    appmenu = 1 << 6,
    shadow = 1 << 7,
    xdg_activation = 1 << 8,
};

}

ENUM_FLAGS(KWin::Test::global_selection)
