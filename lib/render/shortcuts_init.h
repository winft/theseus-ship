/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "post/night_color_setup.h"

namespace KWin::render
{

template<typename Platform>
void init_shortcuts(Platform& platform)
{
    post::init_night_color_shortcuts(*platform.base.mod.input, *platform.night_color);
}

}
