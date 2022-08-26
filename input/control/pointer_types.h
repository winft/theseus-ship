/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::input::control
{

enum class accel_profile {
    none,
    flat,
    adaptive,
};

enum class scroll {
    none,
    two_finger,
    edge,
    on_button_down,
};

enum class clicks {
    none,
    button_areas,
    finger_count,
};

}
